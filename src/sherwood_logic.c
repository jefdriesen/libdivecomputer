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

	// TODO

	return status;
}
