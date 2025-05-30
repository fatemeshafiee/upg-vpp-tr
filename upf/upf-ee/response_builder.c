
// Added by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.
#include "response_builder.h"

#include <vlib/vlib.h>
#include <vlibapi/api.h>
#include <vlibmemory/api.h>
#include <vlib/unix/unix.h>

HTTP_response create_subscription(const char *body, bool *created, char **newSubId) {

  if (body == NULL) {
    return (HTTP_response) {
            .body = simple_message("No body provided."),
            .status = BAD_REQUEST
    };

  }
  clib_warning("[server_info] creating subscription");
  UpfEventSubscription *new_subscription = NULL;
  new_subscription = parse_subscription_request(body);
  size_t sub_len = cvector_size(new_subscription->EventList);

  cvector_vector_type(int)to_remove = NULL;
  for (int i = 0; i < sub_len; ++i) {
    int length = sizeof(supported_events) / sizeof(EventType);

    bool isAvailable = false;
    for (int j = 0; j < length; ++j) {
      clib_warning("[server_info] creating subscription %d %d\n", new_subscription->EventList[i].type, supported_events[j]);

      printf("\nCHECKING %d %d\n", new_subscription->EventList[i].type, supported_events[j]);
      if (new_subscription->EventList[i].type == supported_events[j]) {
        isAvailable = true;
        break;
      }
    }
    if (!isAvailable) {
      cvector_push_back(to_remove, i);
    }
  }

  int* tri;
  cvector_for_each_in(tri, to_remove) {
    printf("\nTO REMOVE %d\n", *tri);
    cvector_erase(new_subscription->EventList, *tri);
  }

  cvector_push_back(subscriptionList, *new_subscription);
  sub_len = cvector_size(new_subscription->EventList);
  for(int i = 0; i < sub_len; i++){
    //    void*  element = hashmap_get(&hashmap, (getEventTypeString(new_subscription->EventList[i].type)), strlen(getEventTypeString(new_subscription->EventList[i].type)));
    cvector_vector_type(UpfEventSubscription *) v = NULL;
    v = hmget(subHash,new_subscription->EventList[i].type);
    cvector_push_back(v,new_subscription);
    hmput(subHash,new_subscription->EventList[i].type, v);
  }

  int subId = 1000000 + cvector_size(subscriptionList) - 1;
  *newSubId = malloc(7 + 1);
  sprintf(*newSubId, "%d", subId);
  *(newSubId + 7) = 0;
  *created = true;
  clib_warning("\nthe length of subscribers  %d\n",cvector_size(subscriptionList));

  return (HTTP_response) {
          .body = json_dumps(serialize_created_response(new_subscription, *newSubId), JSON_ENCODE_ANY),
          .status = CREATED
  };
  return (HTTP_response) {
          .body = simple_message("Not implemented 2"),
          .status = CREATED
  };
}

HTTP_response subscription_router(const char *url, const char *method, const char *body, char *subscription_id, bool *created,
                                  char **newSubId) {
  if (*subscription_id == 0) {
    if (validate_method(method, "POST")) {
      return create_subscription(body, created, newSubId);
    }
  } else {
    return (HTTP_response) {
            .body = simple_message("Not implemented 2"),
            .status = NOT_IMPLEMENTED
    };
  }

}
RSPX HTTP_build_response_JSON(const char *message) {
  struct MHD_Response *response;
  clib_warning("the enum is %d\n",MHD_RESPMEM_PERSISTENT);
  void * fa = (void *)message;
  int x = strlen(message);
  response = MHD_create_response_from_buffer_static(x, fa);


  if (!response)
    return NULL;

  MHD_add_response_header(response, "Content-Type", "application/json");
  return response;
}
RSPX HTTP_build_created_response_JSON(const char *message, const char* newSubId, char * url_str) {
  struct MHD_Response *response;
  clib_warning("[FATEMEH] in the create response");
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
  clib_warning("[FATEMEH] in the create response, End of function");
  return response;
}