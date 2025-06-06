
// Added by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.
#include "handler.h"
#include <vlib/vlib.h>
#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vlib/unix/unix.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
jmp_buf exceptionBuffer;

#define TRY if (setjmp(exceptionBuffer) == 0)
#define CATCH else

void get_current_time(char *buffer, size_t buffer_size) {
  struct timeval tv;
  struct tm local_tm;
  gettimeofday(&tv, NULL);
  localtime_r(&tv.tv_sec, &local_tm);
  int millisec = tv.tv_usec / 1000;
  snprintf(buffer, buffer_size, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
           local_tm.tm_year + 1900, local_tm.tm_mon + 1, local_tm.tm_mday,
           local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec, millisec);
//  time_t now = time(NULL);
//  struct tm local_tm;
//  localtime_r(&now, &local_tm);
//  strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &local_tm);

}

void log_api(const char *url, const char *method) {
  clib_warning("[server_info] We got request[%s] %s\n", method, url);
}

enum MHD_Result default_handler(void *cls, struct MHD_Connection *connection, const char *url,
                                const char *method, const char *version, const char *upload_data,
                                size_t *upload_data_size, void **con_cls) {

  if (NULL == *con_cls) {
    struct PostData *post_data = malloc(sizeof(struct PostData));
    post_data->data = NULL;
    post_data->size = 0;
    post_data->capacity = 0;
    *con_cls = post_data;
    return MHD_YES;
  }

  struct PostData *post_data = *con_cls;

  char *url_str = (char *)url;
  char *method_str = (char *)method;
  int ret;

  struct MHD_Response *response;
  HTTP_response response_api;

  log_api(url_str, method_str);
  bool created = false;
  char* subId = NULL;

  TRY {

    if (strcmp(method, "POST") == 0) {
      if (*upload_data_size != 0) {
        // Ensure enough capacity
        if (post_data->size + *upload_data_size >= post_data->capacity) {
          post_data->capacity = post_data->size + *upload_data_size + 1;
          post_data->data = realloc(post_data->data, post_data->capacity);
        }

        // Copy the data
        memcpy(post_data->data + post_data->size, upload_data, *upload_data_size);
        post_data->size += *upload_data_size;
        post_data->data[post_data->size] = '\0';

        // Reset upload_data_size to signal that the data has been processed
        *upload_data_size = 0;

        return MHD_YES;
      }
    }
    char *subscription_id = strstr(url_str, "/nupf-ee/v1/ee-subscriptions");
    if (subscription_id != NULL) {

      subscription_id += strlen("/nupf-ee/v1/ee-subscriptions");
      clib_warning("\n FATEMEH %s %p\n", *subscription_id, subscription_id);
      clib_warning("\nFATEMEH %s\n", upload_data);
      clib_warning("\nFATEMEH %d\n", *upload_data_size);
      log_api(url_str, method_str);
      char c_time[50];
      get_current_time(c_time, sizeof(c_time));
      clib_warning("[DSN_Latency]The subscription request received the time is %s", c_time);
      response_api = subscription_router(url_str,method_str,post_data->data, subscription_id, &created, &subId);
    }
    else {
      response_api = (HTTP_response){
              .body = simple_message("Not found"),
              .status = NOT_FOUND
      };
    }
  } CATCH {
    response_api = (HTTP_response){
            .body = simple_message("Internal server error"),
            .status = INTERNAL_SERVER_ERROR
    };

    printf("Internal server error");
  }
  clib_warning("the created is%d", created);
  if(created) response = HTTP_build_created_response_JSON(response_api.body, subId, url_str);
  else response = HTTP_build_response_JSON(response_api.body);

  if (!response){
    clib_warning("[FATEMEH] the response is null");
    return MHD_NO;
  }


  ret = MHD_queue_response(connection, response_api.status, response);
  MHD_destroy_response(response);

  fflush(0);
  return ret;
}