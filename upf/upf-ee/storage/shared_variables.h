//
// Created by Fatemeh Shafiei Ardestani on 2024-08-13.
//

#ifndef UPG_VPP_SHARED_VARIABLES_H
#define UPG_VPP_SHARED_VARIABLES_H
#ifdef DEFINE_UPF_SHARED
#define EXTERN
#define INITIALIZER(...) = __VA_ARGS__
#else
#define EXTERN extern
#define INITIALIZER(...)
#endif
#endif //UPG_VPP_SHARED_VARIABLES_H
#include "../types/types.h"
EXTERN usage_report_per_flow_t *usage_report_per_flow_vector;
EXTERN pthread_mutex_t ee_lock INITIALIZER(PTHREAD_MUTEX_INITIALIZER);