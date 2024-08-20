////
//// Created by Fatemeh Shafiei Ardestani on 2024-08-18.
////
//
#ifndef UPG_VPP_EE_SERVER_H
#define UPG_VPP_EE_SERVER_H

#include <vlib/vlib.h>
#include <vnet/vnet.h>
#include <vnet/plugin/plugin.h>
#include <vlib/unix/plugin.h>
#include <vpp/app/version.h>
#include <vnet/dpo/lookup_dpo.h>
#include <vnet/fib/ip4_fib.h>
#include <vnet/fib/ip6_fib.h>
#include <vnet/ip/ip6_hop_by_hop.h>
#include <microhttpd.h>
#include <jansson.h>
#include "handler.h"
#include "types/types.h"
#include "send_data.h"
#include <microhttpd.h>
#include "storage/event.h"
#include "lib/stb_ds.h"

void send_report();
#endif //UPG_VPP_EE_SERVER_H
