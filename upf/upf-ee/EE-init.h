
// Added by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.
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
void log_ee(char* j);
static clib_error_t* init_send_report_client(vlib_main_t *vm, vlib_node_runtime_t * rt, vlib_frame_t * f);
static clib_error_t* init_server_for_getting_requests(vlib_main_t *vm);

void* send_report_client(void *arg);
void* server_for_getting_requests(void *arg);




