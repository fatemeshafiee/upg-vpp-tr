/*
 * Copyright(c) 2017 Travelping GmbH.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
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
#ifndef _UPF_PFCP_SERVER_H
#define _UPF_PFCP_SERVER_H

#include <time.h>
#include "upf.h"
#include "pfcp.h"
#include "flowtable.h"
#include <vppinfra/vec.h>
#include <vppinfra/format.h>
#include <vppinfra/tw_timer_1t_3w_1024sl_ov.h>

#define PFCP_HB_INTERVAL 10
#define PFCP_SERVER_HB_TIMER 0
#define PFCP_SERVER_T1       1
#define PFCP_SERVER_RESPONSE 2

extern vlib_node_registration_t pfcp_api_process_node;

typedef enum
{
  EVENT_RX = 1,
  EVENT_TX,
  EVENT_URR,
  EVENT_PACK,
} pfcp_process_event_t;

typedef struct
{
  /* Required for pool_get_aligned  */
  CLIB_CACHE_LINE_ALIGN_MARK (cacheline0);

  union
  {
    struct
    {
      struct
      {
	ip46_address_t address;
	u16 port;
      } rmt;

      u32 seq_no;
      session_handle_t session_handle;
    };
    u64 request_key[4];
  };

  struct
  {
    ip46_address_t address;
    u16 port;
  } lcl;

  u32 node;
  u32 session_index;

  f64 expires_at;		/* message timestamp */
  u32 timer;
  u32 n1;
  u32 t1;
  u8 is_valid_pool_item;

#if CLIB_DEBUG > 0
  char pool_put_loc[128];
#endif

  u8 *data;
} pfcp_msg_t;

typedef struct
{
  /* PFCP Node Id is either IPv4, IPv6 or FQDN */
  u8 *node_id;
} pfcp_node_t;

typedef struct
{
  u32 seq_no;
  time_t start_time;
  ip46_address_t address;
  f64 now;

    TWT (tw_timer_wheel) timer;
  pfcp_msg_t *msg_pool;
  u32 *msg_pool_cache;
  u32 *msg_pool_free;
  uword *request_q;
  mhash_t response_q;

  vlib_frame_t *ip_lookup_tx_frames[2];

  vlib_main_t *vlib_main;

  uword *free_msgs_by_node;
  u32 *expired;
} pfcp_server_main_t;

typedef struct
{
  uword session_idx;
  u64 cp_seid;
  ip46_address_t ue;
  u8 status;
} upf_event_urr_hdr_t;

typedef struct
{
  u32 urr_id;
  u32 trigger;
} upf_event_urr_data_t;

extern pfcp_server_main_t pfcp_server_main;

#define UDP_DST_PORT_PFCP 8805

void upf_pfcp_session_stop_up_inactivity_timer (urr_time_t * t);
void upf_pfcp_session_start_up_inactivity_timer (u32 si, f64 last,
						 urr_time_t * t);

void upf_pfcp_session_stop_urr_time (urr_time_t * t, f64 now);
void upf_pfcp_session_start_stop_urr_time (u32 si, urr_time_t * t,
					   u8 start_it);

u32 upf_pfcp_server_start_timer (u8 type, u32 id, u32 seconds);
void upf_pfcp_server_stop_msg_timer (pfcp_msg_t * msg);
void upf_pfcp_server_deferred_free_msgs_by_node (u32 node);

int upf_pfcp_send_request (upf_session_t * sx, pfcp_decoded_msg_t * dmsg);

int upf_pfcp_send_response (pfcp_msg_t * req, pfcp_decoded_msg_t * dmsg);

void upf_pfcp_session_up_deletion_report (upf_session_t * sx);

void upf_pfcp_server_fatemeh_packet_report(void * uev);
void upf_pfcp_server_session_usage_report (upf_event_urr_data_t * uev);
void upf_pfcp_fatemeh_traffic_report (upf_session_t * sx, uword sIdx, flowtable_main_t * fm, u8 * p0, vlib_buffer_t * b0);
clib_error_t *pfcp_server_main_init (vlib_main_t * vm);

static inline void
init_pfcp_msg (pfcp_msg_t * m)
{
  u8 is_valid_pool_item = m->is_valid_pool_item;

  memset (m, 0, sizeof (*m));
  m->is_valid_pool_item = is_valid_pool_item;
  m->node = ~0;
}

static inline void
pfcp_msg_pool_init (pfcp_server_main_t * psm)
{
  vec_alloc (psm->msg_pool_cache, 128);
  vec_alloc (psm->msg_pool_free, 128);

}

static inline void
pfcp_msg_pool_loop_start (pfcp_server_main_t * psm)
{
  /* move enough entries from free to cache,
     so that cache has max 128 entries */
  while (vec_len (psm->msg_pool_cache) < 128 &&
	 vec_len (psm->msg_pool_free) != 0)
    {
      vec_add1 (psm->msg_pool_cache, vec_pop (psm->msg_pool_free));
    }

  if (vec_len (psm->msg_pool_free) != 0)
    {
      for (int i = 0; i < vec_len (psm->msg_pool_free); i++)
	pool_put_index (psm->msg_pool, psm->msg_pool_free[i]);
      vec_reset_length (psm->msg_pool_free);
    }
}

static inline pfcp_msg_t *
pfcp_msg_pool_get (pfcp_server_main_t * psm)
{
  pfcp_msg_t *m;

  if (vec_len (psm->msg_pool_cache) != 0)
    {
      u32 index = vec_pop (psm->msg_pool_cache);

      m = pool_elt_at_index (psm->msg_pool, index);
      init_pfcp_msg (m);
    }
  else
    {
      pool_get_aligned_zero (psm->msg_pool, m, CLIB_CACHE_LINE_BYTES);
    }

  m->is_valid_pool_item = 1;
  return m;
}

static inline pfcp_msg_t *
pfcp_msg_pool_add (pfcp_server_main_t * psm, pfcp_msg_t * m)
{
  pfcp_msg_t *msg;

  msg = pfcp_msg_pool_get (psm);
  clib_memcpy_fast (msg, m, sizeof (*m));
  msg->is_valid_pool_item = 1;
#if CLIB_DEBUG > 0
  msg->pool_put_loc[0] = 0;
#endif
  return msg;
}

#if CLIB_DEBUG > 0
#define pfcp_msg_pool_put(psm, m) do {							\
    snprintf((m)->pool_put_loc, 128, "%s:%d %s", __FILE__, __LINE__, __FUNCTION__);	\
    _pfcp_msg_pool_put(psm, m);								\
  } while (0)
#else
#define pfcp_msg_pool_put(psm, m) _pfcp_msg_pool_put(psm, m)
#endif

static inline void
_pfcp_msg_pool_put (pfcp_server_main_t * psm, pfcp_msg_t * m)
{
  ASSERT (m->is_valid_pool_item);

  vec_free (m->data);
  m->is_valid_pool_item = 0;
  vec_add1 (psm->msg_pool_free, m - psm->msg_pool);
}

static inline int
pfcp_msg_pool_is_free_index (pfcp_server_main_t * psm, u32 index)
{
  if (!pool_is_free_index (psm->msg_pool, index))
    {
      pfcp_msg_t *m = pool_elt_at_index (psm->msg_pool, index);
      return !m->is_valid_pool_item;
    }
  return 0;
}

static inline pfcp_msg_t *
pfcp_msg_pool_elt_at_index (pfcp_server_main_t * psm, u32 index)
{
  pfcp_msg_t *m = pool_elt_at_index (psm->msg_pool, index);
#if CLIB_DEBUG > 0
  if (!m->is_valid_pool_item)
    {
      clib_warning ("ERROR: accessing a PFCP msg that was freed at: %s",
		    m->pool_put_loc);
      ASSERT (0);
    }
#endif
  return m;
}

static inline u32
pfcp_msg_get_index (pfcp_server_main_t * psm, pfcp_msg_t * m)
{
  return m - psm->msg_pool;
}

#endif /* _UPF_PFCP_SERVER_H */

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
