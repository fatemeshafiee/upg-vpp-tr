//
// Created by Fatemeh Shafiei Ardestani on 2024-08-20.
//

#include "ee_server.h"
#include <microhttpd.h>
#include <vlib/vlib.h>
#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vlib/unix/unix.h>
#include <time.h>

#define SERVER_PORT 8080

struct MHD_Daemon *daemon = NULL;


static void poll_http_server() {
  if (daemon != NULL) {
    MHD_run(daemon);
  }
}

void* ee_http_server(void *arg) {
  vlib_main_t *vm = &vlib_global_main;

  clib_warning("[server_info] Starting server to get requests \n");
  daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                            &default_handler, NULL, MHD_OPTION_END);
  if (!daemon) {
    clib_warning("[server_info] Starting server failed. \n");
    return NULL;
  }

  while (1) {
    vlib_process_wait_for_event_or_clock(vm, 0.1 /* seconds */);
    poll_http_server();
  }

  if (daemon != NULL) {
    MHD_stop_daemon(daemon);
    daemon = NULL;
  }
  return NULL;
}

VLIB_REGISTER_NODE(http_server_process_node) = {
        .function = ee_http_server,
        .type = VLIB_NODE_TYPE_PROCESS,
        .name = "http-server-process",
};
