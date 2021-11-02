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

#include <stdlib.h> // malloc, free

#include "ble-win32.h"

#include "common-private.h"
#include "context-private.h"
#include "array.h"

#ifdef HAVE_BLUETOOTHLEAPIS_H

DEFINE_DEVPROPKEY(DEVPKEY_Bluetooth_DeviceAddress, 0x2bd67d8b, 0x8beb, 0x48d5, 0x87, 0xe0, 0x6c, 0xda, 0x34, 0x28, 0x04, 0x0a, 1);

DEFINE_DEVPROPKEY(DEVPKEY_Device_FriendlyName, 0xa45c254e, 0xdf1c, 0x4efd, 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0, 14);

static char *
win32_ble_wstr2str (dc_context_t *context, const wchar_t *wstr)
{
	char *str = NULL;
	int nbytes = 0;

	nbytes = WideCharToMultiByte (CP_ACP, 0, wstr, -1, NULL, 0, NULL, NULL);
	if (nbytes <= 0) {
		DWORD errcode = GetLastError ();
		SYSERROR (context, errcode);
		goto error_exit;
	}

	str = malloc (nbytes);
	if (str == NULL) {
		ERROR (context, "Out of memory");
		goto error_exit;
	}

	nbytes = WideCharToMultiByte (CP_ACP, 0, wstr, -1, str, nbytes, NULL, NULL);
	if (nbytes <= 0) {
		DWORD errcode = GetLastError ();
		SYSERROR (context, errcode);
		goto error_free;
	}

	return str;

error_free:
	free (str);
error_exit:
	return NULL;
}

static char *
win32_ble_get_property (dc_context_t *context, HDEVINFO hDI, SP_DEVINFO_DATA *pdid, const DEVPROPKEY *property)
{
	char *str = NULL;
	wchar_t *value = NULL;

	DWORD length = 0;
	DEVPROPTYPE type = 0;
	while (!SetupDiGetDevicePropertyW (hDI, pdid, property, &type, (BYTE *) value, length, &length, 0)) {
		DWORD errcode = GetLastError ();
		if (errcode != ERROR_INSUFFICIENT_BUFFER) {
			SYSERROR (context, errcode);
			goto error_free;
		}

		value = malloc (length);
		if (value == NULL) {
			ERROR (context, "Out of memory");
			goto error_exit;
		}
	}

	if (type != DEVPROP_TYPE_STRING) {
		ERROR (context, "Unexpected property type (%lu)", type);
		goto error_free;
	}

	str = win32_ble_wstr2str (context, value);
	if (str == NULL) {
		goto error_free;
	}

	free (value);

	return str;

error_free:
	free (value);
error_exit:
	return NULL;
}

char *
win32_ble_get_name (dc_context_t *context, HDEVINFO hDI, SP_DEVINFO_DATA *pdid)
{
	return win32_ble_get_property (context, hDI, pdid, &DEVPKEY_Device_FriendlyName);
}

dc_ble_address_t
win32_ble_get_address (dc_context_t *context, HDEVINFO hDI, SP_DEVINFO_DATA *pdid)
{
	char *str = win32_ble_get_property (context, hDI, pdid, &DEVPKEY_Bluetooth_DeviceAddress);
	dc_ble_address_t address = dc_ble_str2addr (str);
	free (str);

	return address;
}

dc_status_t
win32_ble_get_services(dc_context_t *context, HANDLE hFile, BTH_LE_GATT_SERVICE **out_services, size_t *out_nservices)
{
	dc_status_t status = DC_STATUS_SUCCESS;

	BTH_LE_GATT_SERVICE *services = NULL;
	USHORT nservices = 0;
	HRESULT hr = S_OK;

	hr = BluetoothGATTGetServices(
		hFile,
		0,
		NULL,
		&nservices,
		BLUETOOTH_GATT_FLAG_NONE);
	if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
		SYSERROR (context, hr);
		status = DC_STATUS_IO;
		goto error_exit;
	}

	services = (BTH_LE_GATT_SERVICE *) malloc(nservices * sizeof(BTH_LE_GATT_SERVICE));
	if (services == NULL) {
		status = DC_STATUS_NOMEMORY;
		goto error_exit;
	}

	memset(services, 0, nservices * sizeof(BTH_LE_GATT_SERVICE));

	hr = BluetoothGATTGetServices(
		hFile,
		nservices,
		services,
		&nservices,
		BLUETOOTH_GATT_FLAG_NONE);
	if (hr != S_OK) {
		SYSERROR (context, hr);
		status = DC_STATUS_IO;
		goto error_free;
	}

	*out_services = services;
	*out_nservices = nservices;

	return DC_STATUS_SUCCESS;

error_free:
	free(services);
error_exit:
	return status;
}

dc_status_t
win32_ble_get_characteristics(dc_context_t *context, HANDLE hFile, BTH_LE_GATT_SERVICE *service, BTH_LE_GATT_CHARACTERISTIC **out_characteristics, size_t *out_ncharacteristics)
{
	dc_status_t status = DC_STATUS_SUCCESS;

	BTH_LE_GATT_CHARACTERISTIC *characteristics = NULL;
	USHORT ncharacteristics = 0;
	HRESULT hr = S_OK;

	hr = BluetoothGATTGetCharacteristics(
		hFile,
		service,
		0,
		NULL,
		&ncharacteristics,
		BLUETOOTH_GATT_FLAG_NONE);
	if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
		if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
			goto out;
		SYSERROR (context, hr);
		status = DC_STATUS_IO;
		goto error_exit;
	}

	if (ncharacteristics > 0) {
		characteristics = (BTH_LE_GATT_CHARACTERISTIC *) malloc(ncharacteristics * sizeof(BTH_LE_GATT_CHARACTERISTIC));
		if (characteristics == NULL) {
			status = DC_STATUS_NOMEMORY;
			goto error_exit;
		}

		memset(characteristics, 0, ncharacteristics * sizeof(BTH_LE_GATT_CHARACTERISTIC));

		hr = BluetoothGATTGetCharacteristics(
			hFile,
			service,
			ncharacteristics,
			characteristics,
			&ncharacteristics,
			BLUETOOTH_GATT_FLAG_NONE);
		if (hr != S_OK) {
			SYSERROR (context, hr);
			status = DC_STATUS_IO;
			goto error_free;
		}
	}

out:
	*out_characteristics = characteristics;
	*out_ncharacteristics = ncharacteristics;

	return DC_STATUS_SUCCESS;

error_free:
	free(characteristics);
error_exit:
	return status;
}

dc_status_t
win32_ble_get_descriptors(dc_context_t *context, HANDLE hFile, BTH_LE_GATT_CHARACTERISTIC *characteristic, BTH_LE_GATT_DESCRIPTOR **out_descriptors, size_t *out_ndescriptors)
{
	dc_status_t status = DC_STATUS_SUCCESS;

	BTH_LE_GATT_DESCRIPTOR *descriptors = NULL;
	USHORT ndescriptors = 0;
	HRESULT hr = S_OK;

	hr = BluetoothGATTGetDescriptors(
		hFile,
		characteristic,
		0,
		NULL,
		&ndescriptors,
		BLUETOOTH_GATT_FLAG_NONE);
	if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
		if (hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
			goto out;
		SYSERROR (context, hr);
		status = DC_STATUS_IO;
		goto error_exit;
	}

	if (ndescriptors > 0) {
		descriptors = (BTH_LE_GATT_DESCRIPTOR *)malloc(ndescriptors * sizeof(BTH_LE_GATT_DESCRIPTOR));
		if (descriptors == NULL) {
			status = DC_STATUS_NOMEMORY;
			goto error_exit;
		}

		memset(descriptors, 0, ndescriptors * sizeof(BTH_LE_GATT_DESCRIPTOR));

		hr = BluetoothGATTGetDescriptors(
			hFile,
			characteristic,
			ndescriptors,
			descriptors,
			&ndescriptors,
			BLUETOOTH_GATT_FLAG_NONE);
		if (hr != S_OK) {
			SYSERROR (context, hr);
			status = DC_STATUS_IO;
			goto error_free;
		}
	}

out:
	*out_descriptors = descriptors;
	*out_ndescriptors = ndescriptors;

	return DC_STATUS_SUCCESS;

error_free:
	free(descriptors);
error_exit:
	return status;
}

GUID
win32_ble_uuid2guid(BTH_LE_UUID uuid)
{
	GUID guid = {0};
	if (uuid.IsShortUuid) {
		guid = BTH_LE_ATT_BLUETOOTH_BASE_GUID;
		guid.Data1 += uuid.Value.ShortUuid;
	} else {
		guid = uuid.Value.LongUuid;
	}

	return guid;
}

char *
win32_ble_uuid2str(BTH_LE_UUID uuid, char *str, size_t size)
{
	if (str == NULL)
		return NULL;

	GUID guid = win32_ble_uuid2guid (uuid);

	int n = dc_platform_snprintf (str, size,
		"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
	if (n < 0 || (size_t) n >= size)
		return NULL;

	return str;
}

dc_ble_uuid_t *
win32_ble_uuid2uuid(BTH_LE_UUID uuid, dc_ble_uuid_t *result)
{
	GUID guid = win32_ble_uuid2guid (uuid);

	if (result) {
		array_uint32_be_set (*result + 0, guid.Data1);
		array_uint16_be_set (*result + 4, guid.Data2);
		array_uint16_be_set (*result + 6, guid.Data3);
		memcpy (*result + 8, guid.Data4, sizeof(guid.Data4));
	}

	return result;
}

dc_status_t
win32_ble_open (HANDLE *out, dc_context_t *context, GUID guid, dc_ble_address_t address)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	SP_DEVICE_INTERFACE_DETAIL_DATA *pdidd = NULL;

	HDEVINFO hDI = SetupDiGetClassDevs (&guid, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (hDI == INVALID_HANDLE_VALUE) {
		DWORD errcode = GetLastError ();
		SYSERROR (context, errcode);
		status = DC_STATUS_IO;
		goto error_exit;
	}

	DWORD i = 0;
	while (1) {
		SP_DEVICE_INTERFACE_DATA did = {0};
		did.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

		if (!SetupDiEnumDeviceInterfaces (hDI, NULL, &guid, i++, &did)) {
			DWORD errcode = GetLastError ();
			if (errcode == ERROR_NO_MORE_ITEMS)
				break;
			SYSERROR (context, errcode);
			status = DC_STATUS_IO;
			goto error_free_list;
		}

		SP_DEVINFO_DATA dd = {0};
		dd.cbSize = sizeof(SP_DEVINFO_DATA);

		DWORD size = 0;
		while (!SetupDiGetDeviceInterfaceDetail (hDI, &did, pdidd, size, &size, &dd)) {
			DWORD errcode = GetLastError ();
			if (errcode != ERROR_INSUFFICIENT_BUFFER) {
				SYSERROR (context, errcode);
				status = DC_STATUS_IO;
				goto error_free_path;
			}

			pdidd = malloc (size);
			if (pdidd == NULL) {
				status = DC_STATUS_NOMEMORY;
				goto error_free_list;
			}

			pdidd->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
		}

		dc_ble_address_t addr = win32_ble_get_address (context, hDI, &dd);

		if (addr == address) {
			break;
		}

		free (pdidd);
		pdidd = NULL;
	}

	if (pdidd == NULL) {
		ERROR (context, "No bluetooth low energy device found.");
		status = DC_STATUS_NODEVICE;
		goto error_free_list;
	}

	hFile = CreateFile (pdidd->DevicePath,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, // No security attributes.
			OPEN_EXISTING,
			0,
			NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		DWORD errcode = GetLastError ();
		SYSERROR (context, errcode);
		status = DC_STATUS_IO;
		goto error_free_path;
	}

	*out = hFile;

error_free_path:
	free (pdidd);
error_free_list:
	SetupDiDestroyDeviceInfoList (hDI);
error_exit:
	return status;
}

dc_status_t
win32_ble_characteristic_read (dc_context_t *context, HANDLE hFile, BTH_LE_GATT_CHARACTERISTIC *characteristic, void *data, size_t size, size_t *actual)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	BTH_LE_GATT_CHARACTERISTIC_VALUE *value = NULL;
	HRESULT hr = S_OK;
	size_t nbytes = 0;

	if (characteristic == NULL ||
		(!characteristic->IsReadable)) {
		ERROR (context, "Characteristic does not support reading.");
		status = DC_STATUS_INVALIDARGS;
		goto error_exit;
	}

	ULONG flags = BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_DEVICE;

	USHORT length = 0;
	hr = BluetoothGATTGetCharacteristicValue (hFile, characteristic, 0, NULL, &length, flags);
	if (hr != HRESULT_FROM_WIN32(ERROR_MORE_DATA)) {
		SYSERROR (context, hr);
		status = DC_STATUS_IO;
		goto error_exit;
	}

	value = (BTH_LE_GATT_CHARACTERISTIC_VALUE *) malloc (length);
	if (value == NULL) {
		ERROR (context, "Out of memory");
		status = DC_STATUS_NOMEMORY;
		goto error_exit;
	}

	memset (value, 0, length);

	hr = BluetoothGATTGetCharacteristicValue (hFile, characteristic, length, value, NULL, flags);
	if (hr != S_OK) {
		SYSERROR (context, hr);
		status = DC_STATUS_IO;
		goto error_free;
	}

	nbytes = value->DataSize;

	if (nbytes > size) {
		ERROR (context, "Value too large.");
		status = DC_STATUS_IO;
		nbytes = size;
	}

	memcpy (data, value->Data, nbytes);

error_free:
	free (value);
error_exit:
	if (actual) {
		*actual = nbytes;
	}

	return status;
}

dc_status_t
win32_ble_characteristic_write (dc_context_t *context, HANDLE hFile, BTH_LE_GATT_CHARACTERISTIC *characteristic, const void *data, size_t size)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	BTH_LE_GATT_CHARACTERISTIC_VALUE *value = NULL;
	HRESULT hr = S_OK;

	if (characteristic == NULL ||
		(!characteristic->IsWritable && !characteristic->IsWritableWithoutResponse)) {
		ERROR (context, "Characteristic does not support writing.");
		status = DC_STATUS_INVALIDARGS;
		goto error_exit;
	}

	value = malloc (sizeof(BTH_LE_GATT_CHARACTERISTIC_VALUE) + size);
	if (value == NULL) {
		ERROR (context, "Out of memory");
		status = DC_STATUS_NOMEMORY;
		goto error_exit;
	}

	value->DataSize = size;
	if (size) {
		memcpy (value->Data, data, size);
	}

	ULONG flags = characteristic->IsWritableWithoutResponse ?
		BLUETOOTH_GATT_FLAG_WRITE_WITHOUT_RESPONSE :
		BLUETOOTH_GATT_FLAG_NONE;

	hr = BluetoothGATTSetCharacteristicValue (hFile, characteristic, value, 0, flags);
	if (FAILED(hr)) {
		SYSERROR (context, hr);
		status = DC_STATUS_IO;
		goto error_free;
	}

error_free:
	free (value);
error_exit:
	return status;
}

dc_status_t
win32_ble_characteristic_notify (HANDLE *out, dc_context_t *context, HANDLE hFile, BTH_LE_GATT_CHARACTERISTIC *characteristic, PFNBLUETOOTH_GATT_EVENT_CALLBACK callback, void *userdata)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	BLUETOOTH_GATT_EVENT_HANDLE hEvent = INVALID_HANDLE_VALUE;
	HRESULT hr = S_OK;

	if (characteristic == NULL ||
		(!characteristic->IsNotifiable && !characteristic->IsIndicatable)) {
		ERROR (context, "Characteristic does not support notifications.");
		status = DC_STATUS_INVALIDARGS;
		goto error_exit;
	}

	// Get the BLE descriptors.
	size_t ndescriptors = 0;
	BTH_LE_GATT_DESCRIPTOR *descriptors = NULL;
	status = win32_ble_get_descriptors (context, hFile, characteristic, &descriptors, &ndescriptors);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (context, "Failed to get the descriptors.");
		goto error_exit;
	}

	// Find the client characteristic configuration descriptor.
	BTH_LE_GATT_DESCRIPTOR *config = NULL;
	for (size_t k = 0; k < ndescriptors; k++) {
		if (descriptors[k].DescriptorType == ClientCharacteristicConfiguration) {
			config = descriptors + k;
			break;
		}
	}

	if (config == NULL) {
		ERROR (context, "Client configuration descriptor not found.");
		status = DC_STATUS_IO;
		goto error_free;
	}

	BTH_LE_GATT_DESCRIPTOR_VALUE value;
	memset (&value, 0, sizeof(value));
	value.DescriptorType = ClientCharacteristicConfiguration;
	value.DescriptorUuid = config->DescriptorUuid;
	value.ClientCharacteristicConfiguration.IsSubscribeToNotification = characteristic->IsNotifiable;
	value.ClientCharacteristicConfiguration.IsSubscribeToIndication = characteristic->IsIndicatable;
	value.DataSize = 0;

	hr = BluetoothGATTSetDescriptorValue (hFile, config, &value, BLUETOOTH_GATT_FLAG_NONE);
	if (FAILED(hr)) {
		SYSERROR (context, hr);
		status = DC_STATUS_IO;
		goto error_free;
	}

	BLUETOOTH_GATT_VALUE_CHANGED_EVENT_REGISTRATION params;
	params.Characteristics[0] = *characteristic;
	params.NumCharacteristics = 1;
	hr = BluetoothGATTRegisterEvent (hFile, CharacteristicValueChangedEvent, &params, callback, userdata, &hEvent, BLUETOOTH_GATT_FLAG_NONE);
	if (FAILED(hr)) {
		SYSERROR (context, hr);
		status = DC_STATUS_IO;
		goto error_free;
	}

	*out = hEvent;

error_free:
	free (descriptors);
error_exit:
	return status;
}

#endif /* HAVE_BLUETOOTHLEAPIS_H */
