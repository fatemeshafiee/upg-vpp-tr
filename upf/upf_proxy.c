/*
* Copyright (c) 2018,2019 Travelping GmbH
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

#include <assert.h>
#include <vnet/vnet.h>
#include <vnet/tcp/tcp.h>
#include <vnet/session/application.h>
#include <vnet/session/application_interface.h>
#include <vnet/session/session.h>

#include "upf.h"
#include "upf_pfcp.h"
#include "upf_pfcp_api.h"
#include "upf_proxy.h"
#include "upf_app_db.h"

#if CLIB_DEBUG > 1
#define upf_debug clib_warning
#else
#define upf_debug(...)				\
  do { } while (0)
#endif

#define TCP_MSS 1460

typedef enum
{
  EVENT_WAKEUP = 1,
} http_process_event_t;

upf_proxy_main_t upf_proxy_main;

static inline u32
proxy_session_index (upf_proxy_session_t * ps)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  return ps ? ps - pm->sessions : ~0;
}

static void proxy_session_try_close_unlocked (upf_proxy_session_t * ps);
static void
proxy_session_try_close_vpp_session (session_t * s, int is_active_open);
static session_t *session_from_proxy_session_get_if_valid (upf_proxy_session_t
							   * ps,
							   int
							   is_active_open);

u8 *
format_upf_proxy_session (u8 * s, va_list * args)
{
  upf_proxy_session_t *ps = va_arg (*args, upf_proxy_session_t *);
  session_t *ao, *p;

  if (!ps)
    return format (s, "(NULL)");

  p = session_from_proxy_session_get_if_valid (ps, 0);
  ao = session_from_proxy_session_get_if_valid (ps, 1);

  s = format (s, "Idx %u @ %p, Clnt: [%u:%u], Srv: [%u:%u], Flow Idx: %u\n",
	      ps->session_index, ps, ps->proxy_thread_index,
	      ps->proxy_session_index, ps->active_open_thread_index,
	      ps->active_open_session_index, ps->flow_index);
  if (p)
    s = format (s, "Clnt: %U\n", format_session, p, 0);
  if (ao)
    s = format (s, "Srv:  %U\n", format_session, ao, 0);

  return s;
}

static inline u32
proxy_app_index (upf_proxy_main_t * pm, u8 is_active_open)
{
  return is_active_open ? pm->active_open_app_index : pm->server_app_index;
}

static upf_proxy_session_t *
proxy_session_alloc (u32 thread_index)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  upf_proxy_session_t *ps;

  pool_get (pm->sessions, ps);
  clib_memset (ps, 0, sizeof (*ps));
  ps->session_index = ps - pm->sessions;

  ps->proxy_session_index = ~0;
  ps->active_open_session_index = ~0;
  ps->flow_index = ~0;

  return ps;
}

static void
proxy_session_free (upf_proxy_session_t * ps)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  flowtable_main_t *fm = &flowtable_main;

  clib_warning("[FATEMEH] Got packet: %d", 10000008);

  flow_entry_t *flow;
  if (ps->flow_index != ~0)
    {
      flow = flowtable_get_flow (fm, ps->flow_index);
      flow->ps_index = ~0;
    }
  vec_free (ps->rx_buf);
  pool_put (pm->sessions, ps);
  if (CLIB_DEBUG)
    memset (ps, 0xfa, sizeof (*ps));
}

static void
proxy_session_put (upf_proxy_session_t * ps)
{
  ASSERT (ps);

  if (--ps->refcnt == 0)
    proxy_session_free (ps);
}

static void
proxy_session_lookup_add (session_t * s, upf_proxy_session_t * ps)
{
  upf_proxy_main_t *pm = &upf_proxy_main;

  vec_validate_init_empty (pm->session_to_proxy_session[s->thread_index],
			   s->session_index, ~0);
  pm->session_to_proxy_session[s->thread_index][s->session_index] =
    ps->session_index;
  ps->refcnt++;
}

static void
proxy_session_lookup_del (session_t * s)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  upf_proxy_session_t *ps;
  u32 ps_index;

  ASSERT (s->session_index <
	  vec_len (pm->session_to_proxy_session[s->thread_index]));

  ps_index = pm->session_to_proxy_session[s->thread_index][s->session_index];
  ps = proxy_session_get (ps_index);
  if (ps)
    {
      pm->session_to_proxy_session[s->thread_index][s->session_index] = ~0;
      proxy_session_put (ps);
    }
}

static upf_proxy_session_t *
proxy_session_lookup (session_t * s)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  u32 ps_index;

  if (s->session_index <
      vec_len (pm->session_to_proxy_session[s->thread_index]))
    {
      ps_index =
	pm->session_to_proxy_session[s->thread_index][s->session_index];
      return proxy_session_get (ps_index);
    }
  return 0;
}

static void
active_open_session_lookup_add (session_t * s, upf_proxy_session_t * ps)
{
  upf_proxy_main_t *pm = &upf_proxy_main;

  vec_validate_init_empty (pm->session_to_active_open_session
			   [s->thread_index], s->session_index, ~0);
  pm->session_to_active_open_session[s->thread_index][s->session_index] =
    ps->session_index;
  /*
   * Not increasing ps->refcnt here b/c it was increased when
   * initiating the active open connection
   */
}

static void
active_open_session_lookup_del (session_t * s)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  upf_proxy_session_t *ps;
  u32 ps_index;

  ASSERT (s->session_index <
	  vec_len (pm->session_to_active_open_session[s->thread_index]));

  ps_index =
    pm->session_to_active_open_session[s->thread_index][s->session_index];
  ps = proxy_session_get (ps_index);
  if (ps)
    {
      pm->session_to_active_open_session[s->thread_index][s->session_index] =
	~0;
      proxy_session_put (ps);
    }
}

static upf_proxy_session_t *
active_open_session_lookup (session_t * s)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  u32 ps_index;

  if (s->session_index <
      vec_len (pm->session_to_active_open_session[s->thread_index]))
    {
      ps_index =
	pm->session_to_active_open_session[s->thread_index][s->session_index];
      return proxy_session_get (ps_index);
    }
  return 0;
}

static upf_proxy_session_t *
ps_session_lookup (session_t * s, int is_active_open)
{
  if (!is_active_open)
    return proxy_session_lookup (s);
  else
    return active_open_session_lookup (s);
}

static void
ps_session_lookup_del (session_t * s, int is_active_open)
{
  if (!is_active_open)
    proxy_session_lookup_del (s);
  else
    active_open_session_lookup_del (s);
}

static session_t *
session_from_proxy_session_get_if_valid (upf_proxy_session_t * ps,
					 int is_active_open)
{
  if (!ps)
    return 0;

  if (!is_active_open)
    return ps->proxy_session_index == ~0 ? 0 :
      session_get_if_valid (ps->proxy_session_index, ps->proxy_thread_index);
  else
    return ps->active_open_session_index == ~0 ? 0 :
      session_get_if_valid (ps->active_open_session_index,
			    ps->active_open_thread_index);
}

static_always_inline void
rx_callback_inline (svm_fifo_t * tx_fifo)
{
  /*
   * Send event for server tx fifo
   */
  if (svm_fifo_set_event (tx_fifo))
    {
      u8 thread_index = tx_fifo->master_thread_index;
      u32 session_index = tx_fifo->master_session_index;
      if (session_send_io_evt_to_thread_custom (&session_index,
						thread_index,
						SESSION_IO_EVT_TX))
	clib_warning ("failed to enqueue tx evt");
    }

  if (svm_fifo_max_enqueue (tx_fifo) <= TCP_MSS)
    svm_fifo_add_want_deq_ntf (tx_fifo, SVM_FIFO_WANT_DEQ_NOTIF);
}

static_always_inline int
tx_callback_inline (session_t * s, int is_active_open)
{
  transport_connection_t *tc;
  upf_proxy_session_t *ps;
  session_t *tx;
  u32 min_free;

  min_free = clib_min (svm_fifo_size (s->tx_fifo) >> 3, 128 << 10);
  if (svm_fifo_max_enqueue (s->tx_fifo) < min_free)
    {
      svm_fifo_add_want_deq_ntf (s->tx_fifo, SVM_FIFO_WANT_DEQ_NOTIF);
      return 0;
    }

  proxy_server_sessions_reader_lock ();

  ps = ps_session_lookup (s, is_active_open);
  if (!ps || ps->po_disconnected || ps->ao_disconnected)
    goto out_unlock;

  tx = session_from_proxy_session_get_if_valid (ps, !is_active_open);
  if (!tx)
    goto out_unlock;

  ASSERT (tx != s);

  /* Force ack on other side to update rcv wnd */
  tc = session_get_transport (tx);
  /*
     FIXME: band-aid: this should not happen but it does.
     Possibly bad session cleanup somewhere
   */
  if (!tc)
    clib_warning ("session_get_transport (tx) == NULL");
  else
    tcp_send_ack ((tcp_connection_t *) tc);

out_unlock:
  proxy_server_sessions_reader_unlock ();

  return 0;
}

static void
proxy_start_connect_fn (const u32 * session_index)
{
  upf_main_t *gtm = &upf_main;
  upf_proxy_main_t *pm = &upf_proxy_main;
  flowtable_main_t *fm = &flowtable_main;


  clib_warning("[FATEMEH] Got packet: %d", 10000018);

  vnet_connect_args_t _a, *a = &_a;
  ip46_address_t *src, *dst;
  upf_proxy_session_t *ps;
  struct rules *active;
  flow_entry_t *flow;
  upf_session_t *sx;
  upf_pdr_t *pdr;
  upf_far_t *far;
  int is_ip4;
  int rv;

  proxy_server_sessions_reader_lock ();

  ps = proxy_session_get (*session_index);
  ASSERT (ps);
  ASSERT (ps->active_open_establishing);

  if (pool_is_free_index (fm->flows, ps->flow_index))
    {
      /*
       * Not going to connect, decrement the refcount
       * that was incremented in proxy_start_connect()
       */
      ps->refcnt--;
      ps->active_open_establishing = 0;
      goto out;
    }

  /* TODO: RACE: should not access the flow on the main thread */
  flow = pool_elt_at_index (fm->flows, ps->flow_index);
  sx = pool_elt_at_index (gtm->sessions, flow->session_index);
  if (sx->generation != flow->generation)
    {
      ps->refcnt--;
      ps->active_open_establishing = 0;
      goto out;
    }
  active = pfcp_get_rules (sx, PFCP_ACTIVE);

  src = &flow->key.ip[FT_ORIGIN ^ flow->is_reverse];
  dst = &flow->key.ip[FT_REVERSE ^ flow->is_reverse];
  is_ip4 = ip46_address_is_ip4 (dst);

  ASSERT (flow_pdr_id (flow, FT_ORIGIN) != ~0);
  pdr = pfcp_get_pdr_by_id (active, flow_pdr_id (flow, FT_ORIGIN));
  ASSERT (pdr);
  far = pfcp_get_far_by_id (active, pdr->far_id);

  memset (a, 0, sizeof (*a));
  a->api_context = *session_index;
  a->app_index = pm->active_open_app_index;
  a->sep_ext = (session_endpoint_cfg_t) SESSION_ENDPOINT_CFG_NULL;
  upf_nwi_if_and_fib_index (gtm, is_ip4 ? FIB_PROTOCOL_IP4 : FIB_PROTOCOL_IP6,
			    far->forward.nwi_index,
			    &a->sep_ext.sw_if_index, &a->sep_ext.fib_index);
  a->sep_ext.transport_proto = TRANSPORT_PROTO_TCP;
  a->sep_ext.mss = pm->mss;
  a->sep_ext.is_ip4 = is_ip4;
  a->sep_ext.ip = *dst;
  a->sep_ext.port = flow->key.port[FT_REVERSE ^ flow->is_reverse];
  a->sep_ext.peer.fib_index = a->sep_ext.fib_index;
  a->sep_ext.peer.sw_if_index = a->sep_ext.sw_if_index;
  a->sep_ext.peer.is_ip4 = is_ip4;
  a->sep_ext.peer.ip = *src;
  a->sep_ext.peer.port = flow->key.port[FT_ORIGIN ^ flow->is_reverse];

  if ((rv = vnet_connect (a)) != 0)
    {
      clib_warning ("vnet_connect() failed: %d", rv);
      ps->refcnt--;
      ps->active_open_establishing = 0;
    }

out:
  proxy_server_sessions_reader_unlock ();
}

static void
proxy_start_connect (upf_proxy_session_t * ps)
{
  clib_warning ("psidx %d", proxy_session_index (ps));

  ps->active_open_establishing = 1;
  /*
   * Increase refcount here. It will be decremented after active open
   * session is cleaned up
   */
  ps->refcnt++;
  if (vlib_get_thread_index () == 0)
    {
      proxy_start_connect_fn (&ps->session_index);
    }
  else
    {
      vl_api_rpc_call_main_thread (proxy_start_connect_fn,
				   (u8 *) & ps->session_index,
				   sizeof (ps->session_index));
    }
}

static const char *upf_proxy_template =
  "HTTP/1.1 302 OK\r\n"
  "Location: %v\r\n"
  "Content-Type: text/html\r\n"
  "Cache-Control: private, no-cache, must-revalidate\r\n"
  "Expires: Mon, 11 Jan 1970 10:10:10 GMT\r\n"
  "Connection: close\r\n"
  "Pragma: no-cache\r\n" "Content-Length: %d\r\n\r\n%v";

static const char *http_error_template =
  "HTTP/1.1 %s\r\n"
  "Content-Type: text/html\r\n"
  "Cache-Control: private, no-cache, must-revalidate\r\n"
  "Expires: Mon, 11 Jan 1970 10:10:10 GMT\r\n"
  "Connection: close\r\n" "Pragma: no-cache\r\n" "Content-Length: 0\r\n\r\n";

static const char *wispr_proxy_template =
  "<!--\n"
  "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
  "<WISPAccessGatewayParam"
  " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
  " xsi:noNamespaceSchemaLocation=\"http://www.acmewisp.com/WISPAccessGatewayParam.xsd\">"
  "<Proxy>"
  "<MessageType>110</MessageType>"
  "<ResponseCode>200</ResponseCode>"
  "<NextURL>%v</NextURL>" "</Proxy>" "</WISPAccessGatewayParam>\n" "-->\n";

static const char *html_redirect_template =
  "<!DOCTYPE html>\n"
  "<html>\n"
  "%v"
  "   <head>\n"
  "      <title>Redirection</title>\n"
  "      <meta http-equiv=\"refresh\" content=\"0; URL=%v\">\n"
  "   </head>\n"
  "   <body>\n"
  "      Please <a href='%v'>click here</a> to continue\n"
  "   </body>\n" "</html>\n";

static void
http_redir_send_data (upf_proxy_session_t * ps, session_t * s, u8 * data)
{
  vlib_main_t *vm = vlib_get_main ();
  f64 last_sent_timer = vlib_time_now (vm);
  u32 offset, bytes_to_send;
  f64 delay = 10e-3;

  bytes_to_send = vec_len (data);
  offset = 0;

  while (bytes_to_send > 0)
    {
      int actual_transfer;

      actual_transfer = svm_fifo_enqueue
	(s->tx_fifo, bytes_to_send, data + offset);

      /* Made any progress? */
      if (actual_transfer <= 0)
	{
	  vlib_process_suspend (vm, delay);
	  /* 10s deadman timer */
	  if (vlib_time_now (vm) > last_sent_timer + 10.0)
	    {
	      proxy_session_try_close_unlocked (ps);
	      break;
	    }
	  /* Exponential backoff, within reason */
	  if (delay < 1.0)
	    delay = delay * 2.0;
	}
      else
	{
	  last_sent_timer = vlib_time_now (vm);
	  offset += actual_transfer;
	  bytes_to_send -= actual_transfer;

	  if (svm_fifo_set_event (s->tx_fifo))
	    session_send_io_evt_to_thread (s->tx_fifo,
					   SESSION_IO_EVT_TX_FLUSH);
	  delay = 10e-3;
	}
    }
}

static void
send_error (upf_proxy_session_t * ps, session_t * s, char *str)
{
  u8 *data;

  data = format (0, http_error_template, str);
  http_redir_send_data (ps, s, data);
  vec_free (data);
}

static void
proxy_session_try_close_unlocked (upf_proxy_session_t * ps)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  vnet_disconnect_args_t _a, *a = &_a;
  session_t *s;
  int rv;

  clib_warning ("psidx %d", proxy_session_index (ps));

  /* if active open side is being established, it has to be closed later */
  if (!ps->ao_disconnected && !ps->active_open_establishing)
    {
      s = session_from_proxy_session_get_if_valid (ps, 1);
      /* it can be that active open session is not initiated yet */
      if (s)
	{
	  clib_warning ("closing active open: sidx %d psidx %d",
		     s->session_index, proxy_session_index (ps));
	  a->handle = session_handle (s);
	  a->app_index = proxy_app_index (pm, 1);
	  if ((rv = vnet_disconnect_session (a)) != 0)
	    {
	      clib_warning
		("vnet_disconnect_session() failed for active open: %d", rv);
	      ASSERT (0);	// TODO: rm
	    }
	  ps->ao_disconnected = 1;
	}
    }

  if (!ps->po_disconnected)
    {
      s = session_from_proxy_session_get_if_valid (ps, 0);
      /* passive side must always be there */
      ASSERT (s);
      clib_warning ("closing passive: sidx %d psidx %d", s->session_index,
		 proxy_session_index (ps));
      a->handle = session_handle (s);
      a->app_index = proxy_app_index (pm, 0);
      if ((rv = vnet_disconnect_session (a)) != 0)
	{
	  clib_warning ("vnet_disconnect_session() failed for passive: %d",
			rv);
	  ASSERT (0);		// TODO: rm
	}
      ps->po_disconnected = 1;
    }
}

static void
proxy_session_try_close_vpp_session (session_t * s, int is_active_open)
{
  upf_proxy_session_t *ps = 0;

  proxy_server_sessions_writer_lock ();
  ps = ps_session_lookup (s, is_active_open);
  clib_warning ("sidx %d psidx %d", s->session_index, proxy_session_index (ps));
  ASSERT (ps != 0);
  proxy_session_try_close_unlocked (ps);
  proxy_server_sessions_writer_unlock ();
}

static void
session_cleanup (session_t * s, session_cleanup_ntf_t ntf, int is_active_open)
{
  flowtable_main_t *fm = &flowtable_main;

  clib_warning("[FATEMEH] Got packet: %d", 10000009);
  upf_proxy_session_t *ps;
  flow_entry_t *flow;
  flow_tc_t *ftc;
  u32 flow_index;

  proxy_server_sessions_writer_lock ();

  ps = ps_session_lookup (s, is_active_open);
  clib_warning ("sidx %d psidx %d", s->session_index, proxy_session_index (ps));
  if (!ps)
    goto out_unlock;

  /*
   * Make sure the corresponding side is marked as disconnected.  When
   * a connection is stitched and the corresponding session and the
   * transport connection are deleted without closing the real TCP connection,
   * cleanup callback will be called without preceding disconnect/reset
   * callback.  We mark the specified side as disconnected here to prevent any
   * attempts of data transfer through the proxy at that
   * point. Otherwise, a late session event can cause a crash while
   * trying to use SVM FIFO.
   */
  if (is_active_open)
    ps->ao_disconnected = 1;
  else
    ps->po_disconnected = 1;

  flow_index = ps->flow_index;
  ps_session_lookup_del (s, is_active_open);

  /* ps might be invalid after this point */

  if (pool_is_free_index (fm->flows, flow_index))
    goto out_unlock;
  flow = pool_elt_at_index (fm->flows, flow_index);

  ftc = &flow_tc (flow, is_active_open ? FT_REVERSE : FT_ORIGIN);
  ftc->conn_index = ~0;

out_unlock:
  proxy_server_sessions_writer_unlock ();
}

static int
common_fifo_tuning_callback (session_t * s, svm_fifo_t * f,
			     session_ft_action_t act, u32 bytes)
{
  upf_proxy_main_t *pm = &upf_proxy_main;

  clib_warning ("sidx %d", s->session_index);

  segment_manager_t *sm = segment_manager_get (f->segment_manager);
  fifo_segment_t *fs = segment_manager_get_segment (sm, f->segment_index);

  u8 seg_usage = fifo_segment_get_mem_usage (fs);
  u32 fifo_in_use = svm_fifo_max_dequeue_prod (f);
  u32 fifo_size = svm_fifo_size (f);
  u32 fifo_usage = ((u64) fifo_in_use * 100) / (u64) fifo_size;
  u32 update_size = 0;

  ASSERT (act < SESSION_FT_ACTION_N_ACTIONS);

  if (act == SESSION_FT_ACTION_ENQUEUED)
    {
      if (seg_usage < pm->low_watermark && fifo_usage > 50)
	update_size = fifo_in_use;
      else if (seg_usage < pm->high_watermark && fifo_usage > 80)
	update_size = fifo_in_use;

      update_size = clib_min (update_size, sm->max_fifo_size - fifo_size);
      if (update_size)
	svm_fifo_set_size (f, fifo_size + update_size);
    }
  else				/* dequeued */
    {
      if (seg_usage > pm->high_watermark || fifo_usage < 20)
	update_size = bytes;
      else if (seg_usage > pm->low_watermark && fifo_usage < 50)
	update_size = (bytes / 2);

      ASSERT (fifo_size >= 4096);
      update_size = clib_min (update_size, fifo_size - 4096);
      if (update_size)
	svm_fifo_set_size (f, fifo_size - update_size);
    }

  return 0;
}

static int
proxy_accept_callback (session_t * s)
{
  flowtable_main_t *fm = &flowtable_main;

  clib_warning("[FATEMEH] Got packet: %d", 10000010);
  upf_proxy_session_t *ps;
  flow_entry_t *flow;

  proxy_server_sessions_writer_lock ();

  if ((ps = proxy_session_lookup (s)))
    {
      clib_warning ("DUP accept: sidx %d psidx %d", s->session_index,
		 proxy_session_index (ps));
      ASSERT (ps->flow_index == s->opaque);
      goto out_unlock;
    }

  ps = proxy_session_alloc (s->thread_index);
  proxy_session_lookup_add (s, ps);
  clib_warning ("NEW: sidx %d psidx %d", s->session_index,
	     proxy_session_index (ps));

  ps->rx_fifo = s->rx_fifo;
  ps->tx_fifo = s->tx_fifo;
  ps->proxy_session_index = s->session_index;
  ps->proxy_thread_index = s->thread_index;

  ps->flow_index = s->opaque;
  flow = flowtable_get_flow (fm, ps->flow_index);
  flow->ps_index = proxy_session_index (ps);

  //TBD ps->session_state = PROXY_STATE_ESTABLISHED;
  //TBD proxy_server_session_timer_start (ps);

out_unlock:
  proxy_server_sessions_writer_unlock ();

  s->session_state = SESSION_STATE_READY;
  return 0;
}

static void
proxy_disconnect_callback (session_t * s)
{
  clib_warning ("sidx %d", s->session_index);
  proxy_session_try_close_vpp_session (s, 0 /* is_active_open */ );
}

static void
proxy_reset_callback (session_t * s)
{
  clib_warning ("sidx %d", s->session_index);
  clib_warning ("Reset session %U", format_session, s, 2);
  proxy_session_try_close_vpp_session (s, 0 /* is_active_open */ );
}

static void
proxy_session_cleanup_callback (session_t * s, session_cleanup_ntf_t ntf)
{
  clib_warning ("sidx %d", s->session_index);
  session_cleanup (s, ntf, 0 /* is_active_open */ );
}

static int
proxy_connected_callback (u32 app_index, u32 api_context,
			  session_t * s, session_error_t err)
{
  clib_warning ("called...");
  return -1;
}

static int
proxy_add_segment_callback (u32 client_index, u64 segment_handle)
{
  clib_warning ("called...");
  return 0;
}

static int
proxy_rx_request (upf_proxy_session_t * ps)
{
  u32 max_dequeue;
  int n_read;

  clib_warning ("psidx %d", proxy_session_index (ps));

  max_dequeue = clib_min (svm_fifo_max_dequeue_cons (ps->rx_fifo), 4096);
  if (PREDICT_FALSE (max_dequeue == 0))
    return -1;

  vec_validate (ps->rx_buf, max_dequeue);
  n_read = app_recv_stream_raw (ps->rx_fifo, ps->rx_buf,
				max_dequeue, 0, 1 /* peek */ );
  ASSERT (n_read == max_dequeue);

  _vec_len (ps->rx_buf) = n_read;
  return 0;
}

static int
proxy_send_redir (session_t * s, upf_proxy_session_t * ps,
		  flow_entry_t * flow, upf_session_t * sx,
		  struct rules *active)
{
  upf_pdr_t *pdr;
  upf_far_t *far;
  u8 *request = 0;
  u8 *wispr, *html, *http, *url;
  int i;

  pdr = pfcp_get_pdr_by_id (active, flow_pdr_id (flow, FT_ORIGIN));
  far = pfcp_get_far_by_id (active, pdr->far_id);

  /* Edge case: session modified, redirect no longer applicable */
  if (!(far && far->forward.flags & FAR_F_REDIRECT_INFORMATION))
    return -1;

  clib_warning ("sidx %d psidx %d", s->session_index, proxy_session_index (ps));

  svm_fifo_dequeue_drop_all (ps->rx_fifo);
  svm_fifo_unset_event (ps->rx_fifo);

  request = ps->rx_buf;
  if (vec_len (request) < 6)
    {
      send_error (ps, s, "400 Bad Request");
      goto out;
    }

  for (i = 0; i < vec_len (request) - 4; i++)
    {
      if (request[i] == 'G' &&
	  request[i + 1] == 'E' &&
	  request[i + 2] == 'T' && request[i + 3] == ' ')
	goto found;
    }
  send_error (ps, s, "400 Bad Request");
  goto out;

found:
  /* Send it */
  url = far->forward.redirect_information.uri;
  wispr = format (0, wispr_proxy_template, url);
  html = format (0, html_redirect_template, wispr, url, url);
  http = format (0, upf_proxy_template, url, vec_len (html), html);

  http_redir_send_data (ps, s, http);

  vec_free (http);
  vec_free (html);
  vec_free (wispr);

out:
  proxy_session_try_close_unlocked (ps);
  return 0;
}

static int
proxy_rx_callback_static (session_t * s, upf_proxy_session_t * ps)
{
  upf_main_t *gtm = &upf_main;
  flowtable_main_t *fm = &flowtable_main;

  clib_warning("[FATEMEH] Got packet: %d", 10000012);
  struct rules *active;
  flow_entry_t *flow;
  upf_session_t *sx;
  adr_result_t r;
  int rv;

  clib_warning ("sidx %d psidx %d", s->session_index, proxy_session_index (ps));

  rv = proxy_rx_request (ps);
  if (rv)
    return rv;

  flow = pool_elt_at_index (fm->flows, ps->flow_index);
  sx = pool_elt_at_index (gtm->sessions, flow->session_index);
  if (sx->generation != flow->generation)
    {
      clib_warning ("flow PDR info outdated, close incoming session");
      proxy_session_try_close_unlocked (ps);
      return 0;
    }
  active = pfcp_get_rules (sx, PFCP_ACTIVE);

  /*
   * 4 possibilities here:
   * a) the flow should be answered with a redirect
   * b) the flow uses app detection
   * c) none of (a) and (b) but one of them used to be true
   *    but (a) once was true
   * d) none of (a) and (b) but one of them used to be true
   *    but (b) once was true
   */
  if (flow->is_redirect)
    {
      /* if we get here, it has to be (a) or (c) */
      if (!proxy_send_redir (s, ps, flow, sx, active))
	return 0;
      flow->is_redirect = 0;	/* (c), but maybe now it needs app detection? */
    }

  if (!(active->flags & PFCP_ADR))
    {
      /*
       * Edge case (c) or (d): the session has been modified, this flow doesn't
       * really need the proxy anymore, but has already been proxied
       */
      clib_warning ("connect outgoing session");

      flow_next (flow, FT_ORIGIN) =
	flow_next (flow, FT_REVERSE) = FT_NEXT_PROXY;

      /* start outgoing connect to server */
      proxy_start_connect (ps);

      return 0;
    }

  /* proceed with app detection (a) */
  clib_warning ("proxy: %v", ps->rx_buf);
  r = upf_application_detection (gtm->vlib_main, ps->rx_buf, flow, active);
  clib_warning ("r: %d", r);
  switch (r)
    {
    case ADR_NEED_MORE_DATA:
      clib_warning ("ADR_NEED_MORE_DATA");

      /* abort ADR scan after 4k of data */
      if (svm_fifo_max_dequeue_cons (ps->rx_fifo) < 4096)
	break;

      /* FALL-THRU */

    case ADR_FAIL:
      clib_warning ("ADR_FAIL, close incoming session");
      proxy_session_try_close_unlocked (ps);
      break;

    case ADR_OK:
      clib_warning ("connect outgoing session");

      /* we are done with scanning for PDRs */
      flow_next (flow, FT_ORIGIN) =
	flow_next (flow, FT_REVERSE) = FT_NEXT_PROXY;

      /* start outgoing connect to server */
      proxy_start_connect (ps);

      break;
    }

  return 0;
}

static int
proxy_rx_callback (session_t * s)
{
  u32 thread_index = vlib_get_thread_index ();
  upf_proxy_session_t *ps;

  clib_warning ("sidx %d", s->session_index);
  ASSERT (s->thread_index == thread_index);

  proxy_server_sessions_reader_lock ();

  ps = proxy_session_lookup (s);
  if (!ps)
    {
      proxy_server_sessions_reader_unlock ();
      return -1;
    }

  if (ps->po_disconnected || ps->ao_disconnected)
    {
      clib_warning ("late rx callback: sidx %d psidx %d", s->session_index,
		 proxy_session_index (ps));
      proxy_server_sessions_reader_unlock ();
      return -1;
    }

  clib_warning ("sidx %d psidx %d", s->session_index, proxy_session_index (ps));
  if (ps->active_open_session_index != ~0 && !ps->ao_disconnected)
    {
      proxy_server_sessions_reader_unlock ();

      rx_callback_inline (s->rx_fifo);
    }
  else
    {
      proxy_server_sessions_reader_unlock ();

      return proxy_rx_callback_static (s, ps);
    }

  return 0;
}

static int
proxy_tx_callback (session_t * s)
{
  clib_warning ("sidx %d", s->session_index);
  return tx_callback_inline (s, 0);
}

static session_cb_vft_t proxy_session_cb_vft = {
  .session_accept_callback = proxy_accept_callback,
  .session_disconnect_callback = proxy_disconnect_callback,
  .session_connected_callback = proxy_connected_callback,
  .add_segment_callback = proxy_add_segment_callback,
  .builtin_app_rx_callback = proxy_rx_callback,
  .builtin_app_tx_callback = proxy_tx_callback,
  .session_reset_callback = proxy_reset_callback,
  .session_cleanup_callback = proxy_session_cleanup_callback,
  .fifo_tuning_callback = common_fifo_tuning_callback
};

static int
active_open_connected_callback (u32 app_index, u32 opaque,
				session_t * s, session_error_t err)
{
  u8 thread_index = vlib_get_thread_index ();
  upf_proxy_main_t *pm = &upf_proxy_main;
  flowtable_main_t *fm = &flowtable_main;


  clib_warning("[FATEMEH] Got packet: %d", 10000013);

  upf_proxy_session_t *ps;

  clib_warning ("sidx %d psidx %d", s ? s->session_index : 0, opaque);

  /*
   * Setup proxy session handle.
   */
  proxy_server_sessions_writer_lock ();

  ps = pool_elt_at_index (pm->sessions, opaque);
  ASSERT (!ps->ao_disconnected);
  ASSERT (ps->active_open_establishing);

  ps->active_open_establishing = 0;

  if (err)
    {
      /*
       * Upon active open connection failure, we close the passive
       * side, too
       */
      clib_warning ("sidx %d psidx %d: connection failed!",
		 s ? s->session_index : -1, opaque);
      ps->ao_disconnected = 1;
      proxy_session_try_close_unlocked (ps);
      ASSERT (ps->po_disconnected);
    }

  if (ps->po_disconnected)
    {
      upf_debug
	("sidx %d psidx %d: passive open side disconnected, closing active open connection",
	 s ? s->session_index : -1, opaque);
      ps->ao_disconnected = 1;
      proxy_server_sessions_writer_unlock ();
      proxy_session_put (ps);
      /*
       * Returning -1 here will cause the active open side to be
       * closed immediatelly. Cleanup callback will not be invoked.
       */
      return -1;
    }

  ASSERT (s);
  ASSERT (!active_open_session_lookup (s));

  ps->active_open_session_index = s->session_index;
  ps->active_open_thread_index = s->thread_index;
  active_open_session_lookup_add (s, ps);

  s->tx_fifo = ps->rx_fifo;
  s->rx_fifo = ps->tx_fifo;

  /*
   * Reset the active-open tx-fifo master indices so the active-open session
   * will receive data, etc.
   */
  s->tx_fifo->master_session_index = s->session_index;
  s->tx_fifo->master_thread_index = s->thread_index;

  /*
   * Account for the active-open session's use of the fifos
   * so they won't disappear until the last session which uses
   * them disappears
   */
  s->tx_fifo->refcnt++;
  s->rx_fifo->refcnt++;

  if (!pool_is_free_index (fm->flows, ps->flow_index))
    {
      transport_connection_t *tc;
      flow_entry_t *flow;
      flow_tc_t *ftc;

      flow = pool_elt_at_index (fm->flows, ps->flow_index);
      ASSERT (flow->ps_index == opaque);
      tc = session_get_transport (s);

      ASSERT (tc->thread_index == thread_index);

      ftc = &flow_tc (flow, FT_REVERSE);
      ftc->conn_index = tc->c_index;
      ftc->thread_index = tc->thread_index;
    }

  proxy_server_sessions_writer_unlock ();

  clib_warning ("sidx %d psidx %d: max tx dequeue %d",
	     s->session_index, opaque, svm_fifo_max_dequeue (s->tx_fifo));
  /*
   * Send event for active open tx fifo
   *  ... we left the so far received data in rx fifo,
   *  this will therefore forward that data...
   */
  ASSERT (s->thread_index == thread_index);
  if (svm_fifo_set_event (s->tx_fifo))
    {
      clib_warning ("sidx %d psidx %d: sending TX event",
		 s->session_index, opaque);
      session_send_io_evt_to_thread (s->tx_fifo, SESSION_IO_EVT_TX);
    }
  else
    {
      clib_warning ("sidx %d psidx %d: NOT sending TX event",
		 s->session_index, opaque);
    }

  return 0;
}

static void
active_open_reset_callback (session_t * s)
{
  clib_warning ("sidx %d", s->session_index);
  proxy_session_try_close_vpp_session (s, 1 /* is_active_open */ );
}

static void
active_open_session_cleanup_callback (session_t * s,
				      session_cleanup_ntf_t ntf)
{
  clib_warning ("sidx %d", s->session_index);
  session_cleanup (s, ntf, 1 /* is_active_open */ );
}

static int
active_open_create_callback (session_t * s)
{
  clib_warning ("sidx %d", s->session_index);
  return 0;
}

static void
active_open_disconnect_callback (session_t * s)
{
  clib_warning ("sidx %d", s->session_index);
  proxy_session_try_close_vpp_session (s, 1 /* is_active_open */ );
}

static int
active_open_add_segment_callback (u32 client_index, u64 segment_handle)
{
  clib_warning ("called...");
  return 0;
}

static int
active_open_rx_callback (session_t * s)
{
  upf_proxy_session_t *ps;

  clib_warning ("sidx %d", s->session_index);
  proxy_server_sessions_reader_lock ();

  ps = active_open_session_lookup (s);
  if (!ps)
    {
      clib_warning ("no proxy session for sidx %d", s->session_index);
      proxy_server_sessions_reader_unlock ();
      return 0;
    }

  if (ps->ao_disconnected || ps->po_disconnected)
    {
      clib_warning ("late tx callback: sidx %d psidx %d", s->session_index,
		 proxy_session_index (ps));
      proxy_server_sessions_reader_unlock ();
      return 0;
    }

  clib_warning ("sidx %d psidx %d", s->session_index, proxy_session_index (ps));
  proxy_server_sessions_reader_unlock ();

  rx_callback_inline (s->rx_fifo);
  return 0;
}

static int
active_open_tx_callback (session_t * s)
{
  clib_warning ("sidx %d", s->session_index);
  return tx_callback_inline (s, 1);
}

/* *INDENT-OFF* */
static session_cb_vft_t active_open_clients = {
  .session_reset_callback = active_open_reset_callback,
  .session_connected_callback = active_open_connected_callback,
  .session_accept_callback = active_open_create_callback,
  .session_disconnect_callback = active_open_disconnect_callback,
  .add_segment_callback = active_open_add_segment_callback,
  .builtin_app_rx_callback = active_open_rx_callback,
  .builtin_app_tx_callback = active_open_tx_callback,
  .session_cleanup_callback = active_open_session_cleanup_callback,
  .fifo_tuning_callback = common_fifo_tuning_callback
};
/* *INDENT-ON* */

static int
proxy_server_attach ()
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  u64 options[APP_OPTIONS_N_OPTIONS];
  vnet_app_attach_args_t _a, *a = &_a;
  app_worker_t *app_wrk;
  application_t *app;
  u8 *name;
  int r = 0;

  clib_memset (a, 0, sizeof (*a));
  clib_memset (options, 0, sizeof (options));

  a->name = name = format (0, "upf-proxy-server");
  a->api_client_index = pm->server_client_index;
  a->session_cb_vft = &proxy_session_cb_vft;
  a->options = options;
  a->options[APP_OPTIONS_SEGMENT_SIZE] = pm->private_segment_size;
  a->options[APP_OPTIONS_ADD_SEGMENT_SIZE] = pm->private_segment_size;
  a->options[APP_OPTIONS_RX_FIFO_SIZE] = pm->fifo_size;
  a->options[APP_OPTIONS_TX_FIFO_SIZE] = pm->fifo_size;
  a->options[APP_OPTIONS_MAX_FIFO_SIZE] = pm->max_fifo_size;
  a->options[APP_OPTIONS_HIGH_WATERMARK] = (u64) pm->high_watermark;
  a->options[APP_OPTIONS_LOW_WATERMARK] = (u64) pm->low_watermark;
  a->options[APP_OPTIONS_PRIVATE_SEGMENT_COUNT] = pm->private_segment_count;
  a->options[APP_OPTIONS_PREALLOC_FIFO_PAIRS] =
    pm->prealloc_fifos ? pm->prealloc_fifos : 0;

  a->options[APP_OPTIONS_FLAGS] = APP_OPTIONS_FLAGS_IS_BUILTIN;

  if (vnet_application_attach (a))
    {
      clib_warning ("failed to attach server");
      r = -1;
      goto out_free;
    }

  pm->server_app_index = a->app_index;

  /* Make sure we have a segment manager for connects */
  app = application_get (pm->server_app_index);
  app_wrk = application_get_worker (app, 0 /* default wrk only */ );
  app_worker_alloc_connects_segment_manager (app_wrk);

out_free:
  vec_free (name);
  return r;
}

static int
active_open_attach (void)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  vnet_app_attach_args_t _a, *a = &_a;
  u64 options[APP_OPTIONS_N_OPTIONS];
  u8 *name;
  int r = 0;

  clib_memset (a, 0, sizeof (*a));
  clib_memset (options, 0, sizeof (options));

  a->api_client_index = pm->active_open_client_index;
  a->session_cb_vft = &active_open_clients;
  a->name = name = format (0, "upf-proxy-active-open");

  options[APP_OPTIONS_ACCEPT_COOKIE] = 0x12345678;
  options[APP_OPTIONS_SEGMENT_SIZE] = 128 << 20;
  options[APP_OPTIONS_ADD_SEGMENT_SIZE] = pm->private_segment_size;
  options[APP_OPTIONS_RX_FIFO_SIZE] = pm->fifo_size;
  options[APP_OPTIONS_TX_FIFO_SIZE] = pm->fifo_size;
  options[APP_OPTIONS_MAX_FIFO_SIZE] = pm->max_fifo_size;
  options[APP_OPTIONS_HIGH_WATERMARK] = (u64) pm->high_watermark;
  options[APP_OPTIONS_LOW_WATERMARK] = (u64) pm->low_watermark;
  options[APP_OPTIONS_PRIVATE_SEGMENT_COUNT] = pm->private_segment_count;
  options[APP_OPTIONS_PREALLOC_FIFO_PAIRS] =
    pm->prealloc_fifos ? pm->prealloc_fifos : 0;

  options[APP_OPTIONS_FLAGS] = APP_OPTIONS_FLAGS_IS_BUILTIN
    | APP_OPTIONS_FLAGS_IS_PROXY;

  a->options = options;

  if (vnet_application_attach (a))
    {
      r = -1;
      goto out_free;
    }

  pm->active_open_app_index = a->app_index;

out_free:
  vec_free (name);
  return r;
}

static int
proxy_create (vlib_main_t * vm, u32 fib_index, int is_ip4)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  vlib_thread_main_t *vtm = vlib_get_thread_main ();
  u32 num_threads;
  int i, rv;

  if (PREDICT_FALSE (pm->server_client_index == (u32) ~ 0))
    {
      num_threads = 1 /* main thread */  + vtm->n_threads;
      vec_validate (pm->server_event_queue, num_threads - 1);
      vec_validate (pm->active_open_event_queue, num_threads - 1);
      vec_validate (pm->session_to_proxy_session, num_threads - 1);
      vec_validate (pm->session_to_active_open_session, num_threads - 1);

      clib_rwlock_init (&pm->sessions_lock);

      if ((rv = proxy_server_attach ()))
	{
	  assert (rv == 0);
	  clib_warning ("failed to attach server app");
	  return -1;
	}
      if ((rv = active_open_attach ()))
	{
	  assert (rv == 0);
	  clib_warning ("failed to attach active open app");
	  return -1;
	}

      for (i = 0; i < num_threads; i++)
	{
	  pm->active_open_event_queue[i] =
	    session_main_get_vpp_event_queue (i);

	  ASSERT (pm->active_open_event_queue[i]);

	  pm->server_event_queue[i] = session_main_get_vpp_event_queue (i);
	}
    }

  return 0;
}

static void
upf_proxy_create (vlib_main_t * vm, u32 fib_index, int is_ip4)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  int rv;

  if (pm->server_client_index == (u32) ~ 0)
    vnet_session_enable_disable (vm, 1 /* turn on TCP, etc. */ );

  rv = proxy_create (vm, fib_index, is_ip4);
  if (rv != 0)
    clib_error ("UPF http redirect server create returned %d", rv);
}

static int
upf_proxy_flow_expiration_hook (flow_entry_t * flow)
{
  upf_proxy_session_t *ps;
  if (flow->ps_index == ~0)
    return 0;

  proxy_server_sessions_writer_lock ();
  ps = proxy_session_get (flow->ps_index);
  ASSERT (ps);
  proxy_session_try_close_unlocked (ps);
  proxy_server_sessions_writer_unlock ();
  return -1;
}

clib_error_t *
upf_proxy_main_init (vlib_main_t * vm)
{
  upf_proxy_main_t *pm = &upf_proxy_main;
  tcp_main_t *tm = vnet_get_tcp_main ();

  pm->mss = 1350;
  pm->fifo_size = 64 << 10;
  pm->max_fifo_size = 128 << 20;
  pm->high_watermark = 80;
  pm->low_watermark = 50;
  pm->prealloc_fifos = 0;
  pm->private_segment_count = 0;
  pm->private_segment_size = 512 << 20;

  pm->server_client_index = ~0;
  pm->active_open_client_index = ~0;

  /*
   * FIXME: this disables a TIME_WAIT -> LISTEN transition in the TCP stack.
   * We have to do that because UPF proxy doesn't use listeners.
   */
  tm->dispatch_table[TCP_STATE_TIME_WAIT][TCP_FLAG_SYN].next =
    0 /* TCP_INPUT_NEXT_DROP */ ;
  tm->dispatch_table[TCP_STATE_TIME_WAIT][TCP_FLAG_SYN].error =
    TCP_ERROR_DISPATCH;

  clib_warning ("TCP4 Output Node Index %u, IP4 Proxy Output Node Index %u",
	     tcp4_output_node.index, upf_ip4_proxy_server_output_node.index);
  clib_warning ("TCP6 Output Node Index %u, IP6 Proxy Output Node Index %u",
	     tcp6_output_node.index, upf_ip6_proxy_server_output_node.index);
  pm->tcp4_server_output_next =
    vlib_node_add_next (vm, tcp4_output_node.index,
			upf_ip4_proxy_server_output_node.index);
  pm->tcp6_server_output_next =
    vlib_node_add_next (vm, tcp6_output_node.index,
			upf_ip6_proxy_server_output_node.index);

  flow_expiration_hook = upf_proxy_flow_expiration_hook;

  return 0;
}

static int upf_proxy_is_initialized = 0;

/*
 * upf_proxy_create() calls vnet_session_enable_disable() which must
 * not be invoked from an init function, as this leads to a crash in
 * the case of non-zero workers. See session_main_loop_init() in
 * src/vnet/session/session.c
 */
static clib_error_t *
upf_proxy_main_loop_init (vlib_main_t * vm)
{
  vlib_worker_thread_barrier_sync (vm);
  if (!upf_proxy_is_initialized)
    {
      upf_proxy_is_initialized = 1;
      upf_proxy_create (vm, 0, 1);
    }
  vlib_worker_thread_barrier_release (vm);

  return 0;
}

VLIB_INIT_FUNCTION (upf_proxy_main_init);
VLIB_MAIN_LOOP_ENTER_FUNCTION (upf_proxy_main_loop_init);

/*
* fd.io coding-style-patch-verification: ON
*
* Local Variables:
* eval: (c-set-style "gnu")
* End:
*/
