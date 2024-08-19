////
//// Created by Fatemeh Shafiei Ardestani on 2024-08-18.
////
//#include "user_handler.h"
//HTTP_response create_subscription(const char *body, bool *created, char **newSubId) {
//  if (body == NULL) {
//    return (HTTP_response) {
//            .body = simple_message("No body provided."),
//            .status = BAD_REQUEST
//    };
//
//  }
//  UpfEventSubscription *new_subscription = NULL;
//  new_subscription = parse_subscription_request(body);
//  size_t sub_len = cvector_size(new_subscription->EventList);
//
//  cvector_vector_type(int)to_remove = NULL;
//  for (int i = 0; i < sub_len; ++i) {
//    int length = sizeof(supported_events) / sizeof(EventType);
//
//    bool isAvailable = false;
//    for (int j = 0; j < length; ++j) {
//      printf("\nCHECKING %d %d\n", new_subscription->EventList[i].type, supported_events[j]);
//      if (new_subscription->EventList[i].type == supported_events[j]) {
//        isAvailable = true;
//        break;
//      }
//    }
//    if (!isAvailable) {
//      cvector_push_back(to_remove, i);
//    }
//  }
//
//  int* tri;
//  cvector_for_each_in(tri, to_remove) {
//    printf("\nTO REMOVE %d\n", *tri);
//    cvector_erase(new_subscription->EventList, *tri);
//  }
//
//  cvector_push_back(subscriptionList, *new_subscription);
//  sub_len = cvector_size(new_subscription->EventList);
//  for(int i = 0; i < sub_len; i++){
//    //    void*  element = hashmap_get(&hashmap, (getEventTypeString(new_subscription->EventList[i].type)), strlen(getEventTypeString(new_subscription->EventList[i].type)));
//    cvector_vector_type(UpfEventSubscription *) v = NULL;
//    v = hmget(subHash,new_subscription->EventList[i].type);
//    cvector_push_back(v,new_subscription);
//    hmput(subHash,new_subscription->EventList[i].type, v);
//  }
//
//  int subId = 1000000 + cvector_size(subscriptionList) - 1;
//  *newSubId = malloc(7 + 1);
//  sprintf(*newSubId, "%d", subId);
//  *(newSubId + 7) = 0;
//  *created = true;
//  printf("\nthe length of subscribers  %d\n",cvector_size(subscriptionList));
//  return (HTTP_response) {
//          .body = json_dumps(serialize_created_response(new_subscription, *newSubId), JSON_ENCODE_ANY),
//          .status = CREATED
//  };
//}
//
//HTTP_response subscription_router(const char *url, const char *method, const char *body, char *subscription_id, bool *created,
//                                  char **newSubId) {
//  if (*subscription_id == 0) {
//    if (validate_method(method, "POST")) {
//      return create_subscription(body, created, newSubId);
//    }
//  } else {
//    return (HTTP_response) {
//            .body = simple_message("Not implemented 2"),
//            .status = NOT_IMPLEMENTED
//    };
//  }
//
//}