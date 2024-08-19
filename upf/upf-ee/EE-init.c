//
// Created by Fatemeh Shafiei Ardestani on 2024-08-18.
//
#include "EE-init.h"
void EE_INIT_FUNCTION() {
  pthread_t client_thread, server_thread;
  int result;

  clib_warning("[clinent_info]Starting client thread for sending reports.\n");
  result = pthread_create(&client_thread, NULL, send_report_client, NULL);
  if (result != 0) {
    clib_warning(stderr, "Error creating client thread\n");
    return;
  }

  clib_warning("[server_info]Starting server to get requests on port %d\n", PORT);
  result = pthread_create(&server_thread, NULL, server_for_getting_requests, NULL);
  if (result != 0) {
    clib_warning(stderr, "Error creating server thread\n");
    return;
  }

  pthread_join(client_thread, NULL);
  pthread_join(server_thread, NULL);
}
void* send_report_client(){
  pthread_t thread;
  int result;
  clib_warning("[client_info]Starting server for sending reports on port %d\n", PORT);
  result = pthread_create(&thread, NULL, EventReport_UDUT, NULL);
  if (result != 0) {
    fprintf(stderr, "Error creating thread\n");
    return;
  }
  result = pthread_join(thread, NULL);
  return NULL;
}
void* server_for_getting_requests(){

          hmdefault(subHash, NULL);
  struct MHD_Daemon *daemon;
  clib_warning("[server_info] server part is running!!");
  daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                            &default_handler, NULL, MHD_OPTION_END);
  if (!daemon)
    return 1;

  while (1)
    sleep(1);

  MHD_stop_daemon(daemon);
  return NULL;

}