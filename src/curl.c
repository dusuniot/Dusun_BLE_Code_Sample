#include <glib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>
#include "curl.h"
#include "log.h"

typedef struct _CurlGlobalData
{
	CURLM *multi;
	guint timer_event;
	int still_running;
}CurlGlobalData;

/* Information associated with a specific easy handle */
typedef struct _ConnInfo {
	CURL *easy;
	cur_post_callback_func_t  cb;
	void * user_data;
	char error[CURL_ERROR_SIZE];
	char response[2048];
} ConnInfo;

/* Information associated with a specific socket */
typedef struct _SockInfo {
	curl_socket_t sockfd;
	CURL *easy;
	int action;
	long timeout;
	GIOChannel *ch;
	guint ev;
} SockInfo;


static CurlGlobalData gd;

/* Die if we get a bad CURLMcode somewhere */
static void mcode_or_die(const char *where, CURLMcode code) {
	if(CURLM_OK != code) {
		const char *s;
		switch (code) {
			case     CURLM_BAD_HANDLE:         s="CURLM_BAD_HANDLE";         break;
			case     CURLM_BAD_EASY_HANDLE:    s="CURLM_BAD_EASY_HANDLE";    break;
			case     CURLM_OUT_OF_MEMORY:      s="CURLM_OUT_OF_MEMORY";      break;
			case     CURLM_INTERNAL_ERROR:     s="CURLM_INTERNAL_ERROR";     break;
			case     CURLM_BAD_SOCKET:         s="CURLM_BAD_SOCKET";         break;
			case     CURLM_UNKNOWN_OPTION:     s="CURLM_UNKNOWN_OPTION";     break;
			case     CURLM_LAST:               s="CURLM_LAST";               break;
			default: s="CURLM_unknown";
		}
		//LOG("ERROR: %s returns %s\n", where, s);
		exit(code);
	}
}

/* Check for completed transfers, and remove their easy handles */
static void check_multi_info()
{
	char *eff_url;
	CURLMsg *msg;
	int msgs_left;
	ConnInfo *conn;
	CURL *easy;
	CURLcode res;

	//LOG("REMAINING: %d\n", gd.still_running);
	while((msg = curl_multi_info_read(gd.multi, &msgs_left))) {
		if(msg->msg == CURLMSG_DONE) {
			//LOG("DONE2");
			easy = msg->easy_handle;
			res = msg->data.result;
			curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
			curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &eff_url);
			//LOG("DONE: %s => (%d) %s\n", eff_url, res, conn->error);
			curl_multi_remove_handle(gd.multi, easy);
			//free(conn->url);
			if (conn->cb) {
				bool ok = false;
				if (res == CURLE_OK)
					ok = true;
				conn->cb(ok, conn->response, conn->user_data);
			}
			curl_easy_cleanup(easy);
			free(conn);
		}
	}
	//LOG("return of check_multi_info");
}

/* Clean up the SockInfo structure */
static void remsock(SockInfo *f)
{
	if(!f) {
		return;
	}
	if(f->ev) {
		g_source_remove(f->ev);
	}
	g_free(f);
}

static gboolean event_cb(GIOChannel *ch, GIOCondition condition, gpointer data)
{
	CURLMcode rc;
	int fd=g_io_channel_unix_get_fd(ch);

	int action =
		(condition & G_IO_IN ? CURL_CSELECT_IN : 0) |
		(condition & G_IO_OUT ? CURL_CSELECT_OUT : 0);

	rc = curl_multi_socket_action(gd.multi, fd, action, &gd.still_running);
	mcode_or_die("event_cb: curl_multi_socket_action", rc);
//	LOG("event_cb b m");
	check_multi_info();
	//LOG("event_cb after m");
	if(gd.still_running) {
		return TRUE;
	}else {
	//	LOG("last transfer done, kill timeout\n");
		if(gd.timer_event) {
			g_source_remove(gd.timer_event);
		}
		return FALSE;
	}
}

/* Assign information to a SockInfo structure */
static void setsock(SockInfo*f, curl_socket_t s, CURL*e, int act)
{
	GIOCondition kind =
	  (act&CURL_POLL_IN?G_IO_IN:0)|(act&CURL_POLL_OUT?G_IO_OUT:0);

	f->sockfd = s;
	f->action = act;
	f->easy = e;
	if(f->ev) {
		g_source_remove(f->ev);
	}
	f->ev=g_io_add_watch(f->ch, kind, event_cb,  NULL);
}

/* Initialize a new SockInfo structure */
static void addsock(curl_socket_t s, CURL *easy, int action)
{
	SockInfo *fdp = g_malloc0(sizeof(SockInfo));

	fdp->ch=g_io_channel_unix_new(s);
	setsock(fdp, s, easy, action);
	curl_multi_assign(gd.multi, s, fdp);
}


/* CURLMOPT_SOCKETFUNCTION */
static int sock_cb(CURL *e, curl_socket_t s, int what, void *cbp, void *sockp)
{
	static const char *whatstr[]={ "none", "IN", "OUT", "INOUT", "REMOVE" };
	SockInfo *fdp = (SockInfo*) sockp;
	//LOG("socket callback: s=%d e=%p what=%s ", s, e, whatstr[what]);
	if(what == CURL_POLL_REMOVE) {
		LOG("\n");
		remsock(fdp);
	}
	else {
		if(!fdp) {
			//LOG("Adding data: %s%s\n",
			//		what&CURL_POLL_IN?"READ":"",
			//		what&CURL_POLL_OUT?"WRITE":"" );
			addsock(s, e, what);
		}else {
			//LOG("Changing action from %d to %d\n", fdp->action, what);
			setsock(fdp, s, e, what);
		}
	}

	return 0;
}

static gboolean timer_cb(gpointer data)
{
	CURLMcode rc;

	rc = curl_multi_socket_action(gd.multi,
			CURL_SOCKET_TIMEOUT, 0, &gd.still_running);
	mcode_or_die("timer_cb: curl_multi_socket_action", rc);
	check_multi_info();
	return FALSE;
}

static int update_timeout_cb(CURLM *multi, long timeout_ms, void *userp)
{
	  struct timeval timeout;
	  timeout.tv_sec = timeout_ms/1000;
	  timeout.tv_usec = (timeout_ms%1000)*1000;

	 // LOG("*** update_timeout_cb %ld => %ld:%ld ***\n",
	  //        timeout_ms, timeout.tv_sec, timeout.tv_usec);

	  gd.timer_event = g_timeout_add(timeout_ms, timer_cb, NULL);
	  return 0;
}

void init_curl()
{
	memset(&gd, 0, sizeof(gd));

	gd.multi = curl_multi_init();
	curl_multi_setopt(gd.multi, CURLMOPT_SOCKETFUNCTION, sock_cb);
  	curl_multi_setopt(gd.multi, CURLMOPT_TIMERFUNCTION, update_timeout_cb);
}

size_t curl_post_write_data(void *buffer, size_t size, size_t nmemb, void *userp)
{
	ConnInfo * conn = userp;
	int len = size * nmemb;
	if (len >= 2048)
		len = 2048;

	memcpy(conn->response, buffer, len);
	conn->response[len] = 0;
//	LOG("RECV:%s", conn->response);
	return size * nmemb;
}

bool curl_post_json(const char * url, const char * json_data, cur_post_callback_func_t  cb, void * user_data)
{
	ConnInfo *conn;
	CURLMcode rc;

 	conn = g_malloc0(sizeof(ConnInfo));

 	conn->easy = curl_easy_init();
 	conn->cb = cb;
 	conn->user_data = user_data;
 	if (conn->easy == NULL ) {
 		//LOG("failed to curl_easy_init");
 		return false;
 	}

 	struct curl_slist *headers=NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(conn->easy, CURLOPT_URL, url);
	//curl_easy_setopt(conn->easy, CURLOPT_POSTFIELDS, json_data);
	curl_easy_setopt(conn->easy, CURLOPT_POSTFIELDSIZE, strlen(json_data));
	curl_easy_setopt(conn->easy, CURLOPT_COPYPOSTFIELDS, json_data);
	curl_easy_setopt(conn->easy, CURLOPT_WRITEFUNCTION, curl_post_write_data);
	curl_easy_setopt(conn->easy, CURLOPT_WRITEDATA, conn);
	curl_easy_setopt(conn->easy, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(conn->easy, CURLOPT_CONNECTTIMEOUT, 5L);

	//curl_easy_setopt(conn->easy, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(conn->easy, CURLOPT_ERRORBUFFER, conn->error);
	curl_easy_setopt(conn->easy, CURLOPT_PRIVATE, conn);

	rc =curl_multi_add_handle(gd.multi, conn->easy);
	mcode_or_die("new_conn: curl_multi_add_handle", rc);

	return true;
}