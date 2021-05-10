/*
 * libdivecomputer
 *
 * Copyright (C) 2024 Jef Driesen
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h> // malloc, free
#include <stdio.h>
#include <string.h>

#include <libdivecomputer/ble.h>

#include "common-private.h"
#include "context-private.h"
#include "iostream-private.h"
#include "iterator-private.h"
#include "platform.h"

#ifdef _WIN32
#define DC_ADDRESS_FORMAT "%012I64X"
#else
#define DC_ADDRESS_FORMAT "%012llX"
#endif

#define C_ARRAY_SIZE(array) (sizeof (array) / sizeof *(array))

#define ISINSTANCE(device) dc_iostream_isinstance((device), &dc_ble_vtable)

struct dc_ble_device_t {
	dc_ble_address_t address;
	char name[248];
};

#ifdef BLE
static dc_status_t dc_ble_iterator_next (dc_iterator_t *iterator, void *item);
static dc_status_t dc_ble_iterator_free (dc_iterator_t *iterator);

static dc_status_t dc_ble_set_timeout (dc_iostream_t *iostream, int timeout);
static dc_status_t dc_ble_get_available (dc_iostream_t *iostream, size_t *value);
static dc_status_t dc_ble_poll (dc_iostream_t *iostream, int timeout);
static dc_status_t dc_ble_read (dc_iostream_t *iostream, void *data, size_t size, size_t *actual);
static dc_status_t dc_ble_write (dc_iostream_t *iostream, const void *data, size_t size, size_t *actual);
static dc_status_t dc_ble_ioctl (dc_iostream_t *iostream, unsigned int request, void *data, size_t size);
static dc_status_t dc_ble_purge (dc_iostream_t *iostream, dc_direction_t direction);
static dc_status_t dc_ble_sleep (dc_iostream_t *iostream, unsigned int milliseconds);
static dc_status_t dc_ble_close (dc_iostream_t *iostream);

typedef struct dc_ble_iterator_t {
	dc_iterator_t base;
	dc_descriptor_t *descriptor;
} dc_ble_iterator_t;

typedef struct dc_ble_t {
	dc_iostream_t base;
	char name[248];
	int timeout;
} dc_ble_t;

static const dc_iterator_vtable_t dc_ble_iterator_vtable = {
	sizeof(dc_ble_iterator_t),
	dc_ble_iterator_next,
	dc_ble_iterator_free,
};

static const dc_iostream_vtable_t dc_ble_vtable = {
	sizeof(dc_ble_t),
	dc_ble_set_timeout, /* set_timeout */
	NULL, /* set_break */
	NULL, /* set_dtr */
	NULL, /* set_rts */
	NULL, /* get_lines */
	dc_ble_get_available, /* get_available */
	NULL, /* configure */
	dc_ble_poll, /* poll */
	dc_ble_read, /* read */
	dc_ble_write, /* write */
	dc_ble_ioctl, /* ioctl */
	NULL, /* flush */
	dc_ble_purge, /* purge */
	dc_ble_sleep, /* sleep */
	dc_ble_close, /* close */
};
#endif

char *
dc_ble_addr2str(dc_ble_address_t address, char *str, size_t size)
{
	if (str == NULL || size < DC_BLE_ADDRESS_SIZE)
		return NULL;

	int n = dc_platform_snprintf(str, size, "%02X:%02X:%02X:%02X:%02X:%02X",
		(unsigned char)((address >> 40) & 0xFF),
		(unsigned char)((address >> 32) & 0xFF),
		(unsigned char)((address >> 24) & 0xFF),
		(unsigned char)((address >> 16) & 0xFF),
		(unsigned char)((address >>  8) & 0xFF),
		(unsigned char)((address >>  0) & 0xFF));
	if (n < 0 || (size_t) n >= size)
		return NULL;

	return str;
}

dc_ble_address_t
dc_ble_str2addr(const char *str)
{
	dc_ble_address_t address = 0;

	if (str == NULL)
		return 0;

	unsigned char c = 0;
	while ((c = *str++) != '\0') {
		if (c == ':') {
			continue;
		} else if (c >= '0' && c <= '9') {
			c -= '0';
		} else if (c >= 'A' && c <= 'F') {
			c -= 'A' - 10;
		} else if (c >= 'a' && c <= 'f') {
			c -= 'a' - 10;
		} else {
			return 0; /* Invalid character! */
		}

		address <<= 4;
		address |= c;
	}

	return address;
}

char *
dc_ble_uuid2str (const dc_ble_uuid_t uuid, char *str, size_t size)
{
	if (str == NULL || size < DC_BLE_UUID_SIZE)
		return NULL;

	int n = dc_platform_snprintf(str, size,
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		uuid[0], uuid[1], uuid[2], uuid[3],
		uuid[4], uuid[5],
		uuid[6], uuid[7],
		uuid[8], uuid[9],
		uuid[10], uuid[11], uuid[12],
		uuid[13], uuid[14], uuid[15]);
	if (n < 0 || (size_t) n >= size)
		return NULL;

	return str;
}

int
dc_ble_str2uuid (const char *str, dc_ble_uuid_t uuid)
{
	dc_ble_uuid_t tmp = {0};

	if (str == NULL || uuid == NULL)
		return 0;

	unsigned int i = 0;
	unsigned char c = 0;
	while ((c = *str++) != '\0') {
		if (c == '-') {
			if (i != 8 && i != 12 && i != 16 && i != 20) {
				return 0; /* Invalid character! */
			}
			continue;
		} else if (c >= '0' && c <= '9') {
			c -= '0';
		} else if (c >= 'A' && c <= 'F') {
			c -= 'A' - 10;
		} else if (c >= 'a' && c <= 'f') {
			c -= 'a' - 10;
		} else {
			return 0; /* Invalid character! */
		}

		if ((i & 1) == 0) {
			c <<= 4;
		}

		if (i >= 2 * sizeof(tmp)) {
			return 0; /* Too many characters! */
		}

		tmp[i / 2] |= c;
		i++;
	}

	if (i != 2 * sizeof(tmp)) {
		return 0; /* Not enough characters! */
	}

	memcpy (uuid, tmp, sizeof(tmp));

	return 1;
}

dc_ble_address_t
dc_ble_device_get_address (dc_ble_device_t *device)
{
	if (device == NULL)
		return 0;

	return device->address;
}

const char *
dc_ble_device_get_name (dc_ble_device_t *device)
{
	if (device == NULL || device->name[0] == '\0')
		return NULL;

	return device->name;
}

void
dc_ble_device_free (dc_ble_device_t *device)
{
	free (device);
}

dc_status_t
dc_ble_iterator_new (dc_iterator_t **out, dc_context_t *context, dc_descriptor_t *descriptor)
{
#ifdef BLE
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_ble_iterator_t *iterator = NULL;

	if (out == NULL)
		return DC_STATUS_INVALIDARGS;

	iterator = (dc_ble_iterator_t *) dc_iterator_allocate (context, &dc_ble_iterator_vtable);
	if (iterator == NULL) {
		return DC_STATUS_NOMEMORY;
	}

	// TODO

	iterator->descriptor = descriptor;

	*out = (dc_iterator_t *) iterator;

	return DC_STATUS_SUCCESS;

error_free:
	dc_iterator_deallocate ((dc_iterator_t *) iterator);
	return status;
#else
	return DC_STATUS_UNSUPPORTED;
#endif
}

#ifdef BLE
static dc_status_t
dc_ble_iterator_next (dc_iterator_t *abstract, void *out)
{
	dc_ble_iterator_t *iterator = (dc_ble_iterator_t *) abstract;
	dc_ble_device_t *device = NULL;

	// TODO

	return DC_STATUS_DONE;
}

static dc_status_t
dc_ble_iterator_free (dc_iterator_t *abstract)
{
	dc_ble_iterator_t *iterator = (dc_ble_iterator_t *) abstract;

	// TODO

	return DC_STATUS_SUCCESS;
}
#endif

dc_status_t
dc_ble_open (dc_iostream_t **out, dc_context_t *context, dc_ble_address_t address)
{
#ifdef BLE
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_ble_t *device = NULL;

	if (out == NULL)
		return DC_STATUS_INVALIDARGS;

	INFO (context, "Open: address=" DC_ADDRESS_FORMAT, address);

	// Allocate memory.
	device = (dc_ble_t *) dc_iostream_allocate (context, &dc_ble_vtable, DC_TRANSPORT_BLE);
	if (device == NULL) {
		return DC_STATUS_NOMEMORY;
	}

	memset (device->name, 0, sizeof(device->name));
	device->timeout = -1;

	// TODO

	*out = (dc_iostream_t *) device;

	return DC_STATUS_SUCCESS;

error_free:
	dc_iostream_deallocate ((dc_iostream_t *) device);
	return status;
#else
	return DC_STATUS_UNSUPPORTED;
#endif
}

#ifdef BLE
static dc_status_t
dc_ble_close (dc_iostream_t *abstract)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_ble_t *device = (dc_ble_t *) abstract;

	// TODO

	return status;
}

static dc_status_t
dc_ble_set_timeout (dc_iostream_t *abstract, int timeout)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_ble_t *device = (dc_ble_t *) abstract;

	device->timeout = timeout;

	return status;
}

static dc_status_t
dc_ble_poll (dc_iostream_t *abstract, int timeout)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_ble_t *device = (dc_ble_t *) abstract;

	// TODO

	return status;
}

static dc_status_t
dc_ble_read (dc_iostream_t *abstract, void *data, size_t size, size_t *actual)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_ble_t *device = (dc_ble_t *) abstract;
	size_t nbytes = 0;

	// TODO

out:
	if (actual)
		*actual = nbytes;

	return status;
}

static dc_status_t
dc_ble_write (dc_iostream_t *abstract, const void *data, size_t size, size_t *actual)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_ble_t *device = (dc_ble_t *) abstract;
	size_t nbytes = 0;

	// TODO

out:
	if (actual)
		*actual = nbytes;

	return status;
}

static dc_status_t
dc_ble_ioctl (dc_iostream_t *abstract, unsigned int request, void *data, size_t size)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_ble_t *device = (dc_ble_t *) abstract;

	switch (request) {
	case DC_IOCTL_BLE_GET_NAME:
		strncpy(data, device->name, size);
		return DC_STATUS_SUCCESS;
	default:
		return DC_STATUS_UNSUPPORTED;
	}

	return status;
}

static dc_status_t
dc_ble_purge (dc_iostream_t *abstract, dc_direction_t direction)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_ble_t *device = (dc_ble_t *) abstract;

	// TODO

	return status;
}

static dc_status_t
dc_ble_get_available (dc_iostream_t *abstract, size_t *value)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	dc_ble_t *device = (dc_ble_t *) abstract;
	size_t nbytes = 0;

	// TODO

	if (value)
		*value = nbytes;

	return status;
}

static dc_status_t
dc_ble_sleep (dc_iostream_t *abstract, unsigned int timeout)
{
	if (dc_platform_sleep (timeout) != 0) {
		return DC_STATUS_IO;
	}

	return DC_STATUS_SUCCESS;
}
#endif
