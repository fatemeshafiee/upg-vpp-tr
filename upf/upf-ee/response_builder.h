#include <microhttpd.h>
#include "utils.h"

struct MHD_Response *HTTP_build_response_JSON(const char *message);
struct MHD_Response *HTTP_build_created_response_JSON(const char *message, const char* newSubId, char * url_str);