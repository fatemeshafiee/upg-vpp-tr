//
// Created by Fatemeh Shafiei Ardestani on 2024-08-13.
//
#include "../types/types.h"
#include <pthread.h>
#ifndef UPG_VPP_SHARED_VARIABLES_H
#define UPG_VPP_SHARED_VARIABLES_H
#ifdef DEFINE_UPF_SHARED
#define EXTERN
#define INITIALIZER pthread_mutex_init(&ee_lock, NULL);
#else
#define EXTERN extern
#define INITIALIZER
#endif


EXTERN usage_report_per_flow_t *usage_report_per_flow_vector;
EXTERN pthread_mutex_t ee_lock;
INITIALIZER
#endif //UPG_VPP_SHARED_VARIABLES_H
