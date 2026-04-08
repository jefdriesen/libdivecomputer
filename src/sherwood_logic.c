/*
 * libdivecomputer
 *
 * Copyright (C) 2026 Jef Driesen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "sherwood_logic.h"
#include "context-private.h"
#include "device-private.h"
#include "platform.h"
#include "checksum.h"
#include "array.h"

#define MAX_DATA 256

#define BLOCKSIZE 200

#define CMD_RESPONSE 0x01

#define CMD_AUTH   0x41
#define CMD_AUTH_UNKNOWN 0x31

#define CMD_QUERY 0xA0
#define CMD_QUERY_MODEL 0x01

#define CMD_DATA   0xD0
#define CMD_DATA_SERIAL 0x10

#define CMD_FILE   0xE0
#define CMD_FILE_COUNT 0x00
#define CMD_FILE_STAT  0x01
#define CMD_FILE_OPEN  0x02
#define CMD_FILE_READ  0x03
#define CMD_FILE_CLOSE 0x04

typedef struct sherwood_logic_file_t {
	unsigned char name[12];
	unsigned int size;
} sherwood_logic_file_t;

typedef struct sherwood_logic_device_t {
	dc_device_t base;
	dc_iostream_t *iostream;
	unsigned char fingerprint[4];
} sherwood_logic_device_t;

static dc_status_t sherwood_logic_device_set_fingerprint (dc_device_t *abstract, const unsigned char data[], unsigned int size);
static dc_status_t sherwood_logic_device_foreach (dc_device_t *abstract, dc_dive_callback_t callback, void *userdata);

static const dc_device_vtable_t sherwood_logic_device_vtable = {
	sizeof(sherwood_logic_device_t),
	DC_FAMILY_SHERWOOD_LOGIC,
	sherwood_logic_device_set_fingerprint, /* set_fingerprint */
	NULL, /* read */
	NULL, /* write */
	NULL, /* dump */
	sherwood_logic_device_foreach, /* foreach */
	NULL, /* timesync */
	NULL, /* close */
};

static dc_status_t
sherwood_logic_send (sherwood_logic_device_t *device, unsigned char cmd, const unsigned char data[], size_t size)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_device_t *abstract = (dc_device_t *) device;

	if (size > MAX_DATA)
		return DC_STATUS_INVALIDARGS;

	if (device_is_cancelled (abstract))
		return DC_STATUS_CANCELLED;

	unsigned char packet[1 + MAX_DATA + 1] = {0};
	packet[0] = cmd;
	if (size) {
		memcpy (packet + 1, data, size);
	}
	packet[1 + size] = checksum_crc8 (packet, 1 + size, 0x00, 0x00);

	status = dc_iostream_write (device->iostream, packet, 1 + size + 1, NULL);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (device->base.context, "Failed to send the packet.");
		return status;
	}

	return status;
}

static dc_status_t
sherwood_logic_recv (sherwood_logic_device_t *device, unsigned char cmd, unsigned char data[], size_t size)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_device_t *abstract = (dc_device_t *) device;

	if (size > MAX_DATA)
		return DC_STATUS_INVALIDARGS;

	if (device_is_cancelled (abstract))
		return DC_STATUS_CANCELLED;

	size_t length = 0;
	unsigned char packet[1 + MAX_DATA + 1] = {0};
	status = dc_iostream_read (device->iostream, packet, sizeof(packet), &length);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (device->base.context, "Failed to read the packet.");
		goto error_exit;
	}

	// Verify the minimum length of the packet.
	if (length < 2) {
		ERROR (abstract->context, "Unexpected packet length (" DC_PRINTF_SIZE ").", length);
		status = DC_STATUS_PROTOCOL;
		goto error_exit;
	}

	// Verify the checksum.
	unsigned char crc = packet[length - 1];
	unsigned char ccrc = checksum_crc8 (packet, length - 1, 0x00, 0x00);
	if (crc != ccrc) {
		ERROR (abstract->context, "Unexpected packet checksum (%02x %02x).", crc, ccrc);
		status = DC_STATUS_PROTOCOL;
		goto error_exit;
	}

	// Verify the command byte.
	unsigned char rsp = cmd | CMD_RESPONSE;
	if (packet[0] != rsp) {
		ERROR (abstract->context, "Unexpected command byte (%02x).", packet[0]);
		status = DC_STATUS_PROTOCOL;
		goto error_exit;
	}

	// Verify the maximum length of the packet.
	if (length - 2 != size) {
		ERROR (abstract->context, "Unexpected packet length (" DC_PRINTF_SIZE ").", length - 2);
		status = DC_STATUS_PROTOCOL;
		goto error_exit;
	}

	if (length - 2) {
		memcpy (data, packet + 1, length - 2);
	}

error_exit:
	return status;
}

static dc_status_t
sherwood_logic_read_model (sherwood_logic_device_t *device, unsigned char data[], size_t size)
{
	unsigned char params[] = {CMD_QUERY_MODEL, 0x01}, payload[8] = {0};
	sherwood_logic_send (device, CMD_QUERY, params, sizeof(params));
	sherwood_logic_recv (device, CMD_QUERY, payload, sizeof(payload));

	memcpy (data, payload + 2, sizeof(payload) - 2);

	return DC_STATUS_SUCCESS;
}

static dc_status_t
sherwood_logic_read_serial (sherwood_logic_device_t *device, unsigned char data[], size_t size)
{
	unsigned char params[] = {CMD_DATA_SERIAL, 0x01}, payload[34] = {0};
	sherwood_logic_send (device, CMD_DATA, params, sizeof(params));
	sherwood_logic_recv (device, CMD_DATA, payload, sizeof(payload));

	memcpy (data, payload + 2, sizeof(payload) - 2);

	return DC_STATUS_SUCCESS;
}

static dc_status_t
sherwood_logic_auth (sherwood_logic_device_t *device)
{
	unsigned char params[] = {0x31, 0x00, 0x05, 0x69, 0x8a, 0x12, 0x9d, 0xd8};
	sherwood_logic_send (device, CMD_AUTH, params, sizeof(params));
	sherwood_logic_recv (device, CMD_AUTH, NULL, 0);

	return DC_STATUS_SUCCESS;
}

static dc_status_t
sherwood_logic_fileop (sherwood_logic_device_t *device, unsigned char cmd, const unsigned char idata[], size_t isize, unsigned char odata[], size_t osize)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_device_t *abstract = (dc_device_t *) device;

	if (isize > MAX_DATA || osize > MAX_DATA)
		return DC_STATUS_INVALIDARGS;

	unsigned char params[3 + MAX_DATA] = {0};
	params[0] = 0x00;
	params[1] = 0x01;
	params[2] = cmd;
	if (isize) {
		memcpy (params + 3, idata, isize);
	}

	status = sherwood_logic_send (device, CMD_FILE, params, 3 + isize);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (abstract->context, "Failed to send the packet.");
		return status;
	}

	unsigned char response[2 + MAX_DATA] = {0};
	status = sherwood_logic_recv (device, CMD_FILE, response, 2 + osize);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (abstract->context, "Failed to receive the packet.");
		return status;
	}

	memcpy (odata, response + 2, osize);

	return status;
}

static dc_status_t
sherwood_logic_file_count (sherwood_logic_device_t *device, unsigned int *count)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_device_t *abstract = (dc_device_t *) device;

	unsigned char response[2] = {0};
	status = sherwood_logic_fileop (device, CMD_FILE_COUNT, NULL, 0, response, sizeof(response));
	if (status != DC_STATUS_SUCCESS) {
		ERROR (abstract->context, "Failed to send the packet.");
		return status;
	}

	*count = array_uint16_le (response);

	return status;
}

static dc_status_t
sherwood_logic_file_stat (sherwood_logic_device_t *device, unsigned int idx, sherwood_logic_file_t *file)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_device_t *abstract = (dc_device_t *) device;

	unsigned char params[2] = {0};
	unsigned char response[18] = {0};
	array_uint16_le_set(params, idx);
	status = sherwood_logic_fileop (device, CMD_FILE_STAT, params, sizeof(params), response, sizeof(response));
	if (status != DC_STATUS_SUCCESS) {
		ERROR (abstract->context, "Failed to send the packet.");
		return status;
	}

	memcpy (file->name, response + 2, 12);
	file->size = array_uint32_le (response + 14);

	return status;
}

static dc_status_t
sherwood_logic_file_open (sherwood_logic_device_t *device, unsigned int idx)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_device_t *abstract = (dc_device_t *) device;

	unsigned char params[2] = {0};
	array_uint16_le_set(params, idx);
	status = sherwood_logic_fileop (device, CMD_FILE_OPEN, params, sizeof(params), NULL, 0);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (abstract->context, "Failed to send the packet.");
		return status;
	}

	return status;
}

static dc_status_t
sherwood_logic_file_read (sherwood_logic_device_t *device, unsigned int idx, unsigned int offset, unsigned char data[], unsigned int size)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_device_t *abstract = (dc_device_t *) device;

	unsigned char params[8] = {0};
	array_uint32_le_set(params + 0, offset);
	array_uint32_le_set(params + 4, size);
	status = sherwood_logic_fileop (device, CMD_FILE_READ, params, sizeof(params), data, size);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (abstract->context, "Failed to send the packet.");
		return status;
	}

	return status;
}

static dc_status_t
sherwood_logic_file_close (sherwood_logic_device_t *device, unsigned int idx)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_device_t *abstract = (dc_device_t *) device;

	unsigned char params[2] = {0};
	unsigned char response[1] = {0};
	array_uint16_le_set(params, idx);
	status = sherwood_logic_fileop (device, CMD_FILE_CLOSE, NULL, 0, response, sizeof(response));
	if (status != DC_STATUS_SUCCESS) {
		ERROR (abstract->context, "Failed to send the packet.");
		return status;
	}

	return status;
}

dc_status_t
sherwood_logic_device_open (dc_device_t **out, dc_context_t *context, dc_iostream_t *iostream)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	sherwood_logic_device_t *device = NULL;

	if (out == NULL)
		return DC_STATUS_INVALIDARGS;

	// Allocate memory.
	device = (sherwood_logic_device_t *) dc_device_allocate (context, &sherwood_logic_device_vtable);
	if (device == NULL) {
		ERROR (context, "Failed to allocate memory.");
		return DC_STATUS_NOMEMORY;
	}

	// Set the default values.
	device->iostream = iostream;
	memset(device->fingerprint, 0, sizeof(device->fingerprint));

	// Set the serial communication protocol (115200 8N1).
	status = dc_iostream_configure (device->iostream, 115200, 8, DC_PARITY_NONE, DC_STOPBITS_ONE, DC_FLOWCONTROL_NONE);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (context, "Failed to set the terminal attributes.");
		goto error_free;
	}

	// Set the timeout for receiving data (3000ms).
	status = dc_iostream_set_timeout (device->iostream, 3000);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (context, "Failed to set the timeout.");
		goto error_free;
	}

	// TODO

	*out = (dc_device_t *) device;

	return DC_STATUS_SUCCESS;

error_free:
	dc_device_deallocate ((dc_device_t *) device);
	return status;
}

static dc_status_t
sherwood_logic_device_set_fingerprint (dc_device_t *abstract, const unsigned char data[], unsigned int size)
{
	sherwood_logic_device_t *device = (sherwood_logic_device_t *) abstract;

	if (size && size != sizeof (device->fingerprint))
		return DC_STATUS_INVALIDARGS;

	if (size)
		memcpy (device->fingerprint, data, sizeof (device->fingerprint));
	else
		memset (device->fingerprint, 0, sizeof (device->fingerprint));

	return DC_STATUS_SUCCESS;
}

static dc_status_t
sherwood_logic_device_foreach (dc_device_t *abstract, dc_dive_callback_t callback, void *userdata)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	sherwood_logic_device_t *device = (sherwood_logic_device_t *) abstract;

	// Read the model number.
	unsigned char model[6] = {0};
	status = sherwood_logic_read_model (device, model, sizeof(model));
	if (status != DC_STATUS_SUCCESS) {
		ERROR (abstract->context, "Failed to read the model number.");
		goto error_exit;
	}

	HEXDUMP(abstract->context, DC_LOGLEVEL_DEBUG, "Model", model, sizeof(model));
	DEBUG (abstract->context, "Model: %.*s", (int)sizeof(model), model);

	// Send the authentication handshake.
	status = sherwood_logic_auth (device);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (abstract->context, "Failed to send the authentication handshake.");
		goto error_exit;
	}

	// Read the serial number.
	unsigned char serial[32] = {0};
	status = sherwood_logic_read_serial (device, serial, sizeof(serial));
	if (status != DC_STATUS_SUCCESS) {
		ERROR (abstract->context, "Failed to read the serial number.");
		goto error_exit;
	}

	HEXDUMP(abstract->context, DC_LOGLEVEL_DEBUG, "Serial", serial, sizeof(serial));
	DEBUG (abstract->context, "Serial: %.*s", (int)sizeof(serial), serial);

	// Get the number of files.
	unsigned int count = 0;
	status = sherwood_logic_file_count (device, &count);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (abstract->context, "Failed to get the number of files.");
		goto error_exit;
	}
	DEBUG (abstract->context, "count=%u", count);

	sherwood_logic_file_t *files = malloc (count * sizeof(sherwood_logic_file_t));
	if (files == NULL) {
		status = DC_STATUS_NOMEMORY;
		goto error_exit;
	}

	unsigned int maxsize = 0;
	for (unsigned int i = 0; i < count; ++i) {
		// Get the file metadata.
		sherwood_logic_file_t file = {0};
		sherwood_logic_file_stat (device, i, &file);
		if (status != DC_STATUS_SUCCESS) {
			ERROR (abstract->context, "Failed to get the file metadata.");
			goto error_exit;
		}

		files[i] = file;

		DEBUG (abstract->context, "stat: idx=%u, name=%.*s, size=%u", i, (int)sizeof(files[i].name), files[i].name, files[i].size);

		if (maxsize < file.size) {
			maxsize = file.size;
		}
	}

	unsigned char *buffer = malloc (maxsize);
	if (buffer == NULL) {
		status = DC_STATUS_NOMEMORY;
		goto error_exit;
	}

	for (unsigned int i = 0; i < count; ++i) {

		DEBUG (abstract->context, "stat: idx=%u, name=%.*s, size=%u", i, (int)sizeof(files[i].name), files[i].name, files[i].size);

		status = sherwood_logic_file_open (device, i);
		if (status != DC_STATUS_SUCCESS) {
			ERROR (abstract->context, "Failed to open the file.");
			//goto error_exit;
		}

		unsigned int offset = 0;
		while (offset < files[i].size) {
			unsigned int len = files[i].size - offset;
			if (len > BLOCKSIZE) {
				len = BLOCKSIZE;
			}

			status = sherwood_logic_file_read (device, i, offset, buffer + offset, len);
			if (status != DC_STATUS_SUCCESS) {
				ERROR (abstract->context, "Failed to read the file block.");
				goto error_exit;
			}

			offset += len;
		}

		status = sherwood_logic_file_close (device, i);
		if (status != DC_STATUS_SUCCESS) {
			ERROR (abstract->context, "Failed to close the file.");
			goto error_exit;
		}

		if (callback && !callback (buffer, files[i].size, NULL, 0, userdata)) {
			break;
		}
	}

	free (buffer);
	free (files);

error_exit:
	return status;
}
