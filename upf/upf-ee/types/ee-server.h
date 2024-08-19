//
// Created by Fatemeh Shafiei Ardestani on 2024-08-18.
//

#ifndef UPG_VPP_EE_SERVER_H
#define UPG_VPP_EE_SERVER_H

#include <vlib/vlib.h>
#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
//Event Exposure VPP Server Plugin
#define Event_Exposure_VPP_SERVER "event_exposure_plugin"
void* server_for_getting_requests(void *arg);
clib_error_t* init_server_for_getting_requests(vlib_main_t *vm);

#endif //UPG_VPP_EE_SERVER_H
