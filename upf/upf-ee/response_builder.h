#include <microhttpd.h>
#include "utils.h"

struct MHD_Response *HTTP_build_response_JSON(const char *message) {
  struct MHD_Response *response;

  response = MHD_create_response_from_buffer(strlen(message), (void *)message, MHD_RESPMEM_PERSISTENT);

  if (!response)
    return NULL;

  MHD_add_response_header(response, "Content-Type", "application/json");
  return response;
}
struct MHD_Response *HTTP_build_created_response_JSON(const char *message, const char* newSubId, char * url_str) {
  struct MHD_Response *response;

  response = MHD_create_response_from_buffer(strlen(message), (void *)message, MHD_RESPMEM_PERSISTENT);
  size_t url_len = strlen(url_str);
  size_t sub_id_len = strlen(newSubId);
  size_t total_len = url_len + sub_id_len + 1;

  char *location = (char *)malloc(total_len);

  strcpy(location, url_str);
  strcat(location, newSubId);

  if (!response)
    return NULL;
  MHD_add_response_header(response,"Location",location);
  MHD_add_response_header(response, "Content-Type", "application/json");

  return response;
}
