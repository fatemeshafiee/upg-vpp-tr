
// Added by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.
#include "EE-init.h"
#include <stdio.h>

void log_ee(char* j) {
  clib_warning(j);
  char* f = "/openair-upf/log_ee.txt";
  FILE *file = fopen(f, "a");
  if (file == NULL) {
    perror("Failed to open file");
    return;
  }
  fprintf(file, "%s\n", j);
  fclose(file);
}

static clib_error_t* init_send_report_client(vlib_main_t *vm, vlib_node_runtime_t * rt, vlib_frame_t * f) {
  pthread_t client_thread;
  int result;
  log_ee("[client_info] in the init client function.");

  result = pthread_create(&client_thread, NULL, send_report_client, NULL);
  if (result != 0) {
    log_ee("[client_info] Error creating client thread");
    return clib_error_return(0, "[client_info] Error creating client thread");
  }
  pthread_join(client_thread, NULL);

  return 0;
}

static clib_error_t* init_server_for_getting_requests(vlib_main_t *vm) {
//  pthread_t server_thread;
//  int result;
//  log_ee("[server_info] in the init server function.");
//  result = pthread_create(&server_thread, NULL, server_for_getting_requests, NULL);
//  if (result != 0) {
//    log_ee("[server_info]  Error creating server thread");
//    return clib_error_return(0, "Error creating server thread");
//  }
//
////  pthread_join(server_thread, NULL);
//
//  return 0;
  struct MHD_Daemon *daemon;

  log_ee("[server_info] Starting server to get requests on port\n");
  daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                            &default_handler, NULL, MHD_OPTION_END);
  if (!daemon) {
    log_ee("[server_info] Failed to start server");
    return clib_error_return(0, "Error creating server thread");;
  }

  while (1) {
    sleep(1);
  }

  MHD_stop_daemon(daemon);
  return 0;
}

void* send_report_client(void *arg) {
  pthread_t thread;
  int result;

  log_ee("[client_info] Starting client for sending reports on port\n");
  result = pthread_create(&thread, NULL, EventReport_UDUT, NULL);
  if (result != 0) {
    fprintf(stderr, "Error creating report thread\n");
    return NULL;
  }
//  pthread_join(thread, NULL);
  return NULL;
}

void* server_for_getting_requests(void *arg) {
  struct MHD_Daemon *daemon;

  log_ee("[server_info] Starting server to get requests on port\n");
  daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                            &default_handler, NULL, MHD_OPTION_END);
  if (!daemon) {

    return NULL;
  }

  while (1) {
    sleep(1);
  }

  MHD_stop_daemon(daemon);
  return NULL;
}
//VLIB_REGISTER_NODE(send_report_node) = {
//        .function = init_send_report_client,
//        .type = VLIB_NODE_TYPE_PROCESS,
//        .name = "periodic-sending-process",
//};
//VLIB_INIT_FUNCTION (init_send_report_client);
//VLIB_INIT_FUNCTION (init_server_for_getting_requests);