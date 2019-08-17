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
#include "wt.h"

#define UUID_WEIGHT_SERVICE "0000feb3-0000-1000-8000-00805f9b34fb"
#define UUID_WEIGHT_WRITE    "0000fed5-0000-1000-8000-00805f9b34fb"
#define UUID_WEIGHT_IND    	 "0000fed6-0000-1000-8000-00805f9b34fb"

typedef struct WeightData_
{
	GattCharacteristic * write_chr;
	GattCharacteristic * ind_chr;

	unsigned char sv; //software version
	unsigned char sd; //distinguishability 0x00: 0.1kg 0x01: 0.01kg
	unsigned char bv; //bluetooth version
	unsigned char ss; //state: 0x00: powered on 0x01: powered off

	bool data_reported;
	bool wait_user_data;
	bool get_user_data;

	int age;
	int gender;
	int height;
	int user_weight; //weight get by getuserinfo

	int weight;
	int fat;
	int tbw;
	int mus;
	int bone;
} WeightData;

static void get_user_data(BluetoothDevice * btdev);
static void wt_init(BluetoothDevice * btdev)
{
	LOG("wt_init()\n");
	WeightData * data = g_new0(WeightData, 1);
	btdev->private_data = data;
	data->age = 20;
	data->height = 170;
	data->gender = 0;
	data->user_weight = 65;
	get_user_data(btdev);
	keep_report_device(btdev);
}

static void wt_exit(BluetoothDevice *btdev)
{
	LOG("wt_exit");
	if (btdev && btdev->private_data) {
		g_free(btdev->private_data);
	}
}

static void wt_scan_found(BluetoothDevice * btdev)
{
	LOG("wt scan found\n");
	connect_device(btdev);
}

static void wt_connect_state_changed(BluetoothDevice * btdev, bool connected)
{
	WeightData  * data = btdev->private_data;
	if (data) {
		data->data_reported = false;
		data->wait_user_data = false;
	}
	LOG("WT connected changed: %d", connected);
}

void wt_characteristics_added (BluetoothDevice * btdev, GattCharacteristic * characteristics)
{
	WeightData  * data = btdev->private_data;
	const char * uuid = get_characteristics_uuid(characteristics);
	if (uuid == NULL)
		return;
	if (!strcmp(uuid, UUID_WEIGHT_IND) ) {
		data->ind_chr = characteristics;
		start_notify(characteristics);
	} else if (!strcmp(uuid, UUID_WEIGHT_WRITE)) {
		data->write_chr = characteristics;
	}
}

static unsigned char  checksum(const unsigned char * val, int len)
{
	unsigned char sum = 0;
	int i =0;
	for(i=0; i<len; i++) {
		sum += val[i];
	}
	return sum;
}

static void confirm_data_received(BluetoothDevice * btdev)
{
	WeightData  * data = btdev->private_data;
	if (data == NULL || data->write_chr == NULL)
		return;
	unsigned char rpt[8] ;
	rpt[0] = 0x02;
	rpt[1] = 0x00; //header
	rpt[2] = 0x05; //length
	rpt[3] = 0x1F;  //cmd
	rpt[4] = 0x05; //length
	rpt[5] = 0x15;  //cmd
	rpt[6] = 0x10;  //cmd
	rpt[7] = 0x49;  //cmd
	write_characteristic(data->write_chr, rpt, 12);
}

//height: cm
static void set_user_data(BluetoothDevice * btdev, unsigned char height,
		unsigned char age, unsigned char gender) {
	WeightData  * data = btdev->private_data;

	if (data == NULL || data->write_chr == NULL)
		return;
	//unsigned char rpt[]= {0x02,0x00,0x09,0x13,0x09,0x15,0x01,0x10,0xAA,0x20,0x00,0x0C};
	unsigned char rpt[12];
	rpt[0] = 0x02;
	rpt[1] = 0x00; //header
	rpt[2] = 0x09; //length
	rpt[3] = 0x13;  //cmd
	rpt[4] = 0x09; //length
	rpt[5] = 0x15; //constant
	rpt[6] = 0x01; //kg
	rpt[7] = 20;  //display on time
	rpt[8] = height;
	rpt[9] = age;
	rpt[10] = gender;
	rpt[11] = checksum(rpt+3, 8);
	write_characteristic(data->write_chr, rpt, 12);
 }

static const char * gw_mac ="30:AE:7B:00:00:13";

/*
{"syb_status":"000","syb_info":"Success.","syb_providerSeqNo":"1aff86c2-8760-46a8-a122-66363fc44eb0","syb_consumerSeqNo":"","syb_sessionToken":"","syb_data":{"age":"29","gender":"0","height":"170.0","weight":"70.0"}}
*/
static bool parse_user_info(WeightData * data, const char * response)
{
	json_object * obj = json_tokener_parse(response);
	if (obj == NULL)
		return false;

	const char * status = json_get_string(obj, "syb_status");
	if (status == NULL || strcmp(status, "000") )
		return false;

	json_object * syb_data = json_get_object(obj, "syb_data");
	if (syb_data == NULL) {
		json_object_put(obj);
		return false;
	}
	const char * tmp = json_get_string(syb_data, "age");
	if (tmp != NULL && strlen(tmp) > 0 )
		data->age = atoi(tmp);

	tmp = json_get_string(syb_data, "gender");
	if (tmp != NULL && strlen(tmp) > 0) {
		data->gender = atoi(tmp);
	}

	tmp = json_get_string(syb_data, "height");
	if (tmp != NULL && strlen(tmp) > 0) {
		data->height = atoi(tmp);
	}

	tmp = json_get_string(syb_data, "weight");
	if (tmp != NULL && strlen(tmp) > 0) {
		data->user_weight = atoi(tmp);
	}

	LOG("WT: getuserinfo: age:%d gender: %d height: %d weight: %d", data->age,
				data->gender, data->height, data->user_weight);
	json_object_put(obj);
}

static void get_user_data_callback(bool res, const char * response, void * user_data)
{
	BluetoothDevice * btdev = user_data;
	if (btdev == NULL )
		return;

	WeightData * data = btdev->private_data;
	if (data == NULL )
		return;

	data->get_user_data = false;
	if (res &&  response != NULL)
	{
		LOG("GOT Respone from Server\n%s", response);
		parse_user_info(data, response);
	} else
	{
		LOG("GetUserData Failed");
	}

	if (data->wait_user_data) {
		set_user_data(btdev, data->height, data->age, data->gender);
	}
}
static void get_user_data(BluetoothDevice * btdev)
{
	char * url = "http://121.196.244.25:7070/services/RestServices/yundihealth/bluetooth/getuserinfo";
	WeightData * wt_data = btdev->private_data;
	if (wt_data == NULL)
		return;

	if (wt_data->get_user_data)
		return;
	wt_data->get_user_data = true;

	json_object * obj = json_object_new_object();
	json_object * dataAry = json_object_new_array();
	json_object * data = json_object_new_object();

	json_add_string(obj, "gsn", "");
	json_add_string(obj, "gsnmac", get_gateway_mac());
	json_add_string(obj, "deviceSn", "");
	json_add_string(obj, "devicemacaddress", btdev->address);
	const char * json_string =json_object_to_json_string(obj);
	curl_post_json(url, json_string, get_user_data_callback, btdev);
	json_object_put(obj);
}

static void upload_data_callback(bool res, const char * response, void * user_data)
{
	if ( res == false ) {
		LOG("POST FAILED");
	} else {
		LOG("POST OK");
		LOG("GOT Respone from Server");
		LOG("=======>>");
		LOG("%s", response);
		LOG("<<=======");
		led_shot("errled");
	}
}

//02 00 0F 12 0F 15 EB 3E ED 1C F5 CB 05 00 03 00 00 30
 //02 00 0B 14 0B 15 00 00 01 00 00 00 00 35
static void wt_characteristics_notify(BluetoothDevice * btdev, GattCharacteristic * characteristics,
				 const unsigned char * value, int len)
{
	WeightData  * data = btdev->private_data;

	if (data == NULL)
		return;

	static flag = false;
	int i = 0;
	for (i=0; i<len; i++) {
		printf("%02X ", value[i]);
	}
	printf("\n");

	if (len < 6)
		return;

	if (value[0] != 0x02 || value[1] != 0x00 )
		return;
	if (value[2] != value[4]  || (value[2] + 3)  != len) //value2/value4 is length
		return;
	unsigned char sum = checksum(value+3, len-4);
	if (sum != value[len-1] )
		return;
	unsigned char cmd = value[3];
	if (cmd == 0x12 && len > 16 )  {
		data->sv = value[12];
		data->sd =value[13];
		data->bv = value[14];
		data->ss = value[15];
		LOG("SV(SotewareVersion): %d SD(distinguishability):%d bv(BlueVersion): %d ss(State):%d",
			data->sv, data->sd, data->bv, data->ss);

		if (data->write_chr == NULL) //delay get/set user data when write_chr not inited.
			return;

		if (data->wait_user_data == false ) {
			get_user_data(btdev);
			data->wait_user_data = true;
		}
	} else if (cmd == 0x14 && len > 9) {
		if (value[8] == 0x01) {
			LOG("WT: user data set ok");
			data->wait_user_data = false;
		} else {
			LOG("WT: user data set failed");
		}
	} else if (cmd == 0x10 && len > 9) {
		unsigned char sid = value[8];
		data->wait_user_data = false;
		if ( sid == 0x01 && len > 19) {
			data->weight = value[6] * 256 + value[7];
			data->fat = value[13]*256 + value[14];
			data->tbw = value[15]*256 + value[16];
			data->mus = value[17]*256 + value[18];
			LOG("WT: WEIGHT: %d fat: %d tbw: %d mus: %d", data->weight,
				data->fat, data->tbw, data->mus);
			confirm_data_received(btdev);
			data->data_reported = false;
		} else if (sid == 0x02 && len > 16) {
			data->bone = value[15];
			LOG("WT: BONE: %d", data->bone);
			confirm_data_received(btdev);
			if (data->data_reported == false ) {
				char buf[256];
				char weight[20];
				if (data->sd == 0x00)  {
					sprintf(weight, "%.1f", data->weight/10.0);
				} else if (data->sd== 0x01) {
					sprintf(weight, "%.2f", data->weight/100.0);
				} else
					sprintf(weight, "%d", data->weight);

				sprintf(buf, "%s,0,%d,0,0,0,%d,%d,0,0,%d",
						weight, data->fat, data->tbw, data->bone, data->mus);

				upload_health_data(btdev, "2", buf, upload_data_callback, NULL);
				data->data_reported = true;
			}
		}
	}
}

static BluetoothDeviceCallbacks weight_callbacks =
{
	.init = wt_init,
	.exit = wt_exit,
	.connect_state_changed = wt_connect_state_changed,
	.scan_found = wt_scan_found ,
	.characteristics_added = wt_characteristics_added,
	.characteristics_notify = wt_characteristics_notify,
};

HealthDriver weight_driver =
 {
 	.name = "weight",
 	.service_uuid =  UUID_WEIGHT_SERVICE,
 	.callbacks = &weight_callbacks,
 };

void add_weight_device(const char * address)
{
	add_health_device(address, &weight_driver);
}
