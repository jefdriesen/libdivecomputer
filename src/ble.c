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

#ifdef _WIN32
#include "ble-win32.h"
#endif

#include <libdivecomputer/ble.h>
#include <libdivecomputer/buffer.h>

#include "common-private.h"
#include "context-private.h"
#include "iostream-private.h"
#include "iterator-private.h"
#include "platform.h"
#include "queue.h"

#ifdef _WIN32
#define DC_ADDRESS_FORMAT "%012I64X"
#else
#define DC_ADDRESS_FORMAT "%012llX"
#endif

#define C_ARRAY_SIZE(array) (sizeof (array) / sizeof *(array))

#define ISINSTANCE(device) dc_iostream_isinstance((device), &dc_ble_vtable)

#define UART_CREDITS_MIN        16
#define UART_CREDITS_MAX        32
#define UART_CREDITS_DISCONNECT 255

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
#ifdef _WIN32
	HDEVINFO hDI;
	DWORD current;
#endif
} dc_ble_iterator_t;

typedef struct dc_ble_uart_t {
	const char *service;
	struct {
		const char *rx;
		const char *tx;
		const char *rx_credits;
		const char *tx_credits;
	} characteristics;
} dc_ble_uart_t;

typedef struct dc_ble_t {
	dc_iostream_t base;
	char name[248];
	int timeout;
	unsigned int flowcontrol;
	unsigned char credits_tx;
	unsigned char credits_rx;
	dc_queue_t *packets;
#ifdef _WIN32
	HANDLE hDevice;
	HANDLE hService;
	BLUETOOTH_GATT_EVENT_HANDLE hEvent;
	BLUETOOTH_GATT_EVENT_HANDLE hEventCredits;
	BTH_LE_GATT_SERVICE service;
	BTH_LE_GATT_CHARACTERISTIC characteristic_tx;
	BTH_LE_GATT_CHARACTERISTIC characteristic_rx;
	BTH_LE_GATT_CHARACTERISTIC characteristic_tx_credits;
	BTH_LE_GATT_CHARACTERISTIC characteristic_rx_credits;
#endif
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

static const dc_ble_uart_t g_uarts[] = {
	// Telit/Stollmann (Heinrichs Weikamp)
	{"0000fefb-0000-1000-8000-00805f9b34fb", {
		"00000001-0000-1000-8000-008025000000",
		"00000002-0000-1000-8000-008025000000",
		"00000003-0000-1000-8000-008025000000",
		"00000004-0000-1000-8000-008025000000"}},
	// U-Blox (Heinrichs Weikamp)
	{"2456e1b9-26e2-8f83-e744-f34f01e9d701", {
		"2456e1b9-26e2-8f83-e744-f34f01e9d703",
		"2456e1b9-26e2-8f83-e744-f34f01e9d703",
		"2456e1b9-26e2-8f83-e744-f34f01e9d704",
		"2456e1b9-26e2-8f83-e744-f34f01e9d704"}},
	// Nordic Semiconductor (Deepblu, Oceans, Divesoft)
	{"6e400001-b5a3-f393-e0a9-e50e24dcca9e", {
		"6e400002-b5a3-f393-e0a9-e50e24dcca9e",
		"6e400003-b5a3-f393-e0a9-e50e24dcca9e",
		NULL, NULL}},
	// Microchip (Ratio, McLean)
	{"49535343-fe7d-4ae5-8fa9-9fafd205e455", {
		"49535343-8841-43f4-a8d4-ecbe34729bb3",
		"49535343-1e4d-4bd9-ba61-23c647249616",
		NULL, NULL}},
	// Shearwater
	{"fe25c237-0ece-443c-b0aa-e02033e7029d", {
		"27b7570b-359e-45a3-91bb-cf7e70049bd2",
		"27b7570b-359e-45a3-91bb-cf7e70049bd2",
		NULL, NULL}},
	// Mares
	{"544e326b-5b72-c6b0-1c46-41c1bc448118", {
		"99a91ebd-b21f-1689-bb43-681f1f55e966",
		"1d1aae28-d2a8-91a1-1242-9d2973fbe571",
		NULL, NULL}},
	// Suunto
	{"98ae7120-e62e-11e3-badd-0002a5d5c51b", {
		"c6339440-e62e-11e3-a5b3-0002a5d5c51b",
		"d0fd6b80-e62e-11e3-a2e9-0002a5d5c51b",
		NULL, NULL}},
	// ScubaPro
	{"fdcdeaaa-295d-470e-bf15-04217b7aa0a0", {
		"a188b7dd-debb-449a-852d-c243d46b4b1a",
		"aa0c68f0-ea9c-493d-8112-62879e72af68",
		NULL, NULL}},
	// Pelagic
	{"cb3c4555-d670-4670-bc20-b61dbc851e9a", {
		"6606ab42-89d5-4a00-a8ce-4eb5e1414ee0",
		"a60b8e5c-b267-44d7-9764-837caf96489e",
		NULL, NULL}},
	// Pelagic
	{"ca7b0001-f785-4c38-b599-c7c5fbadb034", {
		"ca7b0003-f785-4c38-b599-c7c5fbadb034",
		"ca7b0002-f785-4c38-b599-c7c5fbadb034",
		NULL, NULL}},
	// Deep Six
	{"f000ffe0-ab12-45ec-84c8-46483f4626e9", {
		"f000ffe1-ab12-45ec-84c8-46483f4626e9",
		"f000ffe1-ab12-45ec-84c8-46483f4626e9",
		NULL, NULL}},
	// Divesoft
	{"0000fcef-0000-1000-8000-00805f9b34fb", {
		"6e400002-b5a3-f393-e0a9-e50e24dcca9e",
		"6e400003-b5a3-f393-e0a9-e50e24dcca9e",
		NULL, NULL}},
	// Divesoft (16bit transitional)
	{"0000fcef-0000-1000-8000-00805f9b34fb", {
		"00000002-0000-1000-8000-00805f9b34fb",
		"00000003-0000-1000-8000-00805f9b34fb",
		NULL, NULL}},
	// Halcyon Symbios
	{"00000001-8c3b-4f2c-a59e-8c08224f3253", {
		"00000101-8c3b-4f2c-a59e-8c08224f3253",
		"00000201-8c3b-4f2c-a59e-8c08224f3253",
		NULL, NULL}},
	// Seac
	{"84968ffe-d26d-478a-b953-5010bcf58bca", {
		"43c620c2-1b09-4951-bc1e-9c75298cddeb",
		"43c620c2-1b09-4951-bc1e-9c75298cddeb",
		NULL, NULL}},
};

static const dc_ble_uart_t *
dc_ble_uart_find (const char *service)
{
	if (service == NULL)
		return NULL;

	for (size_t i = 0; i < C_ARRAY_SIZE(g_uarts); ++i) {
		if (strcasecmp (g_uarts[i].service, service) == 0)
			return g_uarts + i;
	}

	return NULL;
}
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

#ifdef _WIN32
	HDEVINFO hDI = SetupDiGetClassDevs (&GUID_BLUETOOTHLE_DEVICE_INTERFACE, NULL, NULL, DIGCF_DEVICEINTERFACE | DIGCF_PRESENT);
	if (hDI == INVALID_HANDLE_VALUE) {
		DWORD errcode = GetLastError ();
		SYSERROR (context, errcode);
		status = DC_STATUS_IO;
		goto error_free;
	}

	iterator->hDI = hDI;
	iterator->current = 0;
#endif

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

#ifdef _WIN32
	SP_DEVINFO_DATA did = {0};
	did.cbSize = sizeof(SP_DEVINFO_DATA);
	while (SetupDiEnumDeviceInfo (iterator->hDI, iterator->current++, &did)) {
		char *name = win32_ble_get_name (abstract->context, iterator->hDI, &did);
		dc_ble_address_t address = win32_ble_get_address (abstract->context, iterator->hDI, &did);

		INFO (abstract->context, "Discover: address=" DC_ADDRESS_FORMAT ", name=%s",
			address, name ? name : "");

		if (!dc_descriptor_filter (iterator->descriptor, DC_TRANSPORT_BLE, name)) {
			free (name);
			continue;
		}

		device = (dc_ble_device_t *) malloc (sizeof(dc_ble_device_t));
		if (device == NULL) {
			free (name);
			return DC_STATUS_NOMEMORY;
		}

		device->address = address;
		if (name) {
			strncpy(device->name, name, sizeof(device->name) - 1);
			device->name[sizeof(device->name) - 1] = '\0';
		} else {
			memset(device->name, 0, sizeof(device->name));
		}

		free (name);

		*(dc_ble_device_t **) out = device;

		return DC_STATUS_SUCCESS;
	}
#endif

	return DC_STATUS_DONE;
}

static dc_status_t
dc_ble_iterator_free (dc_iterator_t *abstract)
{
	dc_ble_iterator_t *iterator = (dc_ble_iterator_t *) abstract;

#ifdef _WIN32
	SetupDiDestroyDeviceInfoList (iterator->hDI);
#endif

	return DC_STATUS_SUCCESS;
}
#endif

#ifdef BLE
static dc_status_t
ble_write_credits (dc_ble_t *device, unsigned char credits)
{
	dc_status_t status = DC_STATUS_SUCCESS;

#ifdef _WIN32
	status = win32_ble_characteristic_write (device->base.context, device->hService, &device->characteristic_rx_credits, &credits, sizeof(credits));
#endif

	if (status != DC_STATUS_SUCCESS) {
		return status;
	}

	device->credits_tx += credits;

	return status;
}

#ifdef _WIN32
static void CALLBACK
on_win32_ble_notify (BTH_LE_GATT_EVENT_TYPE type, void *parameter, void *userdata)
{
	dc_ble_t *device = userdata;

	if (type != CharacteristicValueChangedEvent || parameter == NULL) {
		return;
	}

	BLUETOOTH_GATT_VALUE_CHANGED_EVENT *event = parameter;
	BTH_LE_GATT_CHARACTERISTIC_VALUE *value = event->CharacteristicValue;

	if (value->DataSize == 0) {
		return;
	}

	if (event->ChangedAttributeHandle == device->characteristic_tx.AttributeHandle) {
		dc_buffer_t *packet = dc_buffer_new (value->DataSize);
		dc_buffer_append (packet, value->Data, value->DataSize);
		dc_queue_push (device->packets, packet);
		if (device->flowcontrol) {
			if (device->credits_tx > 0) {
				device->credits_tx--;
			}
			if (device->credits_tx <= UART_CREDITS_MIN) {
				ble_write_credits (device, UART_CREDITS_MAX - UART_CREDITS_MIN);
			}
		}
	} else if (event->ChangedAttributeHandle == device->characteristic_tx_credits.AttributeHandle) {
		unsigned char credits = value->Data[0];

		device->credits_rx += credits;
	}
}
#endif
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
	device->flowcontrol = 0;
	device->credits_rx = 0;
	device->credits_tx = 0;

	device->packets = dc_queue_new ((dc_queue_free_t) dc_buffer_free);
	if (device->packets == NULL) {
		ERROR (context, "Out of memory.");
		goto error_free;
	}

#ifdef _WIN32
	device->hDevice = INVALID_HANDLE_VALUE;
	device->hService = INVALID_HANDLE_VALUE;
	device->hEvent = INVALID_HANDLE_VALUE;
	device->hEventCredits = INVALID_HANDLE_VALUE;
	memset (&device->service, 0, sizeof (device->service));
	memset (&device->characteristic_tx, 0, sizeof (device->characteristic_tx));
	memset (&device->characteristic_rx, 0, sizeof (device->characteristic_rx));
	memset (&device->characteristic_tx_credits, 0, sizeof (device->characteristic_tx_credits));
	memset (&device->characteristic_rx_credits, 0, sizeof (device->characteristic_rx_credits));

	// Open the BLE device.
	status = win32_ble_open (&device->hDevice, context, GUID_BLUETOOTHLE_DEVICE_INTERFACE, address, device->name, sizeof(device->name));
	if (status != DC_STATUS_SUCCESS) {
		ERROR (context, "Failed to open the BLE device.");
		goto error_free_queue;
	}

	// Detected UART service.
	BTH_LE_GATT_SERVICE *service = NULL;

	// Get the BLE services.
	size_t nservices = 0;
	BTH_LE_GATT_SERVICE *services = NULL;
	status = win32_ble_get_services (context, device->hDevice, &services, &nservices);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (context, "Failed to get the BLE services.");
		goto error_close_device;
	}

	for (size_t i = 0; i < nservices; i++) {
		char service_buf[DC_BLE_UUID_SIZE] = {0};
		const char *service_uuid = win32_ble_uuid2str (services[i].ServiceUuid, service_buf, sizeof(service_buf));

		INFO (context, "Service: handle=%04x, uuid=%s",
			services[i].AttributeHandle, service_uuid);

		// Check for a known UART service.
		const dc_ble_uart_t *uart = dc_ble_uart_find (service_uuid);

		// Detected UART characteristics.
		BTH_LE_GATT_CHARACTERISTIC *characteristic_rx = NULL;
		BTH_LE_GATT_CHARACTERISTIC *characteristic_tx = NULL;
		BTH_LE_GATT_CHARACTERISTIC *characteristic_rx_credits = NULL;
		BTH_LE_GATT_CHARACTERISTIC *characteristic_tx_credits = NULL;

		// Get the BLE characteristics.
		size_t ncharacteristics = 0;
		BTH_LE_GATT_CHARACTERISTIC *characteristics = NULL;
		status = win32_ble_get_characteristics (context, device->hDevice, services + i, &characteristics, &ncharacteristics);
		if (status != DC_STATUS_SUCCESS) {
			ERROR (context, "Failed to get the BLE characteristics.");
			goto error_close_device;
		}

		for (size_t j = 0; j < ncharacteristics; j++) {
			char characteristic_buf[DC_BLE_UUID_SIZE] = {0};
			const char *characteristic_uuid = win32_ble_uuid2str (characteristics[j].CharacteristicUuid, characteristic_buf, sizeof(characteristic_buf));

			INFO (context, "\tCharacteristic: handle=%04x, uuid=%s, flags=%s%s%s%s%s%s%s",
				characteristics[j].AttributeHandle, characteristic_uuid,
				characteristics[j].IsBroadcastable ? "B" : "",
				characteristics[j].IsReadable ? "R" : "",
				characteristics[j].IsWritable ? "W" : "",
				characteristics[j].IsWritableWithoutResponse ? "(WWR)" : "",
				characteristics[j].IsSignedWritable ? "S" : "",
				characteristics[j].IsNotifiable ? "N" : "",
				characteristics[j].IsIndicatable ? "I" : "");

			// Get the BLE descriptors.
			size_t ndescriptors = 0;
			BTH_LE_GATT_DESCRIPTOR *descriptors = NULL;
			status = win32_ble_get_descriptors (context, device->hDevice, characteristics + j, &descriptors, &ndescriptors);
			if (status != DC_STATUS_SUCCESS) {
				ERROR (context, "Failed to get the BLE descriptors.");
				goto error_close_device;
			}

			for (size_t k = 0; k < ndescriptors; k++) {
				char descriptor_buf[DC_BLE_UUID_SIZE] = {0};
				const char *descriptor_uuid = win32_ble_uuid2str (descriptors[k].DescriptorUuid, descriptor_buf, sizeof(descriptor_buf));

				INFO (context, "\t\tDescriptor: handle=%04x, uuid=%s, type=%d",
					descriptors[k].AttributeHandle, descriptor_uuid, descriptors[k].DescriptorType);
			}

			free (descriptors);

			if (uart) {
				if (uart->characteristics.rx &&
					strcasecmp (uart->characteristics.rx, characteristic_uuid) == 0) {
					characteristic_rx = characteristics + j;
					DEBUG (context, "RX: %s", characteristic_uuid);
				}
				if (uart->characteristics.tx &&
					strcasecmp (uart->characteristics.tx, characteristic_uuid) == 0) {
					characteristic_tx = characteristics + j;
					DEBUG (context, "TX: %s", characteristic_uuid);
				}
				if (uart->characteristics.rx_credits &&
					strcasecmp (uart->characteristics.rx_credits, characteristic_uuid) == 0) {
					characteristic_rx_credits = characteristics + j;
					DEBUG (context, "RX Credits: %s", characteristic_uuid);
				}
				if (uart->characteristics.tx_credits &&
					strcasecmp (uart->characteristics.tx_credits, characteristic_uuid) == 0) {
					characteristic_tx_credits = characteristics + j;
					DEBUG (context, "TX Credits: %s", characteristic_uuid);
				}
			}
		}

		if (service == NULL && uart && characteristic_rx && characteristic_tx) {
			service = services + i;
			device->service = *service;
			device->characteristic_rx = *characteristic_rx;
			device->characteristic_tx = *characteristic_tx;
			if (characteristic_rx_credits && characteristic_tx_credits) {
				device->characteristic_rx_credits = *characteristic_rx_credits;
				device->characteristic_tx_credits = *characteristic_tx_credits;
				device->flowcontrol = 1;
			}
		}

		free (characteristics);
	}

	free (services);

	if (service == NULL) {
		ERROR (context, "No uart service found.");
		status = DC_STATUS_IO;
		goto error_close_device;
	}

	// Get the service GUID.
	GUID guid = win32_ble_uuid2guid (device->service.ServiceUuid);

	// Open the BLE service.
	status = win32_ble_open (&device->hService, context, guid, address, NULL, 0);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (context, "Failed to open the BLE service.");
		goto error_close_device;
	}

	// Enable notifications for the Tx credits characteristic.
	if (device->flowcontrol) {
		status = win32_ble_characteristic_notify (&device->hEventCredits, context, device->hService, &device->characteristic_tx_credits, (PFNBLUETOOTH_GATT_EVENT_CALLBACK) on_win32_ble_notify, device);
		if (status != DC_STATUS_SUCCESS) {
			ERROR (context, "Failed to enable notifications for the Tx credits characteristic.");
			goto error_close_service;
		}
	}

	// Enable notifications for the Tx characteristic.
	status = win32_ble_characteristic_notify (&device->hEvent, context, device->hService, &device->characteristic_tx, (PFNBLUETOOTH_GATT_EVENT_CALLBACK) on_win32_ble_notify, device);
	if (status != DC_STATUS_SUCCESS) {
		ERROR (context, "Failed to enable notifications for the Tx characteristic.");
		goto error_unregister_credits;
	}

	// Write the initial credits.
	if (device->flowcontrol) {
		status = ble_write_credits (device, UART_CREDITS_MAX);
		if (status != DC_STATUS_SUCCESS) {
			ERROR (context, "Failed to write the initial credits.");
			goto error_unregister_data;
		}
	}
#endif

	*out = (dc_iostream_t *) device;

	return DC_STATUS_SUCCESS;

#ifdef _WIN32
error_unregister_data:
	BluetoothGATTUnregisterEvent (device->hEvent, BLUETOOTH_GATT_FLAG_NONE);
error_unregister_credits:
	BluetoothGATTUnregisterEvent (device->hEventCredits, BLUETOOTH_GATT_FLAG_NONE);
error_close_service:
	CloseHandle (device->hService);
error_close_device:
	CloseHandle (device->hDevice);
#endif
error_free_queue:
	dc_queue_free (device->packets);
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

#ifdef _WIN32
	BluetoothGATTUnregisterEvent (device->hEvent, BLUETOOTH_GATT_FLAG_NONE);
	if (device->flowcontrol) {
		BluetoothGATTUnregisterEvent (device->hEventCredits, BLUETOOTH_GATT_FLAG_NONE);
	}
	CloseHandle (device->hService);
	CloseHandle (device->hDevice);
#endif

	dc_queue_free (device->packets);

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

	dc_buffer_t *packet = dc_queue_pop (device->packets, device->timeout);
	if (packet == NULL) {
		status = DC_STATUS_TIMEOUT;
		goto out;
	}

	unsigned char *p = dc_buffer_get_data (packet);
	size_t n = dc_buffer_get_size (packet);
	if (p == NULL || n > size) {
		status = DC_STATUS_IO;
		goto error;
	}

	memcpy (data, p, n);
	nbytes = n;

error:
	dc_buffer_free (packet);
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

	if (device->flowcontrol) {
		// Wait for credits.
		while (device->credits_rx == 0) {
			WARNING (abstract->context, "Waiting for uart RX credits.");
			dc_platform_sleep (10);
		}
	}

#ifdef _WIN32
	status = win32_ble_characteristic_write (abstract->context, device->hService, &device->characteristic_rx, data, size);
	if (status != DC_STATUS_SUCCESS) {
		goto out;
	}

	nbytes = size;
#endif

	if (device->flowcontrol) {
		device->credits_rx--;
	}

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

	dc_queue_clear (device->packets);

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
