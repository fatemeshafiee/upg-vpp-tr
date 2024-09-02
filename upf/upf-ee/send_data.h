#ifndef REST_API_C_SEND_DATA_H
#define REST_API_C_SEND_DATA_H

#define MAP_OK 0
#define MAP_MISSING -3
#define MAP_FULL -2
#define MAP_OMEM -1
#include "lib/stb_ds.h"
#include "types/encoder.h"
#include "storage/event.h"
#include "storage/shared_variables.h"
#include <pthread.h>
#include <stdio.h>
#define _GNU_SOURCE
#define __USE_XOPEN
#define _XOPEN_SOURCE 700
#include <time.h>
#include <curl/curl.h>
#include "string.h"
#include <jansson.h>
#include <vlib/vlib.h>
#include <vlib/unix/unix.h>
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
void parse_time(const char* date_time, struct  tm* tm);
void fillNotificationItem(UpfEventSubscription upfSub,cvector_vector_type(NotificationItem *) Notifvec,EventType type);
void create_send_report(UpfEventSubscription upfSub,EventType type);
void send_report(char *json_data,UpfEventSubscription upfSub,EventType type)void* EventReport_UDUT();
#endif //REST_API_C_SEND_DATA_H