
// Added by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.
#include "ee_client.h"
void periodic_sending(){
  vlib_main_t *vm = &vlib_global_main;

  while (1)
  {
    vlib_process_wait_for_event_or_clock (vm, 1 /* seconds */);
    EventReport_UDUT();
  }
}
VLIB_REGISTER_NODE(send_report_node) = {
        .function = periodic_sending,
        .type = VLIB_NODE_TYPE_PROCESS,
        .name = "periodic-sending-process",
};