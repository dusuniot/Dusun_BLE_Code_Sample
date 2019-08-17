#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <stdbool.h>
#include <glib.h>
#include "gdbus/gdbus.h"
#include "monitor/uuid.h"
#include "curl.h"
#include "util.h"
#include "dev.h"
#include "log.h"

#define RESTART_SCAN_TIMEOUT  (1 * 60 * 60)

typedef struct GlobalData_
{
	char       dev_mac[30];
	guint      restart_scan_timeout;
	GList  *  driver_list;
	GList  *  dev_list;  //all BluetoothDevice list
	GDBusProxy * controller;
} GlobalData;

static GlobalData  gd;

static gboolean restart_scan_timeout_cb(gpointer user_data);
bool remove_proxy(GDBusProxy  * proxy);
void init_global_data()
{
	int len;
	memset(&gd, 0, sizeof(gd));
	len = get_line_from_file("/sys/class/net/eth0/address", gd.dev_mac, sizeof(gd.dev_mac));
	if (len >= 17 ) {
		string_to_upper(gd.dev_mac);
	}
	LOG("Gateway mac: %s", gd.dev_mac);

	gd.restart_scan_timeout = g_timeout_add_seconds(RESTART_SCAN_TIMEOUT,
						restart_scan_timeout_cb,  NULL);
}

void register_health_driver(HealthDriver * driver)
{
	gd.driver_list = g_list_append(gd.driver_list, driver);
}

bool add_health_device(const char * address, HealthDriver * driver)
{
	BluetoothDevice * dev = g_new0(BluetoothDevice, 1);
	dev->address = g_strdup(address);
	dev->driver = driver;
	gd.dev_list = g_list_append(gd.dev_list, dev);
	return true;
}

#define DATA_LED  "yellow"
static void shot_data_led()
{
	led_shot(DATA_LED);
}

static HealthDriver *  find_driver_by_uuid(const char *uuid)
{
	GList * l;

	for (l=gd.driver_list; l; l=g_list_next(l))  {
		HealthDriver * driver = l->data;
		if (!driver) {
			continue;
		} 
		if (strcmp(driver->service_uuid, uuid) == 0 ) {
			LOG("driver->name:%s, driver->UUID:%s, uuid:%s", driver->name, driver->service_uuid, uuid);
			return driver;
		}
	}
	return NULL;
}

BluetoothDevice * find_device_by_address(const char * address);
static bool probe_device_driver (GDBusProxy * proxy)
{
	DBusMessageIter iter;
	DBusMessageIter subiter;
	const char * address, *uuid;
	HealthDriver * driver;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return false;
	dbus_message_iter_get_basic(&iter, &address);

	if (g_dbus_proxy_get_property(proxy, "UUIDs", &iter) == FALSE)
		return false;

	dbus_message_iter_recurse(&iter, &subiter);
	while (dbus_message_iter_get_arg_type(&subiter) !=
							DBUS_TYPE_INVALID) {
		dbus_message_iter_get_basic(&subiter, &uuid);
		//LOG("UUID : %s", uuid);
		driver = find_driver_by_uuid(uuid);
		if (driver != NULL) {
			LOG("Found driver: (%s) for dev: %s", driver->name, address);
			add_health_device(address,  driver);
			BluetoothDevice * dev = find_device_by_address(address);
			if (dev)
				dev->auto_added = true;
			return true;
		}
		dbus_message_iter_next(&subiter);
	}

	return false;
}

BluetoothDevice * find_device_by_address(const char * address)
{
	GList * l;
	for (l=gd.dev_list; l; l=g_list_next(l))  {
		BluetoothDevice * dev = l->data;
		if (!strcmp(dev->address, address) )
			return dev;
	}
	return NULL;
}

BluetoothDevice * find_device_by_path(const char * dev_path)
{
	GList  *l;
	const char * path;
	for (l=gd.dev_list; l; l=g_list_next(l)) {
		BluetoothDevice * dev = l->data;
		if (dev->proxy == NULL)
			continue;
		path = g_dbus_proxy_get_path(dev->proxy);
		if (!strcmp(path, dev_path) )
			return dev;
	}
	return NULL;
}


static BluetoothDevice * find_device_of_characteristic(GattCharacteristic * proxy)
{
	DBusMessageIter iter;
	GList * l;
	const char * srv_path, * path;

	if (g_dbus_proxy_get_property(proxy, "Service", &iter) == FALSE)
			return NULL;
	path = g_dbus_proxy_get_path(proxy);

	for (l=gd.dev_list; l; l=g_list_next(l)){
		BluetoothDevice * dev = l->data;
		if (dev && dev->service_proxy) {
			srv_path = g_dbus_proxy_get_path(dev->service_proxy);
			if (g_str_has_prefix(path, srv_path) ) {
				return dev;
			}
		}
	}
	return NULL;
}

extern void print_iter(const char *label, const char *name,
						DBusMessageIter *iter);


static bool probe_device(GDBusProxy * proxy)
{
	DBusMessageIter iter;
	const char *address;

	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return false;
	dbus_message_iter_get_basic(&iter, &address);
	BluetoothDevice * dev = find_device_by_address(address);

	if (dev == NULL) {
		if (probe_device_driver(proxy) ) {
			dev = find_device_by_address(address);
		} else
			return false;
	}

	if (dev != NULL && (dev->proxy == NULL || dev->proxy != proxy) ) {
		dev->proxy = proxy;
		CALL_CALLBACK(dev, init);
	}
	return true;
}

static void remove_device(GDBusProxy * proxy)
{
	DBusMessageIter iter;
	const char *address;
	if (g_dbus_proxy_get_property(proxy, "Address", &iter) == FALSE)
		return;
	dbus_message_iter_get_basic(&iter, &address);
	BluetoothDevice * dev = find_device_by_address(address);
	if (dev == NULL)
		return;

	dev->proxy = NULL;
	dev->service_proxy = NULL;
	CALL_CALLBACK(dev, exit);
	LOG("Remove dev:  %s  type: %s", dev->address, dev->driver->name);
	if (dev->auto_added) {
		gd.dev_list = g_list_remove(gd.dev_list,  dev);
		g_free(dev);
	}
}

static bool add_service(GDBusProxy * proxy)
{
	DBusMessageIter iter;
	const char * dev_path, *uuid;
	if (g_dbus_proxy_get_property(proxy, "Device", &iter) == FALSE)
			return false;

	dbus_message_iter_get_basic(&iter, &dev_path);

	if (g_dbus_proxy_get_property(proxy, "UUID", &iter) == FALSE)
			return false;

	dbus_message_iter_get_basic(&iter, &uuid);
	BluetoothDevice * dev = find_device_by_path(dev_path);
	if (dev == NULL )
		return false;

	if (!dev->driver || strcmp(dev->driver->service_uuid, uuid) )
		return false;
	dev->service_proxy= proxy;
	return true;
}

static bool add_characteristic(GDBusProxy * proxy)
{
	BluetoothDevice  * dev = find_device_of_characteristic(proxy);
	if (dev != NULL)
		CALL_CALLBACK(dev, characteristics_added, proxy);
	return true;
}

static void start_discovery_reply(DBusMessage *message, void *user_data)
{
	DBusError error;
	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {
		LOG("Failed to start discovery: %s",error.name);
		dbus_error_free(&error);
		return;
	}
	LOG("Discovery started");
}

bool start_scan(GDBusProxy * ctrl)
{
	if (g_dbus_proxy_method_call(ctrl, "StartDiscovery",
				NULL, start_discovery_reply,
				NULL, NULL) == FALSE)
		return false;
	return true;
}

static void  stop_discovery_reply(DBusMessage *message, void *user_data)
{
	DBusError error;
	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {
		LOG("Failed to start discovery: %s",error.name);
		dbus_error_free(&error);
		return;
	}
	LOG("Discovery stopped");
}

bool stop_scan(GDBusProxy * ctrl)
{
	if (g_dbus_proxy_method_call(ctrl, "StopDiscovery",
				NULL, stop_discovery_reply,
				NULL, NULL) == FALSE)
		return false;
	return true;
}

static gboolean restart_scan_timeout_cb(gpointer user_data)
{
	LOG("Restart scan");
	if (gd.controller)
		stop_scan(gd.controller);
	return TRUE;
}

static void power_rely(const DBusError *error, void *user_data)
{
	LOG("Power ok");
	if (gd.controller)
		start_scan(gd.controller);
}

static bool power_controller(GDBusProxy * ctrl)
{
	dbus_bool_t powered = true;
	if (g_dbus_proxy_set_property_basic(gd.controller, "Powered",
					DBUS_TYPE_BOOLEAN, &powered,
					power_rely, "power",  NULL) == TRUE)
		return true;
	else
		return false;
}

static void device_connect_reply(DBusMessage *message, void *user_data)
{
	BluetoothDevice * dev = user_data;
	DBusError error;
	dbus_error_init(&error);

	if (dev)
		dev->connecting = false;

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		LOG("Failed to connect: %s", error.name);
		dbus_error_free(&error);
		return;
	}
	LOG("Connection successful");
}

bool device_is_paired(BluetoothDevice * dev)
{
	DBusMessageIter iter;
	if (g_dbus_proxy_get_property(dev->proxy, "Connected", &iter)) {
			dbus_bool_t connected;
			dbus_message_iter_get_basic(&iter, &connected);
			return connected;
	}
	return false;
}

bool device_is_contected(BluetoothDevice * dev)
{
	DBusMessageIter iter;
	if (g_dbus_proxy_get_property(dev->proxy, "Connected", &iter)) {
		dbus_bool_t connected;
		dbus_message_iter_get_basic(&iter, &connected);
		return connected;
	}
	return false;
}

const char *   get_characteristics_uuid(GattCharacteristic * chr)
{
	DBusMessageIter iter;
	const char *uuid;
	if (g_dbus_proxy_get_property(chr, "UUID", &iter) == FALSE)
			return NULL;
	dbus_message_iter_get_basic(&iter, &uuid);
	return uuid;
}

bool connect_device(BluetoothDevice * dev)
{
	if (!dev || !dev->proxy)
		return false;

	if (device_is_contected(dev) )
		return false;

	if (dev->connecting || dev->proxy == NULL)
		return false;

	if (g_dbus_proxy_method_call(dev->proxy, "Connect", NULL, device_connect_reply,
							dev, NULL) == FALSE) {
		LOG("Failed to connect");
		return false;
	}
	dev->connecting  = true;
}


static void write_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		LOG("DUSUN-Failed to write: %s\n", error.name);
		dbus_error_free(&error);
		return;
	}
}

static void write_setup(DBusMessageIter *iter, void *user_data)
{
	struct iovec *iov = user_data;
	DBusMessageIter array;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "y", &array);
	dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE,
						&iov->iov_base, iov->iov_len);
	dbus_message_iter_close_container(iter, &array);
}

//static void write_attribute(GDBusProxy *proxy, char *arg)
void write_characteristic(GattCharacteristic * chr, unsigned char * value, int len)
{
	struct iovec iov;
	iov.iov_base = value;
	iov.iov_len = len;

	if (g_dbus_proxy_method_call(chr, "WriteValue", write_setup,
					write_reply, &iov, NULL) == FALSE) {
		LOG("Failed to write\n");
		return;
	}
	LOG("Attempting to write char\n");
}


bool keep_report_device(BluetoothDevice * dev)
{
	if (dev->proxy == NULL)
		return false;

	if (g_dbus_proxy_method_call(dev->proxy, "KeepReport", NULL, NULL,
							NULL, NULL) == FALSE) {
		LOG("Failed to keep report");
		return false;
	}
	return false;
}

static void start_notify_reply(DBusMessage *message, void *user_data)
{
	DBusError error;
	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {
		LOG("Failed to start notify: %s", error.name);
		dbus_error_free(&error);
		return;
	}
	LOG("Notify started");
}

void start_notify(GattCharacteristic * chr)
{
	if (g_dbus_proxy_method_call(chr, "StartNotify", NULL, start_notify_reply,
				 NULL, NULL) == FALSE)  {
		LOG("Failed to start notify");
	}
}

bool add_proxy(GDBusProxy * proxy)
{
	const char * interface;

	interface = g_dbus_proxy_get_interface(proxy);
	//LOG("------------------------------------------------");
	LOG("add proxy: %s", interface);
	if (!strcmp(interface, "org.bluez.Device1")) {
		probe_device(proxy);
	}else if (!strcmp(interface, "org.bluez.GattService1") ) {
		add_service(proxy);
	} else if (!strcmp(interface, "org.bluez.GattCharacteristic1")) {
		add_characteristic(proxy);
	} else if (!strcmp(interface, "org.bluez.Adapter1")) {
		gd.controller = proxy;
		power_controller(proxy);
	}
}

bool remove_proxy(GDBusProxy  * proxy)
{
	const char * interface;

	interface = g_dbus_proxy_get_interface(proxy);
	if (!strcmp(interface, "org.bluez.Device1")) {
		remove_device(proxy);
	}
}

void property_changed_handler(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);
	if (!strcmp(interface, "org.bluez.Adapter1")) {
		if (!strcmp(name, "Powered")) {
			dbus_bool_t powered;
			dbus_message_iter_get_basic(iter, &powered);
			if (powered && gd.controller)
				start_scan(gd.controller);
		} else if (!strcmp(name, "Discovering") )  {
			dbus_bool_t discov;
			dbus_message_iter_get_basic(iter, &discov);
			if (!discov && gd.controller)
				start_scan(gd.controller);
		}
	} else if (!strcmp(interface, "org.bluez.Device1")) {
			DBusMessageIter addr_iter;
			const char *address;
			if (!g_dbus_proxy_get_property(proxy, "Address", &addr_iter) )
				return;
			dbus_message_iter_get_basic(&addr_iter,&address);
			if (!strcmp(name, "UUIDs") ) {
				probe_device(proxy);
			} else if (!strcmp(name, "RSSI") ) {
				LOG("RSSI Changed, Scan found");
				BluetoothDevice * dev = find_device_by_address(address);
				if (dev != NULL) {
					CALL_CALLBACK(dev, scan_found);
					shot_data_led();
				}
			} else if (!strcmp(name, "Connected") ) {
				dbus_bool_t connected;
				dbus_message_iter_get_basic(iter, &connected);
				BluetoothDevice * dev = find_device_by_address(address);
				if (dev != NULL)
					CALL_CALLBACK(dev, connect_state_changed, connected);
			}
	}else if (!strcmp(interface, "org.bluez.GattCharacteristic1")
			&& !strcmp(name, "Value")) {

		BluetoothDevice * dev = find_device_of_characteristic(proxy);
		if (dev == NULL)
			return;

		DBusMessageIter  array;
		uint8_t *value;
		int len;
		dbus_message_iter_recurse(iter, &array);
		dbus_message_iter_get_fixed_array(&array, &value, &len);
		CALL_CALLBACK(dev, characteristics_notify, proxy, value, len);
		shot_data_led();
	}
}

const char * get_gateway_mac()
{
	return gd.dev_mac;
}

#define UPLOAD_URL "http://121.196.244.25:7070/services/RestServices/yundihealth/bluetooth/uploaddata"
const char * gw_mac = "30:AE:7B:00:00:13";
static int data_sn = 1;
void upload_health_data(BluetoothDevice * btdev, const char * dev_type,
	 		const char * value, cur_post_callback_func_t cb, void * user_data)
{
	char buf[100];
	const char * url = UPLOAD_URL;
	time_t now = time(NULL);
	json_object * obj = json_object_new_object();
	json_object * dataAry = json_object_new_array();
	json_object * data = json_object_new_object();

	json_add_string(obj, "gsn", "");
	json_add_string(obj, "gsnmac", gd.dev_mac);
	json_add_string(obj, "deviceSn", "");
	json_add_string(obj, "devicemacaddress", btdev->address);
	json_add_string(obj, "deviceType", dev_type);
	json_add_string(obj, "timeZone", "GMT+8");
	sprintf(buf,  "%d", data_sn++);
	json_add_string(obj, "dataSn", buf);
	sprintf(buf, "%lu000", now);
	json_add_string(obj, "gsnTime",  buf);
	json_add_string(data, "measuretime", buf);
	json_add_string(data, "value", value);
	json_object_array_add(dataAry, data);
	json_object_object_add(obj, "data", dataAry);

	const char * json_string = json_object_to_json_string(obj);

	curl_post_json(url, json_string, cb, user_data);
	json_object_put(obj);
}

