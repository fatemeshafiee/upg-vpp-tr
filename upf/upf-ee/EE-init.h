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
void VLIB_INIT_FUNCTION();
void* send_report_client();
void* server_for_getting_requests();



