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

#ifndef DC_BLE_WIN32_H
#define DC_BLE_WIN32_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define INITGUID
#define _WIN32_WINNT _WIN32_WINNT_WIN8
#include <windows.h>
#include <setupapi.h>
#ifdef HAVE_BLUETOOTHLEAPIS_H
#define BLE
#include <bthledef.h>
#include <bluetoothleapis.h>
#endif
#endif

#include <libdivecomputer/common.h>
#include <libdivecomputer/context.h>
#include <libdivecomputer/ble.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef HAVE_BLUETOOTHLEAPIS_H

char *
win32_ble_get_name (dc_context_t *context, HDEVINFO hDI, SP_DEVINFO_DATA *pdid);

dc_ble_address_t
win32_ble_get_address (dc_context_t *context, HDEVINFO hDI, SP_DEVINFO_DATA *pdid);

GUID
win32_ble_uuid2guid(BTH_LE_UUID uuid);

char *
win32_ble_uuid2str(BTH_LE_UUID uuid, char *str, size_t size);

dc_ble_uuid_t *
win32_ble_uuid2uuid(BTH_LE_UUID uuid, dc_ble_uuid_t *result);

dc_status_t
win32_ble_open (HANDLE *out, dc_context_t *context, GUID guid, dc_ble_address_t address);

dc_status_t
win32_ble_get_services(dc_context_t *context, HANDLE hFile, BTH_LE_GATT_SERVICE **out_services, size_t *out_nservices);

dc_status_t
win32_ble_get_characteristics(dc_context_t *context, HANDLE hFile, BTH_LE_GATT_SERVICE *service, BTH_LE_GATT_CHARACTERISTIC **out_characteristics, size_t *out_ncharacteristics);

dc_status_t
win32_ble_get_descriptors(dc_context_t *context, HANDLE hFile, BTH_LE_GATT_CHARACTERISTIC *characteristic, BTH_LE_GATT_DESCRIPTOR **out_descriptors, size_t *out_ndescriptors);

dc_status_t
win32_ble_characteristic_read (dc_context_t *context, HANDLE hFile, BTH_LE_GATT_CHARACTERISTIC *characteristic, void *data, size_t size, size_t *actual);

dc_status_t
win32_ble_characteristic_write (dc_context_t *context, HANDLE hFile, BTH_LE_GATT_CHARACTERISTIC *characteristic, const void *data, size_t size);

dc_status_t
win32_ble_characteristic_notify (HANDLE *out, dc_context_t *context, HANDLE hFile, BTH_LE_GATT_CHARACTERISTIC *characteristic, PFNBLUETOOTH_GATT_EVENT_CALLBACK callback, void *userdata);

#endif /* HAVE_BLUETOOTHLEAPIS_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* DC_BLE_WIN32_H */
