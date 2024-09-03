//
// Created by Fatemeh Shafiei Ardestani on 2024-08-13.
//
#include "../types/types.h"
#ifndef UPG_VPP_SHARED_VARIABLES_H
#define UPG_VPP_SHARED_VARIABLES_H
#ifdef DEFINE_UPF_SHARED
#define EXTERN
#define INITIALIZER(...) = __VA_ARGS__
#else
#define EXTERN extern
#define INITIALIZER(...)
#endif
EXTERN usage_report_per_flow_t *usage_report_per_flow_vector;
//EXTERN struct {char* key; cvector_vector_type(usage_report_per_flow_t*) value;} *usage_hash INITIALIZER(NULL);
EXTERN struct {char* key; usage_report_per_flow_t* value;} *usage_hash INITIALIZER(NULL);
EXTERN pthread_mutex_t ee_lock;
#endif //UPG_VPP_SHARED_VARIABLES_H
//hmput(subHash,new_subscription->EventList[i].type, v);
//cvector_vector_type(UpfEventSubscription*) retrieved_vec = hmget(subHash, USER_DATA_USAGE_TRENDS);
