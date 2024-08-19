#include <vlib/vlib.h>
#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
#include "storage/event.h"
#include "lib/stb_ds.h"
#include <stdio.h>
#include <string.h>
#include <microhttpd.h>
#include <jansson.h>
#include "handler.h"
#include "types/types.h"
#include <pthread.h>
#include "send_data.h"
#define PORT 8080

static clib_error_t* init_send_report_client(vlib_main_t *vm);
static clib_error_t* init_server_for_getting_requests(vlib_main_t *vm);

void* send_report_client(void *arg);
void* server_for_getting_requests(void *arg);




