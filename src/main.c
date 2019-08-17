#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <glib.h>

#include "gdbus/gdbus.h"
#include "monitor/uuid.h"

#include "dev.h"
#include "log.h"
#include "curl.h"
#include "bp.h"
#include "wt.h"

static GMainLoop *main_loop;
static DBusConnection *dbus_conn;

static guint input = 0;

static void connect_handler(DBusConnection *connection, void *user_data)
{

}

static void disconnect_handler(DBusConnection *connection, void *user_data)
{

}

void print_iter(const char *label, const char *name,
						DBusMessageIter *iter)
{
	dbus_bool_t valbool;
	dbus_uint32_t valu32;
	dbus_uint16_t valu16;
	dbus_int16_t vals16;
	unsigned char byte;
	const char *valstr;
	DBusMessageIter subiter;
	char *entry;

	if (iter == NULL) {
		LOG("%s%s is nil\n", label, name);
		return;
	}

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_INVALID:
		LOG("%s%s is invalid\n", label, name);
		break;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
		dbus_message_iter_get_basic(iter, &valstr);
		LOG("%s%s: %s\n", label, name, valstr);
		break;
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_basic(iter, &valbool);
		LOG("%s%s: %s\n", label, name,
					valbool == TRUE ? "yes" : "no");
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &valu32);
		LOG("%s%s: 0x%06x\n", label, name, valu32);
		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_basic(iter, &valu16);
		LOG("%s%s: 0x%04x\n", label, name, valu16);
		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_basic(iter, &vals16);
		LOG("%s%s: %d\n", label, name, vals16);
		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_basic(iter, &byte);
		LOG("%s%s: 0x%02x\n", label, name, byte);
		break;
	case DBUS_TYPE_VARIANT:
		dbus_message_iter_recurse(iter, &subiter);
		print_iter(label, name, &subiter);
		break;
	case DBUS_TYPE_ARRAY:
		dbus_message_iter_recurse(iter, &subiter);
		while (dbus_message_iter_get_arg_type(&subiter) !=
							DBUS_TYPE_INVALID) {
			print_iter(label, name, &subiter);
			dbus_message_iter_next(&subiter);
		}
		break;
	case DBUS_TYPE_DICT_ENTRY:
		dbus_message_iter_recurse(iter, &subiter);
		entry = g_strconcat(name, " Key", NULL);
		print_iter(label, entry, &subiter);
		g_free(entry);

		entry = g_strconcat(name, " Value", NULL);
		dbus_message_iter_next(&subiter);
		print_iter(label, entry, &subiter);
		g_free(entry);
		break;
	default:
		LOG("%s%s has unsupported type\n", label, name);
		break;
	}
}

static void proxy_added(GDBusProxy *proxy, void *user_data)
{
	add_proxy(proxy);
}

static void proxy_removed(GDBusProxy *proxy, void *user_data)
{
	remove_proxy(proxy);
}

static void property_changed(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter, void *user_data)
{
	const char *interface;
	char *str;
	str = g_strdup_printf("Attribute %s ",
					g_dbus_proxy_get_path(proxy));
	g_free(str);
	property_changed_handler(proxy, name, iter, user_data);
}

static void message_handler(DBusConnection *connection,
					DBusMessage *message, void *user_data)
{
	LOG("[SIGNAL] %s.%s\n", dbus_message_get_interface(message),
					dbus_message_get_member(message));
}

static void client_ready(GDBusClient *client, void *user_data)
{

}

int set_bdaddr()
{
	char addr[30];
	char cmd[256];
	int i = 0;
	int len =0;
	int ret =0;

	len = get_line_from_file("/sys/class/bluetooth/hci0/address", addr, sizeof(addr));
	if (len <0 )
		return -1;

	if (!strncmp(addr, "30:ae:7b:", 9))  {
	        return -1;
	}

	len = get_line_from_file("/sys/class/net/eth0/address", addr, sizeof(addr));

	if (len < 17)
		return -1;

	LOG("Set bluetooth address: %s", addr);
	for (i=1; i<6; i++)
	{
		addr[i*3-1] = '\0'; //replace ':' with '\0'
	}

	sprintf(cmd, "hciconfig hci0 up &&  bccmd psset -r -s 0 bdaddr 0x%s 0x00 0x%s 0x%s 0x%s 0x00 0x%s 0x%s",
									addr + 3*3,
									addr + 5*3,
									addr + 4*3,
									addr + 2*3,
									addr + 3 ,
									addr);
	ret = system(cmd);
	sleep(1);
	return ret;
}


void load_healt_drivers()
{
	//extern HealthDriver lock_driver;
	//register_health_driver(&lock_driver);
	
	//extern HealthDriver weight_driver;
	//register_health_driver(&weight_driver);
	
	//extern HealthDriver blood_pressure_driver;
	//register_health_driver(&blood_pressure_driver);
	
	extern HealthDriver Oximeter_driver;
	register_health_driver(&Oximeter_driver);
}

int main(int argc, char *argv[])
{
	GError *error = NULL;
	GDBusClient *client;

	INIT_LOG("HL");
	/*init global data for bluez operation*/
	init_global_data();
	init_curl();
	/*register the drivers you have written*/
	load_healt_drivers();
  /*create a main loop object*/
	main_loop = g_main_loop_new(NULL, FALSE);
	/*set up the dbus connection*/
	dbus_conn = g_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, NULL);
	
	/*create a bluez client for dbus connection object*/
	client = g_dbus_client_new(dbus_conn, "org.bluez", "/org/bluez");
  /* set connect/disconnect/signal handler function*/
	g_dbus_client_set_connect_watch(client, connect_handler, NULL);
	g_dbus_client_set_disconnect_watch(client, disconnect_handler, NULL);
	g_dbus_client_set_signal_watch(client, message_handler, NULL);
  /* set proxy handlers*/
	g_dbus_client_set_proxy_handlers(client, proxy_added, proxy_removed,
							property_changed, NULL);
  /* set ready */
	g_dbus_client_set_ready_watch(client, client_ready,  NULL);
  /*running in a loop*/
	g_main_loop_run(main_loop);
  
  /*release the resources*/
	g_dbus_client_unref(client);
	dbus_connection_unref(dbus_conn);
	g_main_loop_unref(main_loop);
	return 0;
}
