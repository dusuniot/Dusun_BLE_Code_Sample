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

#define UUID_PM25_SERVICE  "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define UUID_PM25_TX_CHAR  "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define UUID_PM25_RX_CHAR  "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


//bp short for (BloodPressure)
typedef struct Pm25Data_
{
	bool data_reported;
	GattCharacteristic * tx_char;
	GattCharacteristic * rx_char;
} Pm25Data;

static void pm25_init(BluetoothDevice * btdev)
{
	LOG("bp_init()\n");
	Pm25Data * data = g_new0(Pm25Data, 1);
	btdev->private_data = data;
	keep_report_device(btdev);
}

static void pm25_scan_found(BluetoothDevice * btdev)
{
	LOG("scan found\n");
	connect_device(btdev);
}

static void pm25_connect_state_changed(BluetoothDevice * btdev, bool connected)
{
	Pm25Data  * data = btdev->private_data;
	LOG("connected : %d", connected);
	if (data->tx_char != NULL) {
		unsigned char cmd[] = {0x00, 0xC0, 0x00};
		write_characteristic(data->tx_char, cmd, 3);
	}

	if (data) {
		data->data_reported = false;
	}
}

void pm25_characteristics_added (BluetoothDevice * btdev, GattCharacteristic * characteristics)
{
	const char * uuid = get_characteristics_uuid(characteristics);
	Pm25Data  * data = btdev->private_data;
	LOG("char uuid: %s", uuid);
	//LOG("bp_characteristics_added\n");
	if (uuid == NULL)
		return;
	if (!strcmp(uuid, UUID_PM25_RX_CHAR) ) {
		LOG("start notify");
		data->rx_char = characteristics;
		start_notify(characteristics);
	} else if (!strcmp(uuid, UUID_PM25_TX_CHAR)) {
		LOG("Found Tx char");
		data->tx_char = characteristics;
	}
}
/*
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
*/
void pm25_characteristics_notify(BluetoothDevice * btdev, GattCharacteristic * characteristics,
				 const unsigned char * value, int len)
{
	char  txt[100];
	char * p = txt;
	int i = 0;

	Pm25Data  * data = btdev->private_data;
	for (i=0; i<len; i++) {
		p += sprintf(p, "%02x ", value[i]);
	}

	LOG("pm25_characteristics_notify: %s", txt);

	if (data->data_reported == false) {
		data->data_reported = true;
		unsigned char cmd[] = {0x3C, 0x41, 0x01, 02};
		unsigned char clear_bonding_cmd[] = {0x01, 0x41, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00};
		//write_characteristic(data->tx_char, cmd, 4);
		write_characteristic(data->tx_char, clear_bonding_cmd, 15);
	}
	/*
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
	}*/
}

static BluetoothDeviceCallbacks pm25_callbacks =
{
	.init = pm25_init,
	.connect_state_changed = pm25_connect_state_changed,
	.scan_found = pm25_scan_found ,
	.characteristics_added = pm25_characteristics_added,
	.characteristics_notify = pm25_characteristics_notify,
};

HealthDriver   pm25_driver =
{
	.name = "pm25 detector",
 	.service_uuid =  UUID_PM25_SERVICE,
 	.callbacks = &pm25_callbacks,
};
/*
void add_blood_pressure_device(const char * address)
{
	add_health_device(address, &blood_pressure_driver);
}*/
