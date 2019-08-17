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

#define UUID_BLOOD_PRESSURE_SERVICE  "0000fff0-0000-1000-8000-00805f9b34fb"
#define UUID_BLOOD_PRESSURE_CHAR       "0000fff4-0000-1000-8000-00805f9b34fb"

//bp short for (BloodPressure)
typedef struct BloodPressureData_
{
	bool data_reported;
} BloodPressureData;

static void bp_init(BluetoothDevice * btdev)
{
	LOG("bp_init()\n");
	BloodPressureData * data = g_new0(BloodPressureData, 1);
	btdev->private_data = data;
	keep_report_device(btdev);
}

static void bp_scan_found(BluetoothDevice * btdev)
{
	LOG("scan found\n");
	connect_device(btdev);
}

static void bp_connect_state_changed(BluetoothDevice * btdev, bool connected)
{
	BloodPressureData  * data = btdev->private_data;
	if (data) {
		data->data_reported = false;
	}
}

void bp_characteristics_added (BluetoothDevice * btdev, GattCharacteristic * characteristics)
{
	const char * uuid = get_characteristics_uuid(characteristics);

	LOG("bp_characteristics_added\n");
	if (uuid == NULL)
		return;
	if (!strcmp(uuid, UUID_BLOOD_PRESSURE_CHAR) ) {
		start_notify(characteristics);
	}
}

static void post_data_callback(bool res, const char * response, void * user_data)
{
	if ( res == false ) {
		LOG("POST FAILED");
	} else {
		LOG("POST OK");
		LOG("GOT Respone from Server");
		LOG("=======>>");
		LOG("%s", response);
		LOG("<<=======");
		led_shot("errled"); //TODO:
	}
}

void bp_characteristics_notify(BluetoothDevice * btdev, GattCharacteristic * characteristics,
				 const unsigned char * value, int len)
{
	BloodPressureData  * data = btdev->private_data;
	static bool post = false;

	int i = 0;
	for (i=0; i<len; i++) {
		printf("%02X ", value[i]);
	}
	printf("\n");

	if (value[0] == 0x0C && !data->data_reported) {
		char report_data[100];
		int val1 = value[1] * 256 + value[2];
		int val2 = value[3] * 256 + value[4];
		int val3 = value[7] * 256 + value[8];
		LOG("BloadPressue: %d,%d,%d\n", val1, val2, val3);
		sprintf(report_data, "%d,%d,%d", val1, val2, val3);
		upload_health_data(btdev, "1", report_data, post_data_callback, btdev);
		data->data_reported = true;
	} else if (value[0] == 0x20) {
		data->data_reported =false;
	}
}

static BluetoothDeviceCallbacks blood_pressure_callbacks =
{
	.init = bp_init,
	.connect_state_changed = bp_connect_state_changed,
	.scan_found = bp_scan_found ,
	.characteristics_added = bp_characteristics_added,
	.characteristics_notify = bp_characteristics_notify,
};

HealthDriver   blood_pressure_driver =
{
	.name = "blood_pressure",
 	.service_uuid =  UUID_BLOOD_PRESSURE_SERVICE,
 	.callbacks = &blood_pressure_callbacks,
};

void add_blood_pressure_device(const char * address)
{
	add_health_device(address, &blood_pressure_driver);
}
