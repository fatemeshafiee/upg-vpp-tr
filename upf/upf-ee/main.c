

#define DEFINE_UPF_STORAGE
#include "storage/event.h"
#undef DEFINE_UPF_STORAGE
#define STB_DS_IMPLEMENTATION
#include "lib/stb_ds.h"
#undef STB_DS_IMPLEMENTATION
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

int main() {

  pthread_t thread;
  int result;
  printf("Starting server on port %d\n", PORT);
  result = pthread_create(&thread, NULL, EventReport_UDUT, NULL);
  if (result != 0) {
    fprintf(stderr, "Error creating thread\n");
    return 1;
  }

// Add this as a register node to VLIB
// plan B: make that a thread function
  hmdefault(subHash, NULL);
  struct MHD_Daemon *daemon;

  daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                            &default_handler, NULL, MHD_OPTION_END);
  result = pthread_join(thread, NULL);
  if (!daemon)
    return 1;

  while (1)
    sleep(1);

  MHD_stop_daemon(daemon);



  return 0;
}
