#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <unistd.h>

#include "util.h"

void json_add_string(json_object * root, const char * key, const char * val)
{
	json_object_object_add(root, key, json_object_new_string(val));
}

void json_add_int(json_object * root, const char * key, int val)
{
	json_object_object_add(root, key, json_object_new_int(val));
}

const char * json_get_string(json_object * root, const char * key)
{
	const char * str = NULL;
	json_object * child;
	if (json_object_object_get_ex(root, key, &child)) {
		str = json_object_get_string(child);
	}
	return str;
}

const char * json_get_string_ex(char * buf, json_object * root, const char * key)
{
	const char * val = NULL;
	val = json_get_string(root, key);
	if (val != NULL )
		strcpy(buf, val);
	return val;
}


int json_get_int(json_object * root, char * key)
{
	int i = -1;
	json_object * child;
	if (json_object_object_get_ex(root, key, &child)) {
		i = json_object_get_int(child);
	}
	return i;
}


json_object * json_get_object(json_object * root, const char * key)
{
	json_object * retObj = NULL;
	json_object_object_get_ex(root, key, &retObj);
	return retObj;
}

void string_to_upper(char * str)
{
	int i;
	int len = strlen(str);
	for (i=0; i<len; i++) {
		str[i] = toupper(str[i]);
	}
}

int get_line_from_file(char * path,  char * outbuf, int maxsize)
{
	FILE * fp = fopen(path, "r");

	if (fp == NULL)
		return -1;

	if ( fgets(outbuf, maxsize, fp) )
	{
		int len = strlen(outbuf);

		if (outbuf[len-1] == '\n')
			outbuf[len-1] = '\0';

		fclose(fp);
		return strlen(outbuf);
	} else {
		fclose(fp);
		return -1;
	}
}

static int  write_led_attr(const char * led, const char * attr, const char * value)
{
        char path[100];
        char str[20];
        sprintf(path, "/sys/class/leds/%s/%s", led, attr);

        FILE * fp = fopen(path, "w");
        if (!fp)
                return -1;
        sprintf(str, "%s\n", value);
        fputs(str, fp);
        fclose(fp);
        return 0;
}

void led_shot(const char * led)
{
	 if (write_led_attr(led, "trigger", "oneshot") !=0 )
		return;
	write_led_attr(led, "shot", "1");
}

void led_on(const char * led,  bool on)
{
	if (write_led_attr(led, "trigger", "none") != 0 )
		return;

	const char * val = on ? "1" : "0";
	write_led_attr(led, "brightness", val);
}

void led_blink(const char * led, bool on)
{
	const char * trigger = on ? "timer" : "none";
	write_led_attr(led, "trigger", trigger) ;
}