#ifndef DUSUN_DEV_H
#define DUSUN_DEV_H
#include "curl.h"
//=========new added===========

typedef struct BluetoothDeviceCallbacks_ BluetoothDeviceCallbacks;

typedef struct HealthDriver_
{
	char * name;
	char * service_uuid;
	struct BluetoothDeviceCallbacks_ * callbacks;
}HealthDriver;

typedef GDBusProxy GattCharacteristic;
typedef GDBusProxy GattService;
typedef  struct BluetoothDevice_
{
	char * address;
	HealthDriver * driver;
	GDBusProxy * proxy;
	GDBusProxy * service_proxy;

	bool  connecting;
	bool    auto_added;
	void * private_data;
} BluetoothDevice;


#define CALL_CALLBACK(dev, func, args...) \
	do {   \
		if (dev != NULL && dev->driver != NULL)	{\
			if (dev->driver->callbacks->func)  {\
				dev->driver->callbacks->func(dev, ##args); \
			} \
		}  \
	} while (0);




typedef struct BluetoothDeviceCallbacks_
{
	void (*init) (BluetoothDevice * btdev);
	void (*exit) (BluetoothDevice * btdev);
	void (*scan_found)(BluetoothDevice * btdev);
	void (*connect_state_changed) (BluetoothDevice * btdev, bool connected);
	void (*service_added) (BluetoothDevice * btdev, GattService * service);
	void (*characteristics_added) (BluetoothDevice * btdev, GattCharacteristic * characteristics);
	void (*characteristics_notify)(BluetoothDevice * btdev, GattCharacteristic * characteristics,
				 const unsigned char * value, int len);
	//void (*property_changed)();
}  BluetoothDeviceCallbacks;




void init_global_data();
bool add_proxy(GDBusProxy * proxy);
const char *   get_characteristics_uuid(GattCharacteristic * chr);
bool connect_device(BluetoothDevice * dev);
void write_characteristic(GattCharacteristic * chr, unsigned char * value, int len);
bool keep_report_device(BluetoothDevice * dev);
void property_changed_handler(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter, void *user_data);

bool add_health_device(const char * address, HealthDriver * driver);
void register_health_driver(HealthDriver * driver);
const char * get_gateway_mac();
void upload_health_data(BluetoothDevice * btdev, const char * dev_type,
	 		const char * value, cur_post_callback_func_t cb, void * user_data);
#endif