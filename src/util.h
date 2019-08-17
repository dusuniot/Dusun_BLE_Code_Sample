#ifndef  GW_UTIL_H
#define  GW_UTIL_H
#include <curl/curl.h>
#include <json-c/json.h>

void json_add_string(json_object * root, const char * key, const char * val);
void json_add_int(json_object * root, const char * key, int val);
const char * json_get_string(json_object * root, const char * key);
const char * json_get_string_ex(char * buf, json_object * root, const char * key);
int json_get_int(json_object * root, char * key);
json_object * json_get_object(json_object * root, const char * key);
int get_line_from_file(char * path,  char * outbuf, int maxsize);
void string_to_upper(char * str);
void led_shot(const char * led);
void led_on(const char * led,  bool on);
void led_blink(const char * led, bool on);
#endif