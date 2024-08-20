#include "ee-server.h"
void send_report(){
  vlib_main_t *vm = &vlib_global_main;

  while (1)
  {
    vlib_process_wait_for_event_or_clock (vm, 1 /* seconds */);
    EventReport_UDUT();
  }
}
VLIB_REGISTER_NODE(send_report_node) = {
        .function = send_report,
        .type = VLIB_NODE_TYPE_PROCESS,
        .name = "periodic-sending-process",
};