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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_GLIB
#include <glib.h>
#include <gio/gio.h>
#endif

#include "ble-bluez.h"

#include "common-private.h"
#include "context-private.h"

#define BLUEZ_IFACE               "org.bluez"

#define BLUEZ_ADAPTER_IFACE            "org.bluez.Adapter1"
#define BLUEZ_ADAPTER_PROP_ADDRESS     "Address"
#define BLUEZ_ADAPTER_PROP_NAME        "Name"
#define BLUEZ_ADAPTER_PROP_POWERED     "Powered"
#define BLUEZ_ADAPTER_PROP_DISCOVERING "Discovering"
#define BLUEZ_ADAPTER_METHOD_DISCOVERY_START "StartDiscovery"
#define BLUEZ_ADAPTER_METHOD_DISCOVERY_STOP  "StopDiscovery"

#define BLUEZ_DEVICE_IFACE                 "org.bluez.Device1"
#define BLUEZ_DEVICE_PROP_PARENT           "Adapter"
#define BLUEZ_DEVICE_PROP_ADDRESS          "Address"
#define BLUEZ_DEVICE_PROP_NAME             "Name"
#define BLUEZ_DEVICE_PROP_CONNECTED        "Connected"
#define BLUEZ_DEVICE_PROP_SERVICESRESOLVED "ServicesResolved"
#define BLUEZ_DEVICE_METHOD_CONNECT    "Connect"
#define BLUEZ_DEVICE_METHOD_DISCONNECT "Disconnect"

#define BLUEZ_SERVICE_IFACE        "org.bluez.GattService1"
#define BLUEZ_SERVICE_PROP_PARENT  "Device"
#define BLUEZ_SERVICE_PROP_UUID    "UUID"
#define BLUEZ_SERVICE_PROP_HANDLE  "Handle"

#define BLUEZ_CHARACTERISTIC_IFACE          "org.bluez.GattCharacteristic1"
#define BLUEZ_CHARACTERISTIC_PROP_PARENT    "Service"
#define BLUEZ_CHARACTERISTIC_PROP_UUID      "UUID"
#define BLUEZ_CHARACTERISTIC_PROP_HANDLE    "Handle"
#define BLUEZ_CHARACTERISTIC_PROP_FLAGS     "Flags"
#define BLUEZ_CHARACTERISTIC_PROP_FLAGS_BROADCAST              "broadcast"
#define BLUEZ_CHARACTERISTIC_PROP_FLAGS_READ                   "read"
#define BLUEZ_CHARACTERISTIC_PROP_FLAGS_WRITE_WITHOUT_RESPONSE "write-without-response"
#define BLUEZ_CHARACTERISTIC_PROP_FLAGS_WRITE                  "write"
#define BLUEZ_CHARACTERISTIC_PROP_FLAGS_NOTIFY                 "notify"
#define BLUEZ_CHARACTERISTIC_PROP_FLAGS_INDICATE               "indicate"
#define BLUEZ_CHARACTERISTIC_PROP_NOTIFYING "Notifying"
#define BLUEZ_CHARACTERISTIC_PROP_MTU       "MTU"
#define BLUEZ_CHARACTERISTIC_PROP_VALUE     "Value"
#define BLUEZ_CHARACTERISTIC_METHOD_NOTIFY_START "StartNotify"
#define BLUEZ_CHARACTERISTIC_METHOD_NOTIFY_STOP  "StopNotify"
#define BLUEZ_CHARACTERISTIC_METHOD_VALUE_READ  "ReadValue"
#define BLUEZ_CHARACTERISTIC_METHOD_VALUE_WRITE "WriteValue"

#define BLUEZ_DESCRIPTOR_IFACE       "org.bluez.GattDescriptor1"
#define BLUEZ_DESCRIPTOR_PROP_PARENT "Characteristic"
#define BLUEZ_DESCRIPTOR_PROP_UUID   "UUID"
#define BLUEZ_DESCRIPTOR_PROP_HANDLE "Handle"

#ifdef HAVE_GLIB

typedef enum bluez_object_type_t {
	BLUEZ_OBJECT_NONE,
	BLUEZ_OBJECT_ADAPTER,
	BLUEZ_OBJECT_DEVICE,
	BLUEZ_OBJECT_SERVICE,
	BLUEZ_OBJECT_CHARACTERISTIC,
	BLUEZ_OBJECT_DESCRIPTOR,
} bluez_object_type_t;

typedef struct bluez_object_t {
	bluez_object_type_t type;
	char *path;
	char *parent;
	union {
		struct {
			dc_ble_address_t address;
			char name[248];
			unsigned int powered:1;
			unsigned int discovering:1;
		} adapter;
		struct {
			dc_ble_address_t address;
			char name[248];
			unsigned int connected:1;
			unsigned int resolved:1;
		} device;
		struct {
			dc_ble_uuid_t uuid;
			unsigned int handle;
		} service;
		struct {
			dc_ble_uuid_t uuid;
			unsigned int handle;
			unsigned int flags;
			unsigned int notifying:1;
			unsigned int mtu;
		} characteristic;
		struct {
			dc_ble_uuid_t uuid;
			unsigned int handle;
		} descriptor;
	};
} bluez_object_t;

typedef struct bluez_run_t bluez_run_t;

typedef void (*bluez_object_cb_t) (bluez_run_t *run, const bluez_object_t *object, void *userdata);

struct bluez_run_t {
	bluez_ble_t *bluez;
	GMainContext *ctx;
	GMainLoop *loop;
	GList *objects;
	bluez_object_cb_t add;
	bluez_object_cb_t remove;
	bluez_object_cb_t change;
	void *userdata;
	unsigned int timeout;
};

typedef struct bluez_notify_t {
	bluez_ble_characteristic_t characteristic;
	bluez_notify_cb_t callback;
	void *userdata;
} bluez_notify_t;

struct bluez_ble_t {
	dc_context_t *context;

	GDBusConnection *connection;

	GMainContext *ctx;
	GMainLoop *loop;
	GThread *thread;
	GMutex mutex;
	GHashTable *subscriptions;

	char name[248];
	dc_ble_address_t address;
	char *adapter;
	char *device;

	GList *objects;
};

static void
bluez_object_free (bluez_object_t *object)
{
	if (object == NULL)
		return;

	free (object->path);
	free (object->parent);
	free (object);
}

static bluez_object_type_t
bluez_object_type (const char *iface)
{
	bluez_object_type_t type = BLUEZ_OBJECT_NONE;

	if (iface == NULL)
		return type;

	if (g_strcmp0 (iface, BLUEZ_ADAPTER_IFACE) == 0) {
		type = BLUEZ_OBJECT_ADAPTER;
	} else if (g_strcmp0 (iface, BLUEZ_DEVICE_IFACE) == 0) {
		type = BLUEZ_OBJECT_DEVICE;
	} else if (g_strcmp0 (iface, BLUEZ_SERVICE_IFACE) == 0) {
		type = BLUEZ_OBJECT_SERVICE;
	} else if (g_strcmp0 (iface, BLUEZ_CHARACTERISTIC_IFACE) == 0) {
		type = BLUEZ_OBJECT_CHARACTERISTIC;
	} else if (g_strcmp0 (iface, BLUEZ_DESCRIPTOR_IFACE) == 0) {
		type = BLUEZ_OBJECT_DESCRIPTOR;
	}

	return type;
}

static void
bluez_object_parse_adapter (bluez_object_t *object, GVariant *properties)
{
	GVariantIter iter;
	const gchar *key = NULL;
	GVariant *value = NULL;
	g_variant_iter_init (&iter, properties);
	while (g_variant_iter_next (&iter, "{&sv}", &key, &value)) {
		if (g_strcmp0 (key, BLUEZ_ADAPTER_PROP_ADDRESS) == 0) {
			const char *address = g_variant_get_string (value, NULL);
			object->adapter.address = dc_ble_str2addr (address);
		} else if (g_strcmp0 (key, BLUEZ_ADAPTER_PROP_NAME) == 0) {
			const char *name = g_variant_get_string (value, NULL);
			strncpy (object->adapter.name, name, sizeof(object->adapter.name) - 1);
			object->adapter.name[sizeof(object->adapter.name) - 1] = '\0';
		} else if (g_strcmp0 (key, BLUEZ_ADAPTER_PROP_POWERED) == 0) {
			object->adapter.powered = g_variant_get_boolean (value);
		} else if (g_strcmp0 (key, BLUEZ_ADAPTER_PROP_DISCOVERING) == 0) {
			object->adapter.discovering = g_variant_get_boolean (value);
		}
		g_variant_unref(value);
	}
}

static void
bluez_object_parse_device (bluez_object_t *object, GVariant *properties)
{
	GVariantIter iter;
	const gchar *key = NULL;
	GVariant *value = NULL;
	g_variant_iter_init (&iter, properties);
	while (g_variant_iter_next (&iter, "{&sv}", &key, &value)) {
		if (g_strcmp0 (key, BLUEZ_DEVICE_PROP_PARENT) == 0) {
			object->parent = strdup (g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, BLUEZ_DEVICE_PROP_ADDRESS) == 0) {
			const char *address = g_variant_get_string (value, NULL);
			object->device.address = dc_ble_str2addr (address);
		} else if (g_strcmp0 (key, BLUEZ_DEVICE_PROP_NAME) == 0) {
			const char *name = g_variant_get_string (value, NULL);
			strncpy (object->device.name, name, sizeof(object->device.name) - 1);
			object->device.name[sizeof(object->device.name) - 1] = '\0';
		} else if (g_strcmp0 (key, BLUEZ_DEVICE_PROP_CONNECTED) == 0) {
			object->device.connected = g_variant_get_boolean (value);
		} else if (g_strcmp0 (key, BLUEZ_DEVICE_PROP_SERVICESRESOLVED) == 0) {
			object->device.resolved = g_variant_get_boolean (value);
		}
		g_variant_unref (value);
	}
}

static void
bluez_object_parse_service (bluez_object_t *object, GVariant *properties)
{
	GVariantIter iter;
	const gchar *key = NULL;
	GVariant *value = NULL;
	g_variant_iter_init (&iter, properties);
	while (g_variant_iter_next (&iter, "{&sv}", &key, &value)) {
		if (g_strcmp0 (key, BLUEZ_SERVICE_PROP_PARENT) == 0) {
			object->parent = strdup (g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, BLUEZ_SERVICE_PROP_UUID) == 0) {
			const char *uuid = g_variant_get_string (value, NULL);
			dc_ble_str2uuid (uuid, object->service.uuid);
		} else if (g_strcmp0 (key, BLUEZ_SERVICE_PROP_HANDLE) == 0) {
			object->service.handle = g_variant_get_uint16 (value);
		}
		g_variant_unref (value);
	}
}

static void
bluez_object_parse_characteristic (bluez_object_t *object, GVariant *properties)
{
	GVariantIter iter;
	const gchar *key = NULL;
	GVariant *value = NULL;
	g_variant_iter_init (&iter, properties);
	while (g_variant_iter_next (&iter, "{&sv}", &key, &value)) {
		if (g_strcmp0 (key, BLUEZ_CHARACTERISTIC_PROP_PARENT) == 0) {
			object->parent = strdup (g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, BLUEZ_CHARACTERISTIC_PROP_UUID) == 0) {
			const char *uuid = g_variant_get_string (value, NULL);
			dc_ble_str2uuid (uuid, object->characteristic.uuid);
		} else if (g_strcmp0 (key, BLUEZ_CHARACTERISTIC_PROP_HANDLE) == 0) {
			object->characteristic.handle = g_variant_get_uint16 (value);
		} else if (g_strcmp0 (key, BLUEZ_CHARACTERISTIC_PROP_FLAGS) == 0) {
			GVariantIter flags;
			const gchar *flag = NULL;
			g_variant_iter_init (&flags, value);
			while (g_variant_iter_next (&flags, "&s", &flag)) {
				if (g_strcmp0 (flag, BLUEZ_CHARACTERISTIC_PROP_FLAGS_BROADCAST) == 0) {
					object->characteristic.flags |= GATT_PROP_BROADCAST;
				} else if (g_strcmp0 (flag, BLUEZ_CHARACTERISTIC_PROP_FLAGS_READ) == 0) {
					object->characteristic.flags |= GATT_PROP_READ;
				} else if (g_strcmp0 (flag, BLUEZ_CHARACTERISTIC_PROP_FLAGS_WRITE_WITHOUT_RESPONSE) == 0) {
					object->characteristic.flags |= GATT_PROP_WRITE_WITHOUT_RESPONSE;
				} else if (g_strcmp0 (flag, BLUEZ_CHARACTERISTIC_PROP_FLAGS_WRITE) == 0) {
					object->characteristic.flags |= GATT_PROP_WRITE;
				} else if (g_strcmp0 (flag, BLUEZ_CHARACTERISTIC_PROP_FLAGS_NOTIFY) == 0) {
					object->characteristic.flags |= GATT_PROP_NOTIFY;
				} else if (g_strcmp0 (flag, BLUEZ_CHARACTERISTIC_PROP_FLAGS_INDICATE) == 0) {
					object->characteristic.flags |= GATT_PROP_INDICATE;
				}
			}
		} else if (g_strcmp0 (key, BLUEZ_CHARACTERISTIC_PROP_NOTIFYING) == 0) {
			object->characteristic.notifying = g_variant_get_boolean (value);
		} else if (g_strcmp0 (key, BLUEZ_CHARACTERISTIC_PROP_MTU) == 0) {
			object->characteristic.mtu = g_variant_get_uint16 (value);
		}
		g_variant_unref (value);
	}
}

static void
bluez_object_parse_descriptor (bluez_object_t *object, GVariant *properties)
{
	GVariantIter iter;
	const gchar *key = NULL;
	GVariant *value = NULL;
	g_variant_iter_init (&iter, properties);
	while (g_variant_iter_next (&iter, "{&sv}", &key, &value)) {
		if (g_strcmp0 (key, BLUEZ_DESCRIPTOR_PROP_PARENT) == 0) {
			object->parent = strdup (g_variant_get_string (value, NULL));
		} else if (g_strcmp0 (key, BLUEZ_DESCRIPTOR_PROP_UUID) == 0) {
			const char *uuid = g_variant_get_string (value, NULL);
			dc_ble_str2uuid (uuid, object->descriptor.uuid);
		} else if (g_strcmp0 (key, BLUEZ_DESCRIPTOR_PROP_HANDLE) == 0) {
			object->descriptor.handle = g_variant_get_uint16 (value);
		}
		g_variant_unref (value);
	}
}

static void
bluez_object_add (bluez_run_t *run, const char *path, const char *iface, GVariant *properties)
{
	bluez_object_t *object = NULL;

	// Get the object type.
	bluez_object_type_t type = bluez_object_type (iface);
	if (type == BLUEZ_OBJECT_NONE) {
		return;
	}

	object = malloc (sizeof (*object));
	if (object == NULL) {
		return;
	}

	memset (object, 0, sizeof (*object));
	object->type = type;
	object->path = strdup (path);

	if (type == BLUEZ_OBJECT_ADAPTER) {
		bluez_object_parse_adapter (object, properties);
	} else if (type == BLUEZ_OBJECT_DEVICE) {
		bluez_object_parse_device (object, properties);
	} else if (type == BLUEZ_OBJECT_SERVICE) {
		bluez_object_parse_service (object, properties);
	} else if (type == BLUEZ_OBJECT_CHARACTERISTIC) {
		bluez_object_parse_characteristic (object, properties);
	} else if (type == BLUEZ_OBJECT_DESCRIPTOR) {
		bluez_object_parse_descriptor (object, properties);
	}

	run->objects = g_list_append (run->objects, object);

	if (run->add) {
		run->add (run, object, run->userdata);
	}
}

static void
bluez_object_remove (bluez_run_t *run, const char *path, const char *iface)
{
	bluez_object_t *object = NULL;

	// Get the object type.
	bluez_object_type_t type = bluez_object_type (iface);
	if (type == BLUEZ_OBJECT_NONE) {
		return;
	}

	// Find the object.
	for (GList *iterator = run->objects; iterator; iterator = iterator->next) {
		bluez_object_t *item = iterator->data;
		if (g_strcmp0 (path, item->path) == 0) {
			object = item;
			break;
		}
	}

	if (object == NULL || object->type != type) {
		return;
	}

	run->objects = g_list_remove (run->objects, object);

	if (run->remove) {
		run->remove (run, object, run->userdata);
	}

	bluez_object_free (object);
}

static void
bluez_object_change (bluez_run_t *run, const char *path, const char *iface, GVariant *properties)
{
	bluez_object_t *object = NULL;

	// Get the object type.
	bluez_object_type_t type = bluez_object_type (iface);
	if (type == BLUEZ_OBJECT_NONE) {
		return;
	}

	// Find the object.
	for (GList *iterator = run->objects; iterator; iterator = iterator->next) {
		bluez_object_t *item = iterator->data;
		if (g_strcmp0 (path, item->path) == 0) {
			object = item;
			break;
		}
	}

	if (object == NULL || object->type != type) {
		return;
	}

	if (type == BLUEZ_OBJECT_ADAPTER) {
		bluez_object_parse_adapter (object, properties);
	} else if (type == BLUEZ_OBJECT_DEVICE) {
		bluez_object_parse_device (object, properties);
	} else if (type == BLUEZ_OBJECT_SERVICE) {
		bluez_object_parse_service (object, properties);
	} else if (type == BLUEZ_OBJECT_CHARACTERISTIC) {
		bluez_object_parse_characteristic (object, properties);
	} else if (type == BLUEZ_OBJECT_DESCRIPTOR) {
		bluez_object_parse_descriptor (object, properties);
	}

	if (run->change) {
		run->change (run, object, run->userdata);
	}
}

static void
on_bluez_added(GDBusConnection *conn,
					const gchar *sender,
					const gchar *path,
					const gchar *interface,
					const gchar *signal,
					GVariant *parameters,
					void *userdata)
{
	bluez_run_t *run = userdata;

	const char *object = NULL;
	const char *iface = NULL;
	GVariantIter *interfaces = NULL;
	GVariant *properties = NULL;

	g_variant_get (parameters, "(&oa{sa{sv}})", &object, &interfaces);
	while (g_variant_iter_next (interfaces, "{&s@a{sv}}", &iface, &properties)) {
		bluez_object_add (run, object, iface, properties);
		g_variant_unref (properties);
	}
	g_variant_iter_free (interfaces);
}

static void
on_bluez_removed(GDBusConnection *conn,
					const gchar *sender,
					const gchar *path,
					const gchar *interface,
					const gchar *signal,
					GVariant *parameters,
					void *userdata)
{
	bluez_run_t *run = userdata;

	const char *object = NULL;
	const char *iface = NULL;
	GVariantIter *interfaces = NULL;

	g_variant_get (parameters, "(&oas)", &object, &interfaces);
	while (g_variant_iter_next (interfaces, "&s", &iface)) {
		bluez_object_remove (run, object, iface);
	}
	g_variant_iter_free (interfaces);
}

static void
on_bluez_changed(GDBusConnection *conn,
					const gchar *sender,
					const gchar *path,
					const gchar *interface,
					const gchar *signal,
					GVariant *parameters,
					void *userdata)
{
	bluez_run_t *run = userdata;

	const char *iface = NULL;
	GVariant *properties = NULL;

	g_variant_get (parameters, "(&s@a{sv}as)", &iface, &properties, NULL);
	bluez_object_change (run, path, iface, properties);
	g_variant_unref (properties);
}

static void
on_bluez_characteristic_changed (
	GDBusConnection *conn,
	const gchar *sender,
	const gchar *path,
	const gchar *interface,
	const gchar *signal,
	GVariant *parameters,
	void *userdata)
{
	bluez_ble_t *bluez = (bluez_ble_t *) userdata;

	const char *iface = NULL;
	const char *key = NULL;
	GVariant *value = NULL;
	GVariantIter *properties = NULL;

	g_variant_get (parameters, "(&sa{sv}as)", &iface, &properties, NULL);
	while (g_variant_iter_next (properties, "{&sv}", &key, &value)) {
		if (g_strcmp0 (key, BLUEZ_CHARACTERISTIC_PROP_VALUE) == 0) {
			gsize len = 0;
			gconstpointer ptr = g_variant_get_fixed_array (value, &len, sizeof(unsigned char));

			g_mutex_lock (&bluez->mutex);
			bluez_notify_t *subscription = g_hash_table_lookup (bluez->subscriptions, path);
			if (subscription) {
				subscription->callback (bluez, &subscription->characteristic, ptr, len, subscription->userdata);
			}
			g_mutex_unlock (&bluez->mutex);
		}

		g_variant_unref (value);
	}
	g_variant_iter_free (properties);
}

static gpointer
bluez_ble_threadfunc (gpointer data)
{
	bluez_ble_t *bluez = (bluez_ble_t *) data;

	g_main_context_push_thread_default (bluez->ctx);

	guint changed_id = g_dbus_connection_signal_subscribe (bluez->connection,
						BLUEZ_IFACE,
						"org.freedesktop.DBus.Properties",
						"PropertiesChanged",
						NULL,
						BLUEZ_CHARACTERISTIC_IFACE,
						G_DBUS_SIGNAL_FLAGS_NONE,
						on_bluez_characteristic_changed,
						bluez,
						NULL);

	g_main_loop_run (bluez->loop);

	g_dbus_connection_signal_unsubscribe (bluez->connection, changed_id);

	g_main_context_pop_thread_default (bluez->ctx);

	return NULL;
}

static dc_status_t
bluez_ble_call (bluez_ble_t *bluez, const char *path, const char *interface, const char *method, GVariant *parameters, GVariant **out_result)
{
	GVariant *result = NULL;
	GError *error = NULL;

	result = g_dbus_connection_call_sync (bluez->connection,
			BLUEZ_IFACE,
			path,
			interface,
			method,
			parameters,
			NULL,
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			NULL,
			&error);
	if (result == NULL) {
		if (error) {
			ERROR (bluez->context, "D-Bus call %s.%s failed with error %i %i (%s)", interface, method, error->domain, error->code, error->message);
			g_error_free (error);
		} else {
			ERROR (bluez->context, "D-Bus call %s.%s failed", interface, method);
		}
		return DC_STATUS_IO;
	}

	if (out_result) {
		*out_result = result;
	} else {
		g_variant_unref (result);
	}

	return DC_STATUS_SUCCESS;
}

static void
bluez_dbus_call_async_done (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	dc_context_t *context = user_data;

	GVariant *result = NULL;
	GError *error = NULL;

	result = g_dbus_connection_call_finish ((GDBusConnection *)source_object, res, &error);
	if (result == NULL) {
		if (error) {
			ERROR (context, "D-Bus call failed with error %i %i (%s)", error->domain, error->code, error->message);
			g_error_free (error);
		} else {
			ERROR (context, "D-Bus call failed");
		}
		return;// DC_STATUS_IO;
	}

	g_variant_unref (result);
}

static void
bluez_ble_call_async (bluez_ble_t *bluez, const char *path, const char *interface, const char *method, GVariant *parameters)
{
	g_dbus_connection_call (bluez->connection,
			BLUEZ_IFACE,
			path,
			interface,
			method,
			parameters,
			NULL,
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			NULL,
			bluez_dbus_call_async_done,
			bluez->context);
}


static bluez_object_t *
bluez_ble_object_find (bluez_ble_t *bluez, bluez_object_type_t type, unsigned int handle)
{
	for (GList *iterator = bluez->objects; iterator; iterator = iterator->next) {
		bluez_object_t *object = iterator->data;
		if (object->type == BLUEZ_OBJECT_SERVICE &&
			object->service.handle == handle) {
			return object;
		} else if (object->type == BLUEZ_OBJECT_CHARACTERISTIC &&
			object->characteristic.handle == handle) {
			return object;
		} else if (object->type == BLUEZ_OBJECT_DESCRIPTOR &&
			object->descriptor.handle == handle) {
			return object;
		}
	}

	return NULL;
}

static dc_status_t
bluez_ble_get_objects (bluez_ble_t *bluez, bluez_object_type_t type, const char *parent, void **out_objects, size_t *out_nobjects)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	void *objects = NULL;
	size_t nobjects = 0;

	size_t itemsize = 0;
	switch (type) {
	case BLUEZ_OBJECT_ADAPTER:
	case BLUEZ_OBJECT_DEVICE:
		itemsize = sizeof (bluez_ble_device_t);
		break;
	case BLUEZ_OBJECT_SERVICE:
		itemsize = sizeof (bluez_ble_service_t);
		break;
	case BLUEZ_OBJECT_CHARACTERISTIC:
		itemsize = sizeof (bluez_ble_characteristic_t);
		break;
	case BLUEZ_OBJECT_DESCRIPTOR:
		itemsize = sizeof (bluez_ble_descriptor_t);
		break;
	default:
		return DC_STATUS_INVALIDARGS;
	}

	guint count = g_list_length (bluez->objects);

	objects = malloc (count * itemsize);
	if (objects == NULL) {
		status = DC_STATUS_NOMEMORY;
		goto error_exit;
	}

	memset (objects, 0, count * itemsize);

	for (GList *iterator = bluez->objects; iterator; iterator = iterator->next) {
		bluez_object_t *object = iterator->data;
		if (object->type != type || g_strcmp0 (object->parent, parent) != 0)
			continue;

		if (type == BLUEZ_OBJECT_ADAPTER) {
			bluez_ble_device_t *adapter = (bluez_ble_device_t *) objects + nobjects++;
			memcpy (adapter->name, object->adapter.name, sizeof(object->adapter.name));
			adapter->address = object->adapter.address;
		} else if (type == BLUEZ_OBJECT_DEVICE) {
			bluez_ble_device_t *device = (bluez_ble_device_t *) objects + nobjects++;
			memcpy (device->name, object->device.name, sizeof(object->device.name));
			device->address = object->device.address;
		} else if (type == BLUEZ_OBJECT_SERVICE) {
			bluez_ble_service_t *service = (bluez_ble_service_t *) objects + nobjects++;
			memcpy (service->uuid, object->service.uuid, sizeof(object->service.uuid));
			service->handle = object->service.handle;
		} else if (type == BLUEZ_OBJECT_CHARACTERISTIC) {
			bluez_ble_characteristic_t *characteristic = (bluez_ble_characteristic_t *) objects + nobjects++;
			memcpy (characteristic->uuid, object->characteristic.uuid, sizeof(object->characteristic.uuid));
			characteristic->handle = object->characteristic.handle;
			characteristic->flags = object->characteristic.flags;
			characteristic->mtu = object->characteristic.mtu;
		} else if (type == BLUEZ_OBJECT_DESCRIPTOR) {
			bluez_ble_descriptor_t *descriptor = (bluez_ble_descriptor_t *) objects +nobjects++;
			memcpy (descriptor->uuid, object->descriptor.uuid, sizeof(object->descriptor.uuid));
			descriptor->handle = object->descriptor.handle;
		}
	}

out:
	*out_objects = objects;
	*out_nobjects = nobjects;

	return DC_STATUS_SUCCESS;

error_free:
	free (objects);
error_exit:
	return status;
}

dc_status_t
bluez_ble_new (bluez_ble_t **out, dc_context_t *context)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	bluez_ble_t *bluez = NULL;

	bluez = malloc (sizeof (*bluez));
	if (bluez == NULL) {
		status = DC_STATUS_NOMEMORY;
		goto error_exit;
	}

	bluez->context = context;
	bluez->objects = NULL;

	bluez->address = 0;
	bluez->adapter = NULL;
	bluez->device = NULL;

	GError *error = NULL;
	bluez->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (bluez->connection == NULL) {
		ERROR (context, "D-Bus error (%s)", error ? error->message : "unknown");
		g_error_free (error);
		status = DC_STATUS_IO;
		goto error_free;
	}

	g_mutex_init (&bluez->mutex);
	bluez->subscriptions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	bluez->ctx = g_main_context_new ();
	bluez->loop = g_main_loop_new (bluez->ctx, FALSE);
	bluez->thread = g_thread_new ("bluez", bluez_ble_threadfunc, bluez);

	*out = bluez;

	return DC_STATUS_SUCCESS;

error_free:
	free (bluez);
error_exit:
	return status;
}

void
bluez_ble_free (bluez_ble_t *bluez)
{
	if (bluez == NULL)
		return;

	free (bluez->adapter);
	free (bluez->device);

	// Terminate the background thread.
	g_main_loop_quit (bluez->loop);
	g_thread_join (bluez->thread);
	g_main_loop_unref (bluez->loop);
	g_main_context_unref (bluez->ctx);

	g_mutex_clear (&bluez->mutex);

	g_object_unref (bluez->connection);

	g_list_free_full (bluez->objects, (GDestroyNotify) bluez_object_free);
	g_hash_table_destroy (bluez->subscriptions);

	free (bluez);
}

static gboolean
on_bluez_timeout (gpointer userdata)
{
	bluez_run_t *run = userdata;

	g_main_loop_quit (run->loop);
	run->timeout = 1;

	return G_SOURCE_REMOVE;
}

static dc_status_t
bluez_ble_run (bluez_ble_t *bluez, int timeout, bluez_object_cb_t add, bluez_object_cb_t remove, bluez_object_cb_t change, void *userdata)
{
	bluez_run_t run = {
		bluez,
		NULL, NULL, NULL,
		add,
		remove,
		change,
		userdata,
		0,
	};

	run.ctx = g_main_context_new ();
	run.loop = g_main_loop_new (run.ctx, FALSE);

	g_main_context_push_thread_default (run.ctx);

	GError *error = NULL;
	GVariant *result = g_dbus_connection_call_sync (bluez->connection,
			BLUEZ_IFACE,
			"/",
			"org.freedesktop.DBus.ObjectManager",
			"GetManagedObjects",
			NULL,
			G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
			G_DBUS_CALL_FLAGS_NONE,
			-1,
			NULL,
			&error);
	if (result == NULL) {
		ERROR (bluez->context, "D-Bus error (%s)", error ? error->message : "unknown");
		g_error_free (error);
		return DC_STATUS_IO;
	}

	const char *object = NULL;
	const char *iface = NULL;
	GVariant *properties = NULL;
	GVariantIter *objects = NULL;
	GVariantIter *interfaces = NULL;

	g_variant_get (result, "(a{oa{sa{sv}}})", &objects);
	while (g_variant_iter_next (objects, "{&oa{sa{sv}}}", &object, &interfaces)) {
		while (g_variant_iter_next (interfaces, "{&s@a{sv}}", &iface, &properties)) {
			bluez_object_add (&run, object, iface, properties);
			g_variant_unref (properties);
		}
		g_variant_iter_free (interfaces);
	}
	g_variant_iter_free (objects);

	g_variant_unref (result);

	guint added_id = g_dbus_connection_signal_subscribe (bluez->connection,
							BLUEZ_IFACE,
							"org.freedesktop.DBus.ObjectManager",
							"InterfacesAdded",
							NULL,
							NULL,
							G_DBUS_SIGNAL_FLAGS_NONE,
							on_bluez_added,
							&run,
							NULL);

	guint removed_id = g_dbus_connection_signal_subscribe (bluez->connection,
							BLUEZ_IFACE,
							"org.freedesktop.DBus.ObjectManager",
							"InterfacesRemoved",
							NULL,
							NULL,
							G_DBUS_SIGNAL_FLAGS_NONE,
							on_bluez_removed,
							&run,
							NULL);

	guint changed_id = g_dbus_connection_signal_subscribe (bluez->connection,
						BLUEZ_IFACE,
						"org.freedesktop.DBus.Properties",
						"PropertiesChanged",
						NULL,
						NULL,
						G_DBUS_SIGNAL_FLAGS_NONE,
						on_bluez_changed,
						&run,
						NULL);

	if (timeout >= 0) {
		GSource *source = g_timeout_source_new_seconds (timeout);
		g_source_set_callback (source, on_bluez_timeout, &run, NULL);
		guint source_id = g_source_attach (source, run.ctx);
		g_source_unref (source);
	}

	g_main_loop_run (run.loop);

	g_dbus_connection_signal_unsubscribe (bluez->connection, changed_id);
	g_dbus_connection_signal_unsubscribe (bluez->connection, removed_id);
	g_dbus_connection_signal_unsubscribe (bluez->connection, added_id);

	g_main_context_pop_thread_default (run.ctx);
	g_main_loop_unref (run.loop);
	g_main_context_unref (run.ctx);

	bluez->objects = run.objects;

#if 0
	for (GList *current = bluez->objects; current; current = current->next) {
		bluez_object_t *object = current->data;
		DEBUG (bluez->context, "Object: %u %s %s", object->type, object->path, object->parent);
	}

	for (GList *adapters = bluez->objects; adapters; adapters = adapters->next) {
		bluez_object_t *adapter = adapters->data;
		if (adapter->type != BLUEZ_OBJECT_ADAPTER || adapter->parent != NULL)
			continue;

		DEBUG (bluez->context, "Adapter: "DC_ADDRESS_FORMAT" %s %u %u",
			adapter->adapter.address, adapter->adapter.name, adapter->adapter.powered, adapter->adapter.discovering);

		for (GList *devices = bluez->objects; devices; devices = devices->next) {
			bluez_object_t *device = devices->data;
			if (device->type != BLUEZ_OBJECT_DEVICE || g_strcmp0(device->parent, adapter->path) != 0)
				continue;

			DEBUG (bluez->context, "\tDevice: "DC_ADDRESS_FORMAT" %s %u %u",
				device->device.address, device->device.name, device->device.connected, device->device.resolved);

			for (GList *services = bluez->objects; services; services = services->next) {
				bluez_object_t *service = services->data;
				if (service->type != BLUEZ_OBJECT_SERVICE || g_strcmp0(service->parent, device->path) != 0)
					continue;

				char service_uuid[DC_BLE_UUID_SIZE] = {0};
				INFO (bluez->context, "\t\tService: handle=%04x, uuid=%s",
					service->service.handle,
					dc_ble_uuid2str(service->service.uuid, service_uuid, sizeof(service_uuid)));

				for (GList *characteristics = bluez->objects; characteristics; characteristics = characteristics->next) {
					bluez_object_t *characteristic = characteristics->data;
					if (characteristic->type != BLUEZ_OBJECT_CHARACTERISTIC || g_strcmp0(characteristic->parent, service->path) != 0)
						continue;

					char characteristic_uuid[DC_BLE_UUID_SIZE] = {0};
					INFO (bluez->context, "\t\t\tCharacteristic: handle=%04x, uuid=%s, flags=%s%s%s%s%s%s%s, mtu=%u",
						characteristic->characteristic.handle,
						dc_ble_uuid2str(characteristic->characteristic.uuid, characteristic_uuid, sizeof(characteristic_uuid)),
						characteristic->characteristic.flags & GATT_PROP_BROADCAST ? "B" : "",
						characteristic->characteristic.flags & GATT_PROP_READ ? "R" : "",
						characteristic->characteristic.flags & GATT_PROP_WRITE ? "W" : "",
						characteristic->characteristic.flags & GATT_PROP_WRITE_WITHOUT_RESPONSE ? "(WWR)" : "",
						0 ? "S" : "",
						characteristic->characteristic.flags & GATT_PROP_NOTIFY ? "N" : "",
						characteristic->characteristic.flags & GATT_PROP_INDICATE ? "I" : "",
						characteristic->characteristic.mtu);

					for (GList *descriptors = bluez->objects; descriptors; descriptors = descriptors->next) {
						bluez_object_t *descriptor = descriptors->data;
						if (descriptor->type != BLUEZ_OBJECT_DESCRIPTOR || g_strcmp0(descriptor->parent, characteristic->path) != 0)
							continue;

						char descriptor_uuid[DC_BLE_UUID_SIZE] = {0};
						INFO (bluez->context, "\t\t\t\tDescriptor: handle=%04x, uuid=%s",
							descriptor->descriptor.handle,
							dc_ble_uuid2str(descriptor->descriptor.uuid, descriptor_uuid, sizeof(descriptor_uuid)));
					}
				}
			}
		}
	}
#endif

	if (run.timeout) {
		return DC_STATUS_TIMEOUT;
	}

	return DC_STATUS_SUCCESS;
}

static void
on_bluez_scan_add (bluez_run_t *run, const bluez_object_t *object, void *userdata)
{
	bluez_ble_t *bluez = run->bluez;

	if (object->type == BLUEZ_OBJECT_ADAPTER) {
		if (bluez->adapter == NULL) {
			bluez->adapter = g_strdup (object->path);
			if (!object->adapter.discovering) {
				bluez_ble_call_async (bluez, object->path, BLUEZ_ADAPTER_IFACE, BLUEZ_ADAPTER_METHOD_DISCOVERY_START, NULL);
			}
		}
	} else if (object->type == BLUEZ_OBJECT_DEVICE) {
	}
}

static void
on_bluez_scan_remove (bluez_run_t *run, const bluez_object_t *object, void *userdata)
{
}

static void
on_bluez_connect_add (bluez_run_t *run, const bluez_object_t *object, void *userdata)
{
	bluez_ble_t *bluez = run->bluez;

	if (object->type == BLUEZ_OBJECT_ADAPTER) {
		if (bluez->adapter == NULL) {
			bluez->adapter = strdup (object->path);
			if (!object->adapter.discovering) {
				bluez_ble_call_async (bluez, object->path, BLUEZ_ADAPTER_IFACE, BLUEZ_ADAPTER_METHOD_DISCOVERY_START, NULL);
			}
		}
	} else if (object->type == BLUEZ_OBJECT_DEVICE) {
		if (bluez->device == NULL && bluez->address == object->device.address) {
			bluez->device = strdup (object->path);
			memcpy (bluez->name, object->device.name, sizeof(bluez->name));
			if (!object->device.connected) {
				bluez_ble_call_async (bluez,
					object->path,
					BLUEZ_DEVICE_IFACE,
					BLUEZ_DEVICE_METHOD_CONNECT,
					NULL);
			}
		}
	} else if (object->type == BLUEZ_OBJECT_SERVICE) {

	} else if (object->type == BLUEZ_OBJECT_CHARACTERISTIC) {

	} else if (object->type == BLUEZ_OBJECT_DESCRIPTOR) {

	}
}

static void
on_bluez_connect_remove (bluez_run_t *run, const bluez_object_t *object, void *userdata)
{
}

static void
on_bluez_connect_change (bluez_run_t *run, const bluez_object_t *object, void *userdata)
{
	bluez_ble_t *bluez = run->bluez;

	if (object->type == BLUEZ_OBJECT_DEVICE) {
		if (bluez->device &&
			g_strcmp0 (bluez->device, object->path) == 0 &&
			object->device.resolved) {
			g_main_loop_quit (run->loop);
		}
	}
}

dc_status_t
bluez_ble_scan (bluez_ble_t *bluez, int timeout, bluez_ble_device_t **out_devices, size_t *out_ndevices)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	bluez_ble_device_t *devices = NULL;
	size_t ndevices = 0;

	free (bluez->adapter);
	free (bluez->device);
	bluez->adapter = NULL;
	bluez->device = NULL;

	status = bluez_ble_run (bluez, timeout, on_bluez_scan_add, on_bluez_scan_remove, NULL, NULL);
	if (status != DC_STATUS_SUCCESS && status != DC_STATUS_TIMEOUT) {
		goto error_exit;
	}

	status = bluez_ble_get_objects (bluez, BLUEZ_OBJECT_DEVICE, bluez->adapter, (void **) &devices, &ndevices);
	if (status != DC_STATUS_SUCCESS) {
		goto error_exit;
	}

out:
	*out_devices = devices;
	*out_ndevices = ndevices;

	return DC_STATUS_SUCCESS;

error_free:
	free (devices);
error_exit:
	return status;
}

dc_status_t
bluez_ble_connect (bluez_ble_t *bluez, dc_ble_address_t address)
{
	free (bluez->adapter);
	free (bluez->device);
	bluez->adapter = NULL;
	bluez->device = NULL;

	bluez->address = address;

	return bluez_ble_run (bluez, 30, on_bluez_connect_add, on_bluez_connect_remove, on_bluez_connect_change, NULL);
}

dc_status_t
bluez_ble_disconnect (bluez_ble_t *bluez)
{
	return bluez_ble_call (bluez,
			bluez->device,
			BLUEZ_DEVICE_IFACE,
			BLUEZ_DEVICE_METHOD_DISCONNECT,
			NULL,
			NULL);
}

dc_status_t
bluez_ble_get_services (bluez_ble_t *bluez, bluez_ble_service_t **out_services, size_t *out_nservices)
{
	return bluez_ble_get_objects (bluez, BLUEZ_OBJECT_SERVICE, bluez->device, (void **) out_services, out_nservices);
}

dc_status_t
bluez_ble_get_characteristics (bluez_ble_t *bluez, const bluez_ble_service_t *service, bluez_ble_characteristic_t **out_characteristics, size_t *out_ncharacteristics)
{
	if (service == NULL) {
		ERROR (bluez->context, "Invalid BLE service.");
		return DC_STATUS_INVALIDARGS;
	}

	bluez_object_t *object = bluez_ble_object_find (bluez, BLUEZ_OBJECT_SERVICE, service->handle);
	if (object == NULL) {
		char uuidstr[DC_BLE_UUID_SIZE] = {0};
		ERROR (bluez->context, "Failed to find BLE service '%s'.",
			dc_ble_uuid2str (service->uuid, uuidstr, sizeof(uuidstr)));
		return DC_STATUS_IO;
	}

	return bluez_ble_get_objects (bluez, BLUEZ_OBJECT_CHARACTERISTIC, object->path, (void **) out_characteristics, out_ncharacteristics);
}

dc_status_t
bluez_ble_get_descriptors (bluez_ble_t *bluez, const bluez_ble_characteristic_t *characteristic, bluez_ble_descriptor_t **out_descriptors, size_t *out_ndescriptors)
{
	if (characteristic == NULL) {
		ERROR (bluez->context, "Invalid BLE characteristic.");
		return DC_STATUS_INVALIDARGS;
	}

	bluez_object_t *object = bluez_ble_object_find (bluez, BLUEZ_OBJECT_CHARACTERISTIC, characteristic->handle);
	if (object == NULL) {
		char uuidstr[DC_BLE_UUID_SIZE] = {0};
		ERROR (bluez->context, "Failed to find BLE characteristic '%s'.",
			dc_ble_uuid2str (characteristic->uuid, uuidstr, sizeof(uuidstr)));
		return DC_STATUS_IO;
	}

	return bluez_ble_get_objects (bluez, BLUEZ_OBJECT_DESCRIPTOR, object->path, (void **) out_descriptors, out_ndescriptors);
}

dc_status_t
bluez_ble_characteristic_read (bluez_ble_t *bluez, const bluez_ble_characteristic_t *characteristic, void *data, size_t size, size_t *actual)
{
	dc_status_t status = DC_STATUS_SUCCESS;
	size_t nbytes = 0;

	if (characteristic == NULL ||
		(characteristic->flags & GATT_PROP_READ) == 0) {
		ERROR (bluez->context, "Characteristic does not support reading.");
		status = DC_STATUS_INVALIDARGS;
		goto error_exit;
	}

	bluez_object_t *object = bluez_ble_object_find (bluez, BLUEZ_OBJECT_CHARACTERISTIC, characteristic->handle);
	if (object == NULL) {
		char uuidstr[DC_BLE_UUID_SIZE] = {0};
		ERROR (bluez->context, "Failed to find BLE characteristic '%s'.",
			dc_ble_uuid2str (characteristic->uuid, uuidstr, sizeof(uuidstr)));
		status = DC_STATUS_IO;
		goto error_exit;
	}

	GVariant *params = g_variant_new("(a{sv})", NULL);

	GVariant *result = NULL;
	status = bluez_ble_call (bluez,
		object->path,
		BLUEZ_CHARACTERISTIC_IFACE,
		BLUEZ_CHARACTERISTIC_METHOD_VALUE_READ,
		params,
		&result);
	if (status != DC_STATUS_SUCCESS) {
		goto error_exit;
	}

	GVariant *value = g_variant_get_child_value (result, 0);
	g_variant_unref (result);

	gsize len = 0;
	gconstpointer ptr = g_variant_get_fixed_array (value, &len, sizeof(unsigned char));
	if (ptr == NULL) {
		ERROR (bluez->context, "Failed to get the value.");
		status = DC_STATUS_IO;
		goto error_free;
	}

	nbytes = len;
	if (nbytes > size) {
		ERROR (bluez->context, "The value is too large (" DC_PRINTF_SIZE ").", nbytes);
		status = DC_STATUS_IO;
		nbytes = size;
	}

	memcpy (data, ptr, nbytes);

error_free:
	g_variant_unref (value);
error_exit:
	if (actual) {
		*actual = nbytes;
	}

	return status;
}

dc_status_t
bluez_ble_characteristic_write (bluez_ble_t *bluez, const bluez_ble_characteristic_t *characteristic, const void *data, size_t size)
{
	dc_status_t status = DC_STATUS_SUCCESS;

	if (characteristic == NULL ||
		((characteristic->flags & (GATT_PROP_READ | GATT_PROP_WRITE_WITHOUT_RESPONSE)) == 0)) {
		ERROR (bluez->context, "Characteristic does not support writing.");
		status = DC_STATUS_INVALIDARGS;
		goto error_exit;
	}

	bluez_object_t *object = bluez_ble_object_find (bluez, BLUEZ_OBJECT_CHARACTERISTIC, characteristic->handle);
	if (object == NULL) {
		char uuidstr[DC_BLE_UUID_SIZE] = {0};
		ERROR (bluez->context, "Failed to find BLE characteristic '%s'.",
			dc_ble_uuid2str (characteristic->uuid, uuidstr, sizeof(uuidstr)));
		status = DC_STATUS_IO;
		goto error_exit;
	}

	const char *type = characteristic->flags & GATT_PROP_WRITE_WITHOUT_RESPONSE ?
		"command" :
		"request";

	GVariant *value = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, data, size, sizeof(unsigned char));

	GVariantBuilder options;
	g_variant_builder_init (&options, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add (&options, "{sv}", "type", g_variant_new_string (type));

	GVariant *params = g_variant_new("(@aya{sv})", value, &options);

	status = bluez_ble_call (bluez,
		object->path,
		BLUEZ_CHARACTERISTIC_IFACE,
		BLUEZ_CHARACTERISTIC_METHOD_VALUE_WRITE,
		params,
		NULL);
	if (status != DC_STATUS_SUCCESS) {
		goto error_exit;
	}

error_exit:
	return status;
}

dc_status_t
bluez_ble_characteristic_notify (bluez_ble_t *bluez, const bluez_ble_characteristic_t *characteristic, bluez_notify_cb_t callback, void *userdata)
{
	dc_status_t status = DC_STATUS_SUCCESS;

	if (characteristic == NULL ||
		((characteristic->flags & (GATT_PROP_NOTIFY | GATT_PROP_INDICATE)) == 0)) {
		ERROR (bluez->context, "Characteristic does not support notifications.");
		return DC_STATUS_INVALIDARGS;
	}

	bluez_object_t *object = bluez_ble_object_find (bluez, BLUEZ_OBJECT_CHARACTERISTIC, characteristic->handle);
	if (object == NULL) {
		char uuidstr[DC_BLE_UUID_SIZE] = {0};
		ERROR (bluez->context, "Failed to find BLE characteristic '%s'.",
			dc_ble_uuid2str (characteristic->uuid, uuidstr, sizeof(uuidstr)));
		return DC_STATUS_IO;
	}

	if (callback) {
		bluez_notify_t *subscription = g_malloc (sizeof(bluez_notify_t));
		if (subscription == NULL) {
			return DC_STATUS_NOMEMORY;
		}

		subscription->callback = callback;
		subscription->userdata = userdata;
		subscription->characteristic = *characteristic;

		g_mutex_lock (&bluez->mutex);
		g_hash_table_insert (bluez->subscriptions, g_strdup (object->path), subscription);
		g_mutex_unlock (&bluez->mutex);
	} else {
		g_mutex_lock (&bluez->mutex);
		g_hash_table_remove (bluez->subscriptions, object->path);
		g_mutex_unlock (&bluez->mutex);
	}

	status = bluez_ble_call (bluez,
		object->path,
		BLUEZ_CHARACTERISTIC_IFACE,
		callback ? BLUEZ_CHARACTERISTIC_METHOD_NOTIFY_START : BLUEZ_CHARACTERISTIC_METHOD_NOTIFY_STOP,
		NULL,
		NULL);
	if (status != DC_STATUS_SUCCESS) {
		return status;
	}

	return status;
}

#endif /* HAVE_GLIB */
