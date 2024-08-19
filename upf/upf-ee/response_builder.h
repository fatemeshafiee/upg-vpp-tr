#include <string.h>
#include <stdio.h>
#include "storage/event.h"
#include "types/decoder.h"
#include "types/encoder.h"
#include <jansson.h>
#include "lib/cvector_utils.h"
#include "utils.h"

struct MHD_Response *HTTP_build_response_JSON(const char *message);
struct MHD_Response *HTTP_build_created_response_JSON(const char *message, const char* newSubId, char * url_str);
HTTP_response create_subscription(const char *body, bool *created, char **newSubId);
HTTP_response subscription_router(const char *url, const char *method, const char *body, char *subscription_id, bool *created,
                                  char **newSubId);
#include <microhttpd.h>