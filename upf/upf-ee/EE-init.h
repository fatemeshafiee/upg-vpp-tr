#include "storage/event.h"
#include "lib/stb_ds.h"
#include <stdio.h>
#include <string.h>
#include <microhttpd.h>
#include <jansson.h>
#include "handler.h"
#include "types/types.h"
#include "storage/event.h"
#include <pthread.h>
#include "send_data.h"
#define PORT 8080

void send_report_client(){
  pthread_t thread;
  int result;
  printf("Starting server for sending reports on port %d\n", PORT);
  result = pthread_create(&thread, NULL, EventReport_UDUT, NULL);
  if (result != 0) {
    fprintf(stderr, "Error creating thread\n");
    return;
  }
  result = pthread_join(thread, NULL);
}
void server_for_getting_requests(){

          hmdefault(subHash, NULL);
  struct MHD_Daemon *daemon;

  daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                            &default_handler, NULL, MHD_OPTION_END);
  if (!daemon)
    return 1;

  while (1)
    sleep(1);

  MHD_stop_daemon(daemon);

}
