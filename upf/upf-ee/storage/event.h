//
// Created by Fatemeh Shafiei Ardestani on 2024-07-07.
//

#ifndef REST_API_C_EVENT_H
#define REST_API_C_EVENT_H

#ifdef DEFINE_UPF_STORAGE
#define EXTERN
#define INITIALIZER(...) = __VA_ARGS__
#else
#define EXTERN extern
#define INITIALIZER(...)
#endif

#include "../lib/cvector.h"
#include "../types/types.h"
#include "../lib/stb_ds.h"

EXTERN cvector_vector_type(UpfEventSubscription) subscriptionList INITIALIZER(NULL);
EXTERN EventType supported_events[1] INITIALIZER({USER_DATA_USAGE_TRENDS});
EXTERN struct {EventType key; cvector_vector_type(UpfEventSubscription*) value;} *subHash INITIALIZER(NULL);


//int initialize_hashmap(const unsigned initial_size){
//  return hashmap_create(initial_size, &hashmap);;
//}

#endif //REST_API_C_EVENT_H