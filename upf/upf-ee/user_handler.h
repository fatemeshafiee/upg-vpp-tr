#include "pg.h"
#include <string.h>
#include <stdio.h>
#include "storage/event.h"
#include "types/decoder.h"
#include "types/encoder.h"
#include <jansson.h>
#include "lib/cvector_utils.h"

HTTP_response create_subscription(const char *body, bool *created, char **newSubId);
HTTP_response subscription_router(const char *url, const char *method, const char *body, char *subscription_id, bool *created,
                    char **newSubId);
