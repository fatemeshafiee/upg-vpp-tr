////
//// Created by Fatemeh Shafiei Ardestani on 2024-08-18.
////
//
//#include "ee-server.h"
//
//clib_error_t* init_server_for_getting_requests(vlib_main_t *vm) {
//  pthread_t server_thread;
//  int result;
//  clib_warning("[server_info] trying to create a server");
//  result = pthread_create(&server_thread, NULL, server_for_getting_requests, NULL);
//  if (result != 0) {
//    clib_warning("[server_info] Error creating server thread");
//    return clib_error_return(0, "Error creating server thread");
//  }
//
//  pthread_detach(server_thread);
//
//  return 0;
//}
//
//
//static void* server_for_getting_requests(void *arg) {
//  struct MHD_Daemon *daemon;
//
//  clib_warning("[server_info] Starting server to get requests on port %d\n", PORT);
//  daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
//                            &default_handler, NULL, MHD_OPTION_END);
//  if (!daemon) {
//    clib_warning("[server_info]Failed to start server");
//    return NULL;
//  }
//
//  while (1) {
//    sleep(1);
//  }
//
//  MHD_stop_daemon(daemon);
//  return NULL;
//}
////VLIB_PLUGIN_REGISTER () = {
////.version = VPP_BUILD_VER,
////.description = "Event Exposure VPP Server Plugin",
////};
////
////VLIB_INIT_FUNCTION(server_for_getting_requests);
