//
// Created by Fatemeh Shafiei Ardestani on 2024-08-13.
//

#ifndef UPG_VPP_SHARED_VARIABLES_H
#define UPG_VPP_SHARED_VARIABLES_H
#ifdef DEFINE_UPF_SHARED
#define EXTERN
#else
#define EXTERN extern
#endif
#endif //UPG_VPP_SHARED_VARIABLES_H
EXTERN usage_report_per_flow_t *usage_report_per_flow_vector;
EXTERN pthread_mutex_t lock;