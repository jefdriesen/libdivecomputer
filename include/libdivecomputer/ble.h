/*
 * libdivecomputer
 *
 * Copyright (C) 2019 Jef Driesen
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

#ifndef DC_BLE_H
#define DC_BLE_H

#include "common.h"
#include "context.h"
#include "iostream.h"
#include "iterator.h"
#include "descriptor.h"
#include "ioctl.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * Get the remote device name.
 */
#define DC_IOCTL_BLE_GET_NAME   DC_IOCTL_IOR('b', 0, DC_IOCTL_SIZE_VARIABLE)

/**
 * Get the bluetooth authentication PIN code.
 *
 * The data format is a NULL terminated string.
 */
#define DC_IOCTL_BLE_GET_PINCODE   DC_IOCTL_IOR('b', 1, DC_IOCTL_SIZE_VARIABLE)

/**
 * Get/set the bluetooth authentication access code.
 *
 * The data format is a variable sized byte array.
 */
#define DC_IOCTL_BLE_GET_ACCESSCODE   DC_IOCTL_IOR('b', 2, DC_IOCTL_SIZE_VARIABLE)
#define DC_IOCTL_BLE_SET_ACCESSCODE   DC_IOCTL_IOW('b', 2, DC_IOCTL_SIZE_VARIABLE)

/**
 * Perform a BLE characteristic read/write operation.
 *
 * The UUID of the characteristic must be specified as a #dc_ble_uuid_t
 * data structure. If the operation requires additional data as in- or
 * output, the buffer must be located immediately after the
 * #dc_ble_uuid_t data structure. The size of the ioctl request is the
 * total size, including the size of the #dc_ble_uuid_t structure.
 */
#define DC_IOCTL_BLE_CHARACTERISTIC_READ  DC_IOCTL_IOR('b', 3, DC_IOCTL_SIZE_VARIABLE)
#define DC_IOCTL_BLE_CHARACTERISTIC_WRITE DC_IOCTL_IOW('b', 3, DC_IOCTL_SIZE_VARIABLE)

/**
 * The minimum number of bytes (including the terminating null byte) for
 * formatting a bluetooth address as a string.
 */
#define DC_BLE_ADDRESS_SIZE 18

/**
 * The minimum number of bytes (including the terminating null byte) for
 * formatting a bluetooth UUID as a string.
 */
#define DC_BLE_UUID_SIZE 37

/**
 * Bluetooth address (48 bits).
 */
#if defined (_WIN32) && !defined (__GNUC__)
typedef unsigned __int64 dc_ble_address_t;
#else
typedef unsigned long long dc_ble_address_t;
#endif

/**
 * Bluetooth UUID (128 bits).
 */
typedef unsigned char dc_ble_uuid_t[16];

/**
 * Opaque object representing a bluetooth device.
 */
typedef struct dc_ble_device_t dc_ble_device_t;

typedef struct dc_ble_auth_cbs_t {
	dc_status_t (*get_pincode) (dc_iostream_t *iostream, unsigned char data[], size_t size, void *userdata);
	dc_status_t (*get_accesscode) (dc_iostream_t *iostream, unsigned char data[], size_t size, void *userdata);
	dc_status_t (*set_accesscode) (dc_iostream_t *iostream, unsigned char data[], size_t size, void *userdata);
} dc_ble_auth_cbs_t;

/**
 * Convert a bluetooth address to a string.
 *
 * The bluetooth address is formatted as XX:XX:XX:XX:XX:XX, where each
 * XX is a hexadecimal number specifying an octet of the 48-bit address.
 * The minimum size for the buffer is #DC_BLE_ADDRESS_SIZE bytes.
 *
 * @param[in]  address  A bluetooth address.
 * @param[in]  str      The memory buffer to store the result.
 * @param[in]  size     The size of the memory buffer.
 * @returns The null-terminated string on success, or NULL on failure.
 */
char *
dc_ble_addr2str(dc_ble_address_t address, char *str, size_t size);

/**
 * Convert a string to a bluetooth address.
 *
 * The string is expected to be in the format XX:XX:XX:XX:XX:XX, where
 * each XX is a hexadecimal number specifying an octet of the 48-bit
 * address.
 *
 * @param[in]  address  A null-terminated string.
 * @returns The bluetooth address on success, or zero on failure.
 */
dc_ble_address_t
dc_ble_str2addr(const char *address);

/**
 * Convert a bluetooth UUID to a string.
 *
 * The bluetooth UUID is formatted as
 * XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX, where each XX pair is a
 * hexadecimal number specifying an octet of the UUID.
 * The minimum size for the buffer is #DC_BLE_UUID_SIZE bytes.
 *
 * @param[in]  uuid     A bluetooth UUID.
 * @param[in]  str      The memory buffer to store the result.
 * @param[in]  size     The size of the memory buffer.
 * @returns The null-terminated string on success, or NULL on failure.
 */
char *
dc_ble_uuid2str (const dc_ble_uuid_t uuid, char *str, size_t size);

/**
 * Convert a string to a bluetooth UUID.
 *
 * The string is expected to be in the format
 * XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX, where each XX pair is a
 * hexadecimal number specifying an octet of the UUID.
 *
 * @param[in]  str      A null-terminated string.
 * @param[in]  uuid     The memory buffer to store the result.
 * @returns Non-zero on success, or zero on failure.
 */
int
dc_ble_str2uuid (const char *str, dc_ble_uuid_t uuid);

/**
 * Get the address of the bluetooth device.
 *
 * @param[in]  device  A valid bluetooth device.
 */
dc_ble_address_t
dc_ble_device_get_address (dc_ble_device_t *device);

/**
 * Get the name of the bluetooth device.
 *
 * @param[in]  device  A valid bluetooth device.
 */
const char *
dc_ble_device_get_name (dc_ble_device_t *device);

/**
 * Destroy the bluetooth device and free all resources.
 *
 * @param[in]  device  A valid bluetooth device.
 */
void
dc_ble_device_free (dc_ble_device_t *device);

/**
 * Create an iterator to enumerate the bluetooth devices.
 *
 * @param[out] iterator    A location to store the iterator.
 * @param[in]  context     A valid context object.
 * @param[in]  descriptor  A valid device descriptor or NULL.
 * @returns #DC_STATUS_SUCCESS on success, or another #dc_status_t code
 * on failure.
 */
dc_status_t
dc_ble_iterator_new (dc_iterator_t **iterator, dc_context_t *context, dc_descriptor_t *descriptor);

/**
 * Open an bluetooth connection.
 *
 * @param[out]  iostream   A location to store the bluetooth connection.
 * @param[in]   context    A valid context object.
 * @param[in]   address    The bluetooth device address.
 * @returns #DC_STATUS_SUCCESS on success, or another #dc_status_t code
 * on failure.
 */
dc_status_t
dc_ble_open (dc_iostream_t **iostream, dc_context_t *context, dc_ble_address_t address);

dc_status_t
dc_ble_set_auth (dc_iostream_t *iostream, const dc_ble_auth_cbs_t *callbacks, void *userdata);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* DC_BLE_H */
