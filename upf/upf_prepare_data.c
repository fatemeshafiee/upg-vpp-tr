//
// Created by Fatemeh Shafiei Ardestani on 2024-08-08.

#include "flowtable.h"
#include <vlib/vlib.h>
#include <vlib/unix/unix.h>
#include "upf-ee/types/types.h"

#define DEFINE_UPF_SHARED
#include "upf-ee/storage/shared_variables.h"
#undef DEFINE_UPF_SHARED

#define DEFINE_UPF_STORAGE
#include "upf-ee/storage/event.h"
#undef DEFINE_UPF_STORAGE

#define STB_DS_IMPLEMENTATION
#include "upf-ee/lib/stb_ds.h"
#undef STB_DS_IMPLEMENTATION
#include "EE-init.h"

#if CLIB_DEBUG > 1
#define flow_debug clib_warning
#else
#define flow_debug(...)				\
  do { } while (0)
#endif


void prepare_ee_data(flowtable_main_t *fm){
  VLIB_INIT_FUNCTION();
  clib_warning("[flow_info] let's see what is the bug!!!!!");
  flow_entry_t *flow;
  pthread_mutex_lock(&ee_lock);
  usage_report_per_flow_vector = NULL;

//  if (pthread_spin_lock (&fm->flows_lock) == 0) {
    u32 num_flows = vec_len(fm->flows);
    clib_warning("number of flows is %d", num_flows);
    u32 num = pool_len(fm->flows);
    clib_warning("number of flows with pool is %d", num);
    for(u32 i=0; i < num; i++){
      flow = pool_elt_at_index (fm->flows, i);
      if (flow->stats[0].pkts!=0 || flow->stats[1].pkts!=0){
        usage_report_per_flow_t new_data;
        flow_key_t key = flow->key;
        char buffer[INET6_ADDRSTRLEN];


        inet_ntop(AF_INET, &(key.ip[0]), buffer, sizeof(buffer));
        clib_warning("[1|flow_info] ip[0].  %s", buffer);
        new_data.src_ip = buffer;
        inet_ntop(AF_INET, &(key.ip[1]), buffer, sizeof(buffer));
        new_data.dst_ip = buffer;
        clib_warning("[1|flow_info] ip[1].  %s", buffer);
        clib_warning("[3| flow_info] port[0] %u", key.port[0]);
        clib_warning("[4| flow_info] port[1] %u", key.port[1]);
        clib_warning("[5| flow_info] portocol %u", key.proto);
        clib_warning("[6| flow_info] stst 0 pkts %d", flow->stats[0].pkts);
        clib_warning("[7| flow_info] stst 0 bytes %d", flow->stats[0].bytes);
        clib_warning("[8| flow_info] stst 1 pkts %d", flow->stats[1].pkts);
        clib_warning("[9| flow_info] stst 1 bytes %d", flow->stats[1].bytes);

        new_data.seid = key.seid;

        new_data.src_port = key.port[0];
        new_data.dst_port = key.port[1];
        new_data.proto = key.proto;
        new_data.src_pkts = flow->stats[0].pkts;
        new_data.src_bytes = flow->stats[0].bytes;
        new_data.dst_pkts = flow->stats[1].pkts;
        new_data.dst_bytes = flow->stats[1].bytes;
        vec_add1(usage_report_per_flow_vector,new_data);

      }
    }

  pthread_mutex_unlock(&ee_lock);

  return;
}

//
//static uword process_send_data(vlib_main_t *vm, vlib_node_runtime_t *rt, vlib_frame_t *f) {
//  f64 interval = 5.0;
//
//  while (1) {
//    prepare_ee_data();
//    vlib_process_wait_for_event_or_clock(vm, interval);
//    vlib_process_get_events(vm, NULL);
//  }
//
//  return 0;
//}
//VLIB_REGISTER_NODE(my_process_node) = {
//        .function = process_send_data,
//        .type = VLIB_NODE_TYPE_PROCESS,
//        .name = "periodic-sending-process",
//};
//
//
//static clib_error_t *my_init_function(vlib_main_t *vm) {
//  clib_warning("[flow_FATEMEH]inside the my_init_function");
//  return 0;
//}
//
//VLIB_INIT_FUNCTION(my_init_function);
//

//
//