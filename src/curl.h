#ifndef DUSUN_GLIB_CURL_H
#define DUSUN_GLIB_CURL_H

typedef void (*cur_post_callback_func_t) (bool ret, const char * response_data, void * user_data);
bool curl_post_json(const char * url, const char * json_data, cur_post_callback_func_t  cb, void * user_data);
void init_curl();
#endif