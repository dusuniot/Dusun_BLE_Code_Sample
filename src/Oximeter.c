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


#define UUID_Oximeter_SERVICE  "cdeacb80-5235-4c07-8846-93a37ee6b86d"
#define UUID_Oximeter_TX_CHAR  "cdeacb82-5235-4c07-8846-93a37ee6b86d"
#define UUID_Oximeter_RX_CHAR  "cdeacb81-5235-4c07-8846-93a37ee6b86d"


//bp short for (BloodPressure)
typedef struct OximeterData_
{
	bool data_reported;
	GattCharacteristic * tx_char;
	GattCharacteristic * rx_char;
} OximeterData;

static void Oximeter_init(BluetoothDevice * btdev)
{
	LOG("bp_init()\n");
	OximeterData * data = g_new0(OximeterData, 1);
	btdev->private_data = data;
	keep_report_device(btdev);
}

static void Oximeter_scan_found(BluetoothDevice * btdev)
{
	LOG("scan found\n");
	connect_device(btdev);
}

static void Oximeter_connect_state_changed(BluetoothDevice * btdev, bool connected)
{
	OximeterData  * data = btdev->private_data;
	LOG("connected : %d", connected);
	if (data->tx_char != NULL) {
		unsigned char cmd[] = {0x00, 0xC0, 0x00};
		write_characteristic(data->tx_char, cmd, 3);
	}

	if (data) {
		data->data_reported = false;
	}
}

void Oximeter_characteristics_added (BluetoothDevice * btdev, GattCharacteristic * characteristics)
{
	const char * uuid = get_characteristics_uuid(characteristics);
	OximeterData  * data = btdev->private_data;
	LOG("char uuid: %s", uuid);
	//LOG("bp_characteristics_added\n");
	if (uuid == NULL)
		return;
	if (!strcmp(uuid, UUID_Oximeter_RX_CHAR) ) {
		LOG("start notify");
		data->rx_char = characteristics;
		start_notify(characteristics);
	} else if (!strcmp(uuid, UUID_Oximeter_TX_CHAR)) {
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
void Oximeter_characteristics_notify(BluetoothDevice * btdev, GattCharacteristic * characteristics,
				 const unsigned char * value, int len)
{
	char  txt[100];
	char * p = txt;
	int i = 0;

	OximeterData  * data = btdev->private_data;
	for (i=0; i<len; i++) {
		p += sprintf(p, "%02x ", value[i]);
	}

	LOG("Oximeter_characteristics_notify: %s", txt);

	if (data->data_reported == false) {
		data->data_reported = true;
		unsigned char cmd[] = {0x3C, 0x41, 0x01, 02};
		unsigned char clear_bonding_cmd[] = {0x01, 0x41, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00};
		//write_characteristic(data->tx_char, cmd, 4);
		write_characteristic(data->tx_char, clear_bonding_cmd, 15);
	}
	
}

static BluetoothDeviceCallbacks Oximeter_callbacks =
{
	.init = Oximeter_init,
	.connect_state_changed = Oximeter_connect_state_changed,
	.scan_found = Oximeter_scan_found ,
	.characteristics_added = Oximeter_characteristics_added,
	.characteristics_notify = Oximeter_characteristics_notify,
};

HealthDriver   Oximeter_driver =
{
	.name = "Oximeter detector",
 	.service_uuid =  UUID_Oximeter_SERVICE,
 	.callbacks = &Oximeter_callbacks,
};
/*
void add_blood_pressure_device(const char * address)
{
	add_health_device(address, &blood_pressure_driver);
}*/
