
// Added by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.

#ifndef REST_API_C_DECODER_H
#define REST_API_C_DECODER_H
#include "./types.h"
#include <jansson.h>
#include "../lib/cvector.h"

UpfEventSubscription *parse_subscription_request(const char *body);


#endif //REST_API_C_DECODER_H
