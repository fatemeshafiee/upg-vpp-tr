
// Added by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.
#include "ee_server.h"
#include <microhttpd.h>
#include <vlib/vlib.h>
#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vlib/unix/unix.h>
#include <time.h>

#define SERVER_PORT 4355

struct MHD_Daemon *ee_daemon = NULL;


static void poll_http_server() {
  if (ee_daemon != NULL) {
//    clib_warning("[server_info] got request. \n");
    MHD_run(ee_daemon);
  }
}

void* ee_http_server(void *arg) {
  vlib_main_t *vm = &vlib_global_main;

  clib_warning("[server_info] Starting server to get requests \n");
  ee_daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, SERVER_PORT, NULL, NULL,
                            &default_handler, NULL, MHD_OPTION_END);
  if (!ee_daemon) {
    clib_warning("[server_info] Starting server failed. \n");
    return NULL;
  }

  while (1) {
    vlib_process_wait_for_event_or_clock(vm, 0.0001 /* seconds */);
    poll_http_server();
  }

  if (ee_daemon != NULL) {
    MHD_stop_daemon(ee_daemon);
    ee_daemon = NULL;
  }
  return NULL;
}

VLIB_REGISTER_NODE(http_server_process_node) = {
        .function = ee_http_server,
        .type = VLIB_NODE_TYPE_PROCESS,
        .name = "http-server-process",
};
