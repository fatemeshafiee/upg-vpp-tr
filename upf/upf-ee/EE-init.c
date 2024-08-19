#include "EE-init.h"
clib_error_t* init_send_report_client(vlib_main_t *vm) {
  pthread_t client_thread;
  int result;

  result = pthread_create(&client_thread, NULL, send_report_client, NULL);
  if (result != 0) {
    return clib_error_return(0, "Error creating client thread");
  }
  pthread_detach(client_thread);

  return 0;
}

clib_error_t* init_server_for_getting_requests(vlib_main_t *vm) {
  pthread_t server_thread;
  int result;

  result = pthread_create(&server_thread, NULL, server_for_getting_requests, NULL);
  if (result != 0) {
    return clib_error_return(0, "Error creating server thread");
  }

  pthread_detach(server_thread);

  return 0;
}

void* send_report_client(void *arg) {
  pthread_t thread;
  int result;

  clib_warning("[client_info] Starting client for sending reports on port %d\n", PORT);
  result = pthread_create(&thread, NULL, EventReport_UDUT, NULL);
  if (result != 0) {
    fprintf(stderr, "Error creating report thread\n");
    return NULL;
  }
  pthread_join(thread, NULL);
  return NULL;
}

void* server_for_getting_requests(void *arg) {
  struct MHD_Daemon *daemon;

  clib_warning("[server_info] Starting server to get requests on port %d\n", PORT);
  daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                            &default_handler, NULL, MHD_OPTION_END);
  if (!daemon) {
    clib_warning("Failed to start server");
    return NULL;
  }

  while (1) {
    sleep(1);
  }

  MHD_stop_daemon(daemon);
  return NULL;
}
