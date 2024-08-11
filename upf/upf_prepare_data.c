//
// Created by Fatemeh Shafiei Ardestani on 2024-08-08.

#include "flowtable.h"
#include <vlib/vlib.h>
#include <vlib/unix/unix.h>
#include "upf-ee-types.h"


#if CLIB_DEBUG > 1
#define flow_debug clib_warning
#else
#define flow_debug(...)				\
  do { } while (0)
#endif


void prepare_ee_data(){
  clib_warning("[flow_info] let's see what is the bug!!!!!");
  flowtable_main_t *fm = &flowtable_main;
  flow_entry_t *flow;
  usage_report_per_flow_t *usage_report_per_flow_vector = NULL;
  if (pthread_spin_lock (&fm->flows_lock) == 0) {
    pool_elt_at_index (fm->flows, 0){
//    usage_report_per_flow_t new_data;
//    flow_key_t key = flow->key;
//    new_data.seid = key.inner.repr.seid;
//    new_data.src_ip = key.inner.repr.ip[0];
//    new_data.dst_ip = key.inner.repr.ip[1];
//    new_data.src_port = key.inner.repr.port[0];
//    new_data.dst_port = key.inner.repr.port[1];
//    new_data.proto = key.inner.repr.proto;
//    new_data.bytes = flow->stats.bytes;
//    new_data.pkts = flow->stats.pkts;
//    vecadd1(usage_report_per_flow_vectorl,new_data);
    flow_key_t key = flow->key;
    clib_warning("[1|flow_info] ip[0].  %s", key.ip[0]);
    clib_warning("[2| flow_info] ip[1]  %s", key.ip[1]);
    clib_warning("[3| flow_info] port[0] %s", key.port[0]);
    clib_warning("[4| flow_info] port[1] %s", key.port[1]);
    clib_warning("[5| flow_info] portocol %s", key.proto);
    clib_warning("[6| flow_info] stst 0 pkts %d", flow->stats[0].pkts);
    clib_warning("[7| flow_info] stst 0 bytes %d", flow->stats[0].bytes);
    clib_warning("[8| flow_info] stst 1 pkts %d", flow->stats[1].pkts);
    clib_warning("[9| flow_info] stst 1 bytes %d", flow->stats[1].bytes);

  }
  pthread_spin_unlock (&fm->flows_lock);
  }
  return;
}


static uword process_send_data(vlib_main_t *vm, vlib_node_runtime_t *rt, vlib_frame_t *f) {
  f64 interval = 5.0;

  while (1) {
    prepare_ee_data();
    vlib_process_wait_for_event_or_clock(vm, interval);
    vlib_process_get_events(vm, NULL);
  }

  return 0;
}
VLIB_REGISTER_NODE(my_process_node) = {
        .function = process_send_data,
        .type = VLIB_NODE_TYPE_PROCESS,
        .name = "periodic-sending-process",
};


static clib_error_t *my_init_function(vlib_main_t *vm) {
  clib_warning("[flow_FATEMEH]inside the my_init_function");
  return 0;
}

VLIB_INIT_FUNCTION(my_init_function);
