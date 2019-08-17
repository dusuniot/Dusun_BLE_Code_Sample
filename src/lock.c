#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <glib.h>
#include "gdbus/gdbus.h"
#include "monitor/uuid.h"
#include "curl.h"
#include "dev.h"
#include "util.h"
#include "log.h"
#include "bp.h"

#define UUID_LOCK_SERVICE  "0000fff0-0000-1000-8000-00805f9b34fb"
#define UUID_LOCK_TX_CHAR  "0000fff2-0000-1000-8000-00805f9b34fb"
#define UUID_LOCK_RX_CHAR  "0000fff1-0000-1000-8000-00805f9b34fb"

//bp short for (BloodPressure)
typedef struct LockData_
{
	bool data_reported;
	GattCharacteristic * tx_char;
	GattCharacteristic * rx_char;
} LockData;

static void lock_init(BluetoothDevice * btdev)
{
	LOG("lock_init()\n");
	LockData * data = g_new0(LockData, 1);
	btdev->private_data = data;
	keep_report_device(btdev);
}

static void lock_scan_found(BluetoothDevice * btdev)
{
	LOG("scan found\n");
	connect_device(btdev);
}

/*
guint
g_timeout_add (guint interval,
               GSourceFunc function,
               gpointer data);
*/
gboolean connect_timer_cb(gpointer user_data)
{
	LOG("connect_timer_cb: %d", g_thread_self());
	static unsigned char test = 0;
	BluetoothDevice * dev = (BluetoothDevice * ) user_data;
	LockData * data = dev->private_data;
	if (data->tx_char != NULL) {
		unsigned char cmd[20];
		memset(cmd, test, 20);
		test++;
		write_characteristic(data->tx_char, cmd, 20);
		return TRUE;
	} else
		return TRUE;
}
static void lock_connect_state_changed(BluetoothDevice * btdev, bool connected)
{
	LockData  * data = btdev->private_data;
	LOG("connected : %d, thread: %d", connected, g_thread_self());
	/*if (data->tx_char != NULL && connected) {
		unsigned char cmd[20];
		write_characteristic(data->tx_char, cmd, 2);
	}*/

	if (data) {
		data->data_reported = false;
	}

	g_timeout_add(1000, connect_timer_cb, btdev);
}

void lock_characteristics_added (BluetoothDevice * btdev, GattCharacteristic * characteristics)
{
	const char * uuid = get_characteristics_uuid(characteristics);
	LockData  * data = btdev->private_data;
	LOG("char uuid: %s %s", uuid, g_dbus_proxy_get_path(characteristics));
	if (uuid == NULL)
		return;
	if (!strcmp(uuid, UUID_LOCK_RX_CHAR) ) {
		LOG("start notify");
		data->rx_char = characteristics;
		start_notify(characteristics);
	} else if (!strcmp(uuid, UUID_LOCK_TX_CHAR)) {
		LOG("Found Tx char");
		data->tx_char = characteristics;
	}
}

void lock_characteristics_notify(BluetoothDevice * btdev, GattCharacteristic * characteristics,
				 const unsigned char * value, int len)
{
	char  txt[100];
	char * p = txt;
	int i = 0;

	LockData  * data = btdev->private_data;
	for (i=0; i<len; i++) {
		p += sprintf(p, "%02x ", value[i]);
	}

	LOG("lock_characteristics_notify: %s", txt);

	if (data->data_reported == false) {
		data->data_reported = true;
		unsigned char cmd[] = {0x3C, 0x41, 0x01, 02};
		unsigned char clear_bonding_cmd[] = {0x01, 0x41, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00};
		//write_characteristic(data->tx_char, cmd, 4);
		write_characteristic(data->tx_char, clear_bonding_cmd, 15);
	}
}

static BluetoothDeviceCallbacks lock_callbacks =
{
	.init = lock_init,
	.connect_state_changed = lock_connect_state_changed,
	.scan_found = lock_scan_found ,
	.characteristics_added = lock_characteristics_added,
	.characteristics_notify = lock_characteristics_notify,
};

HealthDriver   lock_driver =
{
	.name = "HuoHe bluetooth lock",
 	.service_uuid =  UUID_LOCK_SERVICE,
 	.callbacks = &lock_callbacks,
};
