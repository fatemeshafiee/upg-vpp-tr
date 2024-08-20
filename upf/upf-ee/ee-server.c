#include "ee-server.h"
vlib_main_t *vm = &vlib_global_main;

while (1)
{
  vlib_process_wait_for_event_or_clock (vm, 1 /* seconds */);
  vec_reset_length(event_data);
  EventReport_UDUT();
}