/*
 * Copyright (c) 2016 Qosmos and/or its affiliates.
 * Copyright (c) 2018 Travelping GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Modified by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.

#include <inttypes.h>

#include <vppinfra/error.h>
#include <vppinfra/hash.h>
#include <vnet/vnet.h>
#include <vnet/ip/ip.h>
#include <vnet/tcp/tcp.h>
#include <vnet/tcp/tcp_inlines.h>
#include <vnet/fib/ip4_fib.h>
#include <vnet/fib/ip6_fib.h>
#include <vnet/ethernet/ethernet.h>

#include <upf/upf.h>
#include <upf/upf_pfcp.h>
#include <upf/upf_proxy.h>

#if CLIB_DEBUG > 1
#define upf_debug clib_warning
#else
#define upf_debug(...)				\
  do { } while (0)
#endif

/* Statistics (not all errors) */
#define foreach_upf_proxy_error					\
  _(PROXY, "good packets proxy")				\
  _(LENGTH, "inconsistent ip/tcp lengths")			\
  _(NO_LISTENER, "no redirect server available")		\
  _(PROCESS, "good packets process")				\
  _(OPTIONS, "Could not parse options")				\
  _(CREATE_SESSION_FAIL, "Sessions couldn't be allocated")      \
  _(INVALID_FLOW, "flow entry not found")                       \
  _(CONNECTION_EXISTS, "connection already exists")

static char *upf_proxy_error_strings[] = {
#define _(sym,string) string,
  foreach_upf_proxy_error
#undef _
};

typedef enum
{
#define _(sym,str) UPF_PROXY_ERROR_##sym,
  foreach_upf_proxy_error
#undef _
    UPF_PROXY_N_ERROR,
} upf_proxy_error_t;

typedef enum
{
  UPF_PROXY_NEXT_DROP,
  UPF_PROXY_N_NEXT,
} upf_proxy_next_t;

typedef struct
{
  u32 session_index;
  u64 cp_seid;
  u32 pdr_idx;
  u8 packet_data[64 - 1 * sizeof (u32)];
}
upf_proxy_trace_t;

static u8 *
format_upf_proxy_accept_trace (u8 * s, va_list * args)
{
  CLIB_UNUSED (vlib_main_t * vm) = va_arg (*args, vlib_main_t *);
  CLIB_UNUSED (vlib_node_t * node) = va_arg (*args, vlib_node_t *);
  upf_proxy_trace_t *t = va_arg (*args, upf_proxy_trace_t *);
  u32 indent = format_get_indent (s);

  s =
    format (s,
	    "upf_session%d cp-seid 0x%016" PRIx64
	    " pdr %d\n%U%U", t->session_index, t->cp_seid,
	    t->pdr_idx, format_white_space, indent,
	    format_ip4_header, t->packet_data, sizeof (t->packet_data));
  return s;
}

/**
 * Accept a stream session. Optionally ping the server by callback.
 */
static int
proxy_session_stream_accept_notify (transport_connection_t * tc, u32 flow_id)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  app_worker_t *app_wrk;
  application_t *app;
  session_t *s;
  int rv;

  app = application_get (pm->server_app_index);
  if (!app)
    return -1;

  app_wrk = application_get_worker (app, 0 /* default wrk only */ );

  s = session_alloc_for_connection (tc);
  s->session_state = SESSION_STATE_CREATED;
  s->app_wrk_index = app_wrk->wrk_index;
  s->opaque = flow_id;

  clib_warning ("proxy session @ %p, app %p, wrk %p (idx %u), flow: 0x%08x",
	     s, app, app_wrk, app_wrk->wrk_index, flow_id);

  if ((rv = app_worker_init_connected (app_wrk, s)))
    {
      session_free (s);
      return rv;
    }

  session_lookup_add_connection (tc, session_handle (s));

  s->session_state = SESSION_STATE_ACCEPTING;
  if ((rv = app_worker_accept_notify (app_wrk, s)))
    {
      session_lookup_del_session (s);
      segment_manager_dealloc_fifos (s->rx_fifo, s->tx_fifo);
      session_free (s);
      return rv;
    }

  clib_warning ("proxy session flow: 0x%08x", s->opaque);
  return 0;
}

/**
 * Lookup transport connection
 */
static tcp_connection_t *
upf_tcp_lookup_connection (u32 fib_index, vlib_buffer_t * b, u8 thread_index,
			   u8 is_ip4)
{
  tcp_header_t *tcp;
  transport_connection_t *tconn;
  tcp_connection_t *tc;
  u8 is_filtered = 0;
  if (is_ip4)
    {
      ip4_header_t *ip4;
      ip4 = vlib_buffer_get_current (b);
      tcp = ip4_next_header (ip4);
      tconn = session_lookup_connection_wt4 (fib_index,
					     &ip4->dst_address,
					     &ip4->src_address,
					     tcp->dst_port,
					     tcp->src_port,
					     TRANSPORT_PROTO_TCP,
					     thread_index, &is_filtered);
      tc = tcp_get_connection_from_transport (tconn);
      /* ASSERT (tcp_lookup_is_valid (tc, b, tcp)); */
    }
  else
    {
      ip6_header_t *ip6;
      ip6 = vlib_buffer_get_current (b);
      tcp = ip6_next_header (ip6);
      tconn = session_lookup_connection_wt6 (fib_index,
					     &ip6->dst_address,
					     &ip6->src_address,
					     tcp->dst_port,
					     tcp->src_port,
					     TRANSPORT_PROTO_TCP,
					     thread_index, &is_filtered);
      tc = tcp_get_connection_from_transport (tconn);
      /* ASSERT (tcp_lookup_is_valid (tc, b, tcp)); */
    }
  return tc;
}

static_always_inline uword
upf_proxy_accept_inline (vlib_main_t * vm, vlib_node_runtime_t * node,
			 vlib_frame_t * from_frame, int is_ip4)
{
  upf_main_t *gtm = &upf_main;
  flowtable_main_t *fm = &flowtable_main;

  clib_warning("[FATEMEH] Got packet: %d", 10000013);
  upf_proxy_main_t *pm = &upf_proxy_main;
  u32 thread_index = vm->thread_index;
  u32 n_left_from, *from, *first_buffer;

  from = first_buffer = vlib_frame_vector_args (from_frame);
  n_left_from = from_frame->n_vectors;

  while (n_left_from > 0)
    {
      tcp_connection_t *old_conn, *child;
      flow_entry_t *flow;
      tcp_header_t *tcp;
      vlib_buffer_t *b;
      u32 flow_id;
      u32 fib_idx;
      u32 error = 0;
      u32 bi;

      bi = from[0];
      from += 1;
      n_left_from -= 1;

      b = vlib_get_buffer (vm, bi);
      UPF_CHECK_INNER_NODE (b);

      flow_id = upf_buffer_opaque (b)->gtpu.flow_id;
      clib_warning ("flow_id: 0x%08x", flow_id);
      flow = pool_elt_at_index (fm->flows, flow_id);
      ASSERT (flow);
      if (pool_is_free (gtm->sessions, gtm->sessions + flow->session_index))
	{
	  clib_warning ("The flow has sidx %d that refers to a dead session",
			flow->session_index);
	  error = UPF_PROXY_ERROR_INVALID_FLOW;
	  goto done;
	}

      /* make sure connection_index is invalid */
      vnet_buffer (b)->tcp.connection_index = ~0;
      tcp_input_lookup_buffer (b, thread_index, &error, is_ip4,
			       1 /* is_nolookup */ );
      clib_warning ("tcp_input_lookup error: %d", error);
      if (error != TCP_ERROR_NONE)
	goto done;

      tcp = tcp_buffer_hdr (b);
      clib_warning ("TCP SYN: %d", tcp_syn (tcp));
      if (PREDICT_FALSE (!tcp_syn (tcp)))
	{
	  error = UPF_PROXY_ERROR_NO_LISTENER;
	  goto done;
	}

      fib_idx = vlib_buffer_get_ip_fib_index (b, is_ip4);
      clib_warning ("FIB: %u", fib_idx);

      /* Make sure connection wasn't just created */
      old_conn = upf_tcp_lookup_connection (fib_idx, b, thread_index, is_ip4);
      if (PREDICT_FALSE (old_conn != NULL))
	{
	  clib_warning ("duplicate connection in upf-proxy-accept");
	  error = UPF_PROXY_ERROR_CONNECTION_EXISTS;
	  goto done;
	}

      /* Create child session and send SYN-ACK */
      child = tcp_connection_alloc (thread_index);

      if (tcp_options_parse (tcp, &child->rcv_opts, 1))
	{
	  error = UPF_PROXY_ERROR_OPTIONS;
	  tcp_connection_free (child);
	  goto done;
	}

      tcp_init_w_buffer (child, b, is_ip4);

      child->state = TCP_STATE_SYN_RCVD;
      child->c_fib_index = fib_idx;
      child->mss = pm->mss;
      child->cc_algo = tcp_cc_algo_get (TCP_CC_CUBIC);
      tcp_connection_init_vars (child);
      child->rto = TCP_RTO_MIN;

      child->next_node_index = is_ip4 ?
	pm->tcp4_server_output_next : pm->tcp6_server_output_next;
      child->next_node_opaque = flow_id;
      clib_warning ("Next Node: %u, Opaque: 0x%08x",
		 child->next_node_index, child->next_node_opaque);

      if (proxy_session_stream_accept_notify (&child->connection, flow_id))
	{
	  tcp_connection_cleanup (child);
	  error = UPF_PROXY_ERROR_CREATE_SESSION_FAIL;
	  goto done;
	}

      vnet_buffer (b)->tcp.connection_index = child->c_c_index;

      flow_tc (flow, FT_ORIGIN).conn_index = child->c_c_index;
      flow_tc (flow, FT_ORIGIN).thread_index = thread_index;

      child->tx_fifo_size = transport_tx_fifo_size (&child->connection);

      tcp_send_synack (child);

      TCP_EVT (TCP_EVT_SYN_RCVD, child, 1);

    done:
      if (PREDICT_FALSE (b->flags & VLIB_BUFFER_IS_TRACED))
	{
	  upf_proxy_trace_t *tr = vlib_add_trace (vm, node, b, sizeof (*tr));
	  clib_memcpy (tr->packet_data, vlib_buffer_get_current (b),
		       sizeof (tr->packet_data));
	}
    }

  vlib_buffer_free (vm, first_buffer, from_frame->n_vectors);

  return from_frame->n_vectors;
}

VLIB_NODE_FN (upf_ip4_proxy_accept_node) (vlib_main_t * vm,
					  vlib_node_runtime_t * node,
					  vlib_frame_t * from_frame)
{
  return upf_proxy_accept_inline (vm, node, from_frame, /* is_ip4 */ 1);
}

VLIB_NODE_FN (upf_ip6_proxy_accept_node) (vlib_main_t * vm,
					  vlib_node_runtime_t * node,
					  vlib_frame_t * from_frame)
{
  return upf_proxy_accept_inline (vm, node, from_frame, /* is_ip4 */ 0);
}

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (upf_ip4_proxy_accept_node) =
{
  .name = "upf-ip4-proxy-accept",
  .vector_size = sizeof (u32),
  .format_trace = format_upf_proxy_accept_trace,
  .type = VLIB_NODE_TYPE_INTERNAL,
  .n_errors = ARRAY_LEN (upf_proxy_error_strings),
  .error_strings = upf_proxy_error_strings,
  .n_next_nodes = 0,
};
/* *INDENT-ON* */

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (upf_ip6_proxy_accept_node) =
{
  .name = "upf-ip6-proxy-accept",
  .vector_size = sizeof (u32),
  .format_trace = format_upf_proxy_accept_trace,
  .type = VLIB_NODE_TYPE_INTERNAL,
  .n_errors = ARRAY_LEN (upf_proxy_error_strings),
  .error_strings = upf_proxy_error_strings,
  .n_next_nodes = 0,
};
/* *INDENT-ON* */

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
