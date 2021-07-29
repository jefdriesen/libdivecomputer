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

#ifndef DC_BLE_BLUEZ_H
#define DC_BLE_BLUEZ_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_GLIB
#define BLE
#endif

#include <libdivecomputer/common.h>
#include <libdivecomputer/context.h>
#include <libdivecomputer/ble.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GATT_PROP_BROADCAST              0x01
#define GATT_PROP_READ                   0x02
#define GATT_PROP_WRITE_WITHOUT_RESPONSE 0x04
#define GATT_PROP_WRITE                  0x08
#define GATT_PROP_NOTIFY                 0x10
#define GATT_PROP_INDICATE               0x20
#define GATT_PROP_AUTHEN                 0x40
#define GATT_PROP_EXTENDED               0x80

typedef struct bluez_ble_device_t {
	dc_ble_address_t address;
	char name[248];
} bluez_ble_device_t;

typedef struct bluez_ble_service_t {
	dc_ble_uuid_t uuid;
	unsigned int handle;
} bluez_ble_service_t;

typedef struct bluez_ble_characteristic_t {
	dc_ble_uuid_t uuid;
	unsigned int handle;
	unsigned int flags;
	unsigned int mtu;
} bluez_ble_characteristic_t;

typedef struct bluez_ble_descriptor_t {
	dc_ble_uuid_t uuid;
	unsigned int handle;
} bluez_ble_descriptor_t;

typedef struct bluez_ble_t bluez_ble_t;

typedef void (*bluez_notify_cb_t) (bluez_ble_t *bluez, const bluez_ble_characteristic_t *characteristic, const unsigned char data[], size_t size, void *userdata);

dc_status_t
bluez_ble_new (bluez_ble_t **out, dc_context_t *context);

void
bluez_ble_free (bluez_ble_t *bluez);

dc_status_t
bluez_ble_scan (bluez_ble_t *bluez, int timeout, bluez_ble_device_t **out_devices, size_t *out_ndevices);

dc_status_t
bluez_ble_connect (bluez_ble_t *bluez, dc_ble_address_t address);

dc_status_t
bluez_ble_disconnect (bluez_ble_t *bluez);

dc_status_t
bluez_ble_get_services (bluez_ble_t *bluez, bluez_ble_service_t **out_services, size_t *out_nservices);

dc_status_t
bluez_ble_get_characteristics (bluez_ble_t *bluez, const bluez_ble_service_t *service, bluez_ble_characteristic_t **out_characteristics, size_t *out_ncharacteristics);

dc_status_t
bluez_ble_get_descriptors (bluez_ble_t *bluez, const bluez_ble_characteristic_t *characteristic, bluez_ble_descriptor_t **out_descriptors, size_t *out_ndescriptors);

dc_status_t
bluez_ble_characteristic_read (bluez_ble_t *bluez, const bluez_ble_characteristic_t *characteristic, void *data, size_t size, size_t *actual);

dc_status_t
bluez_ble_characteristic_write (bluez_ble_t *bluez, const bluez_ble_characteristic_t *characteristic, const void *data, size_t size);

dc_status_t
bluez_ble_characteristic_notify (bluez_ble_t *bluez, const bluez_ble_characteristic_t *characteristic, bluez_notify_cb_t callback, void *userdata);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* DC_BLE_BLUEZ_H */
