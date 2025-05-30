/*
 * Copyright (c) 2017 Intel and/or its affiliates.
 * Copyright (c) 2018-2020 Travelping GmbH
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

#include <vppinfra/error.h>
#include <vppinfra/hash.h>
#include <vnet/vnet.h>
#include <vnet/ip/ip.h>
#include <vnet/fib/ip4_fib.h>
#include <vnet/fib/ip6_fib.h>
#include <vnet/ethernet/ethernet.h>

#include <upf/upf.h>
#include <upf/upf_pfcp.h>

/* Statistics (not all errors) */
#define foreach_upf_encap_error    \
_(ENCAPSULATED, "good packets encapsulated")

static char *upf_encap_error_strings[] = {
#define _(sym,string) string,
  foreach_upf_encap_error
#undef _
};

typedef enum
{
#define _(sym,str) UPF_ENCAP_ERROR_##sym,
  foreach_upf_encap_error
#undef _
    UPF_ENCAP_N_ERROR,
} upf_encap_error_t;

#define foreach_upf_encap_next        \
_(DROP, "error-drop")                  \
_(IP4_LOOKUP, "ip4-lookup")             \
_(IP6_LOOKUP, "ip6-lookup")

typedef enum
{
  UPF_ENCAP_NEXT_DROP,
  UPF_ENCAP_NEXT_IP4_LOOKUP,
  UPF_ENCAP_NEXT_IP6_LOOKUP,
  UPF_ENCAP_N_NEXT,
} upf_encap_next_t;

#ifndef CLIB_MARCH_VARIANT
static u16
encode_ext_header_udp_port (vlib_buffer_t * b, u16 udp_port)
{
  u8 *p = vlib_buffer_get_current (b);
  gtpu_ext_header_t *hdr = (gtpu_ext_header_t *)p;

  hdr->type = GTPU_EXT_HEADER_UDP_PORT;
  hdr->len = 1;   /* in 4 byte units */
  *((u16 *)&hdr->pad) = udp_port; /* already in net order */
  hdr++;

  hdr->type = GTPU_EXT_HEADER_NEXT_HEADER_NO_MORE;
  b->current_length = sizeof (*hdr) + sizeof (hdr->type);

  return b->current_length;
}

static u16
encode_error_indication (vlib_buffer_t * b, gtp_error_ind_t * error, int is_ip4)
{
  u8 *p = vlib_buffer_get_current (b);
  u8 *start = vlib_buffer_get_current (b);

  gtpu_tv_ie_t *tv_ie;
  gtpu_tlv_ie_t *tlv_ie;

  tv_ie = (gtpu_tv_ie_t *)p;
  tv_ie->id = GTPU_IE_TEID_I;
  *((u32 *)&tv_ie->data) = error->teid;
  p += sizeof(*tv_ie) + sizeof (u32);

  tv_ie = (gtpu_tv_ie_t *)p;
  tv_ie->id = GTPU_IE_RECOVERY;
  *((u8 *)&tv_ie->data) = 0;
  p += sizeof(*tv_ie) + sizeof (u8);

  tlv_ie = (gtpu_tlv_ie_t *)p;
  tlv_ie->id = GTPU_IE_GSN_ADDRESS;
  if (is_ip4)
    {
      tlv_ie->len = clib_host_to_net_u16(sizeof (ip4_address_t));
      clib_memcpy_fast (&tlv_ie->data, &error->addr.ip4, sizeof (ip4_address_t));
    }
  else
    {
      tlv_ie->len = clib_host_to_net_u16(sizeof (ip6_address_t));
      clib_memcpy_fast (&tlv_ie->data, &error->addr.ip6, sizeof (ip6_address_t));
    }

  p += sizeof(*tlv_ie) + clib_net_to_host_u16 (tlv_ie->len);
  b->current_length = p - start;

  return b->current_length; //bytes encoded
}

void
upf_gtpu_error_ind (vlib_buffer_t * b0, int is_ip4)
{
  ip4_main_t *i4m = &ip4_main;
  ip6_main_t *i6m = &ip6_main;
  upf_main_t *gtm = &upf_main;
  vlib_main_t *vm = gtm->vlib_main;
  u32 bi = 0;
  gtp_error_ind_t error;
  vlib_buffer_t *p0;
  u16 len_p1, new_len1, new_ip_len1;
  ip_csum_t sum1;
  u32 fib_index1;
  u16 gtp_len_encoded;
  u16 ext_header_len;
  u8 host_config_ttl = i4m->host_config.ttl;

  if (vlib_buffer_alloc (vm, &bi, 1) != 1)
    {
      clib_warning ("No buffers available");
      return;
    }

  p0 = vlib_get_buffer (vm, bi);
  VLIB_BUFFER_TRACE_TRAJECTORY_INIT (p0);

  /*
   * b0 - received buffer, p0 - allocated buffer
   * xxx0 header pointers used for b0
   * xxx1 header pointers used for p1
   */

  if (is_ip4)
    {
      ip4_header_t *ip40, *ip41;
      udp_header_t *udp0, *udp1;
      gtpu_header_t *gtpu0, *gtpu1;

      /* For ip46-lookup, set up FIB */
      fib_index1 = vec_elt (i4m->fib_index_by_sw_if_index,
			    vnet_buffer (b0)->sw_if_index[VLIB_RX]);
      vnet_buffer (p0)->sw_if_index[VLIB_TX] = fib_index1;

      /* Set buffer borders to include headers for now */
      p0->current_length += sizeof (ip4_header_t) + sizeof (udp_header_t) + sizeof (gtpu_header_t);

      vlib_buffer_reset (b0);
      vlib_buffer_advance (b0, vnet_buffer (b0)->l4_hdr_offset);
      gtpu0 = vlib_buffer_get_current (b0);

      vlib_buffer_advance (b0, -(uword) (sizeof (ip4_header_t) + sizeof (udp_header_t)));
      ip40 = vlib_buffer_get_current (b0);

      udp0 = ip4_next_header (ip40);

      ip41 = vlib_buffer_get_current (p0);
      vlib_buffer_advance (p0, sizeof (ip4_header_t));
      udp1 = vlib_buffer_get_current (p0);
      vlib_buffer_advance (p0, sizeof (udp_header_t));
      gtpu1 = vlib_buffer_get_current (p0);
      // TODO: In a packet trace it looks fine, we can see p0 traced for TX
      // But shouldn't we allocate new trace for it?
      p0->trace_handle = b0->trace_handle;

      /* Reuse IP settings of original packet */
      memcpy (ip41, ip40, sizeof (ip4_header_t));

      /* Swap addresses, save src addr to be encoded */
      ip41->dst_address = ip40->src_address;
      ip41->src_address = ip40->dst_address;
      error.addr.ip4 = ip40->src_address;

      /* Swap udp ports */
      udp1->src_port = udp0->dst_port;
      udp1->dst_port = clib_host_to_net_u16 (GTPU_UDP_PORT);

      /* Fill GTPU info */
      gtpu1->ver_flags = GTPU_V1_VER | GTPU_PT_GTP | GTPU_S_BIT | GTPU_E_BIT;
      gtpu1->type = GTPU_TYPE_ERROR_IND;
      gtpu1->sequence = 0;
      gtpu1->teid = 0;
      error.teid = gtpu0->teid; // Net order

      /* Set pointer behind GTPU header to encode ext header UDP port */
      gtp_len_encoded = sizeof (gtpu_header_t) - sizeof (gtpu1->next_ext_type);
      vlib_buffer_advance (p0, gtp_len_encoded);
      ext_header_len = encode_ext_header_udp_port (p0, udp0->src_port);

      /* Set pointer behind GTPU ext header to encode IEs */
      vlib_buffer_advance (p0, ext_header_len);
      len_p1 = encode_error_indication (p0, &error, is_ip4);
      /* GTPU mandatory fields are 8 bytes and are not included into payload */
      gtpu1->length = clib_host_to_net_u16 (len_p1 + (ext_header_len - 1) + (sizeof (gtpu_header_t) - 8));

      /* Calculate new IP length. */
      new_len1 = ip4_header_bytes (ip41) + sizeof (udp_header_t) +
		 gtp_len_encoded + ext_header_len + len_p1;

      /* Update IP header fields and checksum. */
      sum1 = ip41->checksum;

      sum1 = ip_csum_update (sum1, ip41->ttl, host_config_ttl,
			     ip4_header_t, ttl);
      ip41->ttl = host_config_ttl;

      new_ip_len1 = clib_host_to_net_u16 (new_len1);
      sum1 = ip_csum_update (sum1, ip41->length, new_ip_len1,
			     ip4_header_t, length);
      ip41->length = new_ip_len1;
      ip41->checksum = ip_csum_fold (sum1);
      ip41->fragment_id = clib_net_to_host_u16 (1);

      /* UDP length. */
      udp1->length = clib_host_to_net_u16 (sizeof (udp_header_t) +
					   gtp_len_encoded +
					   len_p1 + ext_header_len);
      /* UDP checksum. */
      udp1->checksum = 0;
      udp1->checksum = ip4_tcp_udp_compute_checksum (vm, p0, ip41);
      if (udp1->checksum == 0)
        udp1->checksum = 0xffff;

      p0->flags |= VNET_BUFFER_F_LOCALLY_ORIGINATED;
      p0->flags |= VLIB_BUFFER_IS_TRACED;

      vlib_buffer_advance (p0, -(uword) (sizeof (ip4_header_t) + sizeof (udp_header_t) + gtp_len_encoded + ext_header_len));

      /* Enqueue to IP Lookup */
      upf_ip_lookup_tx (bi, is_ip4);
    }
  else
    {
      ip6_header_t *ip60, *ip61;
      udp_header_t *udp0, *udp1;
      gtpu_header_t *gtpu0, *gtpu1;
      int bogus = 0;

      /* For ip46-lookup, set up FIB */
      fib_index1 = vec_elt (i6m->fib_index_by_sw_if_index,
                                vnet_buffer (b0)->sw_if_index[VLIB_RX]);
      vnet_buffer (p0)->sw_if_index[VLIB_TX] = fib_index1;

      /* Set buffer borders to include headers for now */
      p0->current_length += sizeof (ip6_header_t) + sizeof (udp_header_t) + sizeof (gtpu_header_t);

      vlib_buffer_reset (b0);
      vlib_buffer_advance (b0, vnet_buffer (b0)->l4_hdr_offset);
      gtpu0 = vlib_buffer_get_current (b0);

      vlib_buffer_advance (b0, -(uword) (sizeof (ip6_header_t) + sizeof (udp_header_t)));
      ip60 = vlib_buffer_get_current (b0);

      udp0 = ip6_next_header (ip60);

      ip61 = vlib_buffer_get_current (p0);
      vlib_buffer_advance (p0, sizeof (ip6_header_t));
      udp1 = vlib_buffer_get_current (p0);
      vlib_buffer_advance (p0, sizeof (udp_header_t));
      gtpu1 = vlib_buffer_get_current (p0);
      p0->trace_handle = b0->trace_handle;

      /* Reuse IP settings of original packet */
      memcpy (ip61, ip60, sizeof (ip6_header_t));

      /* Swap addresses, save src addr to be encoded */
      ip61->dst_address = ip60->src_address;
      ip61->src_address = ip60->dst_address;
      error.addr.ip6 = ip60->src_address;

      /* Swap udp ports*/
      udp1->src_port = udp0->dst_port;
      udp1->dst_port = clib_host_to_net_u16 (GTPU_UDP_PORT);

      /* Fill GTPU info */
      gtpu1->ver_flags = GTPU_V1_VER | GTPU_PT_GTP | GTPU_S_BIT | GTPU_E_BIT;
      gtpu1->type = GTPU_TYPE_ERROR_IND;
      gtpu1->sequence = 0;
      error.teid = gtpu0->teid; // Net order

      /* Set pointer behind GTPU header to encode ext header UDP port */
      gtp_len_encoded = sizeof (gtpu_header_t) - sizeof (gtpu1->next_ext_type);
      vlib_buffer_advance (p0, gtp_len_encoded);
      ext_header_len = encode_ext_header_udp_port (p0, udp0->src_port);

      /* Set pointer behind GTPU ext header to encode IEs */
      vlib_buffer_advance (p0, ext_header_len);
      len_p1 = encode_error_indication (p0, &error, is_ip4);
      gtpu1->length = clib_host_to_net_u16 (len_p1 + (ext_header_len - 1) + (sizeof (gtpu_header_t) - 8));

      // Update checksums and length params for IP and UDP
      /* Calculate new IP length. */
      new_len1 = sizeof (udp_header_t) + gtp_len_encoded + len_p1 + ext_header_len;
      ip61->payload_length = clib_host_to_net_u16 (new_len1);
      /* UDP length. */
      udp1->length = ip61->payload_length;
      /* UDP checksum. */
      udp1->checksum = 0;
      udp1->checksum = ip6_tcp_udp_icmp_compute_checksum (vm, p0, ip61, &bogus);
      if (udp1->checksum == 0)
        udp1->checksum = 0xffff;

      p0->flags |= VNET_BUFFER_F_LOCALLY_ORIGINATED;
      p0->flags |= VLIB_BUFFER_IS_TRACED;

      vlib_buffer_advance (p0, -(uword) (sizeof (ip6_header_t) + sizeof (udp_header_t) + gtp_len_encoded + ext_header_len));

      /* Enqueue to IP Lookup */
      upf_ip_lookup_tx (bi, is_ip4);
    }
}

u32
upf_gtpu_end_marker (u32 fib_index, u32 dpoi_index, u8 * rewrite, int is_ip4)
{
  upf_main_t *gtm = &upf_main;
  vlib_main_t *vm = gtm->vlib_main;
  u32 bi = 0;
  vlib_buffer_t *p0;
  ip4_gtpu_header_t *gtpu4;
  ip6_gtpu_header_t *gtpu6;
  u16 new_l0;
  ip_csum_t sum0;

  if (vlib_buffer_alloc (vm, &bi, 1) != 1)
    return ~0;

  p0 = vlib_get_buffer (vm, bi);
  VLIB_BUFFER_TRACE_TRAJECTORY_INIT (p0);

  vnet_buffer (p0)->sw_if_index[VLIB_TX] = fib_index;
  vnet_buffer (p0)->ip.adj_index[VLIB_TX] = dpoi_index;

  if (is_ip4)
    {
      gtpu4 = vlib_buffer_get_current (p0);
      p0->current_length = sizeof (*gtpu4);
      memcpy (gtpu4, rewrite, vec_len (rewrite));

      /* Fix the IP4 checksum and length */
      sum0 = gtpu4->ip4.checksum;
      new_l0 = clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, p0));
      sum0 =
	ip_csum_update (sum0, 0, new_l0, ip4_header_t,
			length /* changed member */ );
      gtpu4->ip4.checksum = ip_csum_fold (sum0);
      gtpu4->ip4.length = new_l0;

      /* Fix UDP length and set source port */
      gtpu4->udp.length =
	clib_host_to_net_u16 (sizeof (udp_header_t) + sizeof (gtpu_header_t));
      gtpu4->udp.src_port = vnet_l2_compute_flow_hash (p0);

      /* Fix GTPU length */
      gtpu4->gtpu.type = GTPU_TYPE_END_MARKER;
      gtpu4->gtpu.length = 0;
    }
  else				/* ip6 path */
    {
      int bogus = 0;

      gtpu6 = vlib_buffer_get_current (p0);
      p0->current_length = sizeof (*gtpu6);
      memcpy (gtpu6, rewrite, vec_len (rewrite));

      /* Fix IP6 payload length */
      new_l0 =
	clib_host_to_net_u16 (sizeof (udp_header_t) + sizeof (gtpu_header_t));
      gtpu6->ip6.payload_length = new_l0;

      /* Fix UDP length  and set source port */
      gtpu6->udp.length = new_l0;
      gtpu6->udp.src_port = vnet_l2_compute_flow_hash (p0);

      /* IPv6 UDP checksum is mandatory */
      gtpu6->udp.checksum =
	ip6_tcp_udp_icmp_compute_checksum (vm, p0, &gtpu6->ip6, &bogus);
      if (gtpu6->udp.checksum == 0)
	gtpu6->udp.checksum = 0xffff;

      /* Fix GTPU length */
      gtpu6->gtpu.type = GTPU_TYPE_END_MARKER;
      gtpu6->gtpu.length = 0;
    }

  return bi;
}
#endif

#define foreach_fixed_header4_offset            \
    _(0) _(1) _(2) _(3)

#define foreach_fixed_header6_offset            \
    _(0) _(1) _(2) _(3) _(4) _(5) _(6)

always_inline uword
upf_encap_inline (vlib_main_t * vm,
		  vlib_node_runtime_t * node,
		  vlib_frame_t * from_frame, u32 is_ip4)
{
  u32 n_left_from, next_index, *from, *to_next;
  upf_main_t *gtm = &upf_main;
  u32 pkts_encapsulated = 0;
  u16 old_l0 = 0, old_l1 = 0, old_l2 = 0, old_l3 = 0;
  u32 next0 = 0, next1 = 0, next2 = 0, next3 = 0;
  upf_session_t *s0 = NULL, *s1 = NULL, *s2 = NULL, *s3 = NULL;
  struct rules *r0 = NULL, *r1 = NULL, *r2 = NULL, *r3 = NULL;
  upf_pdr_t *pdr0 = NULL, *pdr1 = NULL, *pdr2 = NULL, *pdr3 = NULL;
  upf_far_t *far0 = NULL, *far1 = NULL, *far2 = NULL, *far3 = NULL;
  upf_peer_t *peer0 = NULL, *peer1 = NULL, *peer2 = NULL, *peer3 = NULL;

  u32 const csum_mask =
	  ~(VNET_BUFFER_F_OFFLOAD_TCP_CKSUM | VNET_BUFFER_F_OFFLOAD_UDP_CKSUM |
	    (is_ip4 ? VNET_BUFFER_F_OFFLOAD_IP_CKSUM : 0));

  from = vlib_frame_vector_args (from_frame);
  n_left_from = from_frame->n_vectors;

  next_index = node->cached_next_index;

  while (n_left_from > 0)
    {
      u32 n_left_to_next;

      vlib_get_next_frame (vm, node, next_index, to_next, n_left_to_next);

      while (n_left_from >= 8 && n_left_to_next >= 4)
	{
	  u32 bi0, bi1, bi2, bi3;
	  vlib_buffer_t *b0, *b1, *b2, *b3;
	  u32 flow_hash0, flow_hash1, flow_hash2, flow_hash3;
	  ip4_header_t *ip4_0, *ip4_1, *ip4_2, *ip4_3;
	  ip6_header_t *ip6_0, *ip6_1, *ip6_2, *ip6_3;
	  udp_header_t *udp0, *udp1, *udp2, *udp3;
	  gtpu_header_t *gtpu0, *gtpu1, *gtpu2, *gtpu3;
	  u64 *copy_src0, *copy_dst0;
	  u64 *copy_src1, *copy_dst1;
	  u64 *copy_src2, *copy_dst2;
	  u64 *copy_src3, *copy_dst3;
	  u32 *copy_src_last0, *copy_dst_last0;
	  u32 *copy_src_last1, *copy_dst_last1;
	  u32 *copy_src_last2, *copy_dst_last2;
	  u32 *copy_src_last3, *copy_dst_last3;
	  u16 new_l0, new_l1, new_l2, new_l3;
	  ip_csum_t sum0, sum1, sum2, sum3;

	  /* Prefetch next iteration. */
	  {
	    vlib_buffer_t *p4, *p5, *p6, *p7;

	    p4 = vlib_get_buffer (vm, from[4]);
	    p5 = vlib_get_buffer (vm, from[5]);
	    p6 = vlib_get_buffer (vm, from[6]);
	    p7 = vlib_get_buffer (vm, from[7]);

	    vlib_prefetch_buffer_header (p4, LOAD);
	    vlib_prefetch_buffer_header (p5, LOAD);
	    vlib_prefetch_buffer_header (p6, LOAD);
	    vlib_prefetch_buffer_header (p7, LOAD);

	    CLIB_PREFETCH (p4->data, 2 * CLIB_CACHE_LINE_BYTES, LOAD);
	    CLIB_PREFETCH (p5->data, 2 * CLIB_CACHE_LINE_BYTES, LOAD);
	    CLIB_PREFETCH (p6->data, 2 * CLIB_CACHE_LINE_BYTES, LOAD);
	    CLIB_PREFETCH (p7->data, 2 * CLIB_CACHE_LINE_BYTES, LOAD);
	  }

	  bi0 = from[0];
	  bi1 = from[1];
	  bi2 = from[2];
	  bi3 = from[3];
	  to_next[0] = bi0;
	  to_next[1] = bi1;
	  to_next[2] = bi2;
	  to_next[3] = bi3;
	  from += 4;
	  to_next += 4;
	  n_left_to_next -= 4;
	  n_left_from -= 4;

	  b0 = vlib_get_buffer (vm, bi0);
	  b1 = vlib_get_buffer (vm, bi1);
	  b2 = vlib_get_buffer (vm, bi2);
	  b3 = vlib_get_buffer (vm, bi3);

          UPF_CHECK_INNER_NODE (b0);
          UPF_CHECK_INNER_NODE (b1);
          UPF_CHECK_INNER_NODE (b2);
          UPF_CHECK_INNER_NODE (b3);

	  flow_hash0 = vnet_l2_compute_flow_hash (b0);
	  flow_hash1 = vnet_l2_compute_flow_hash (b1);
	  flow_hash2 = vnet_l2_compute_flow_hash (b2);
	  flow_hash3 = vnet_l2_compute_flow_hash (b3);

	  /* Get next node index and adj index from tunnel next_dpo */
	  s0 = &gtm->sessions[upf_buffer_opaque (b0)->gtpu.session_index];
	  s1 = &gtm->sessions[upf_buffer_opaque (b1)->gtpu.session_index];
	  s2 = &gtm->sessions[upf_buffer_opaque (b2)->gtpu.session_index];
	  s3 = &gtm->sessions[upf_buffer_opaque (b3)->gtpu.session_index];

	  r0 = pfcp_get_rules (s0, PFCP_ACTIVE);
	  r1 = pfcp_get_rules (s1, PFCP_ACTIVE);
	  r2 = pfcp_get_rules (s2, PFCP_ACTIVE);
	  r3 = pfcp_get_rules (s3, PFCP_ACTIVE);

	  /* TODO: this should be optimized */
	  pdr0 = vec_elt_at_index (r0->pdr, upf_buffer_opaque (b0)->gtpu.pdr_idx);
	  pdr1 = vec_elt_at_index (r1->pdr, upf_buffer_opaque (b1)->gtpu.pdr_idx);
	  pdr2 = vec_elt_at_index (r2->pdr, upf_buffer_opaque (b2)->gtpu.pdr_idx);
	  pdr3 = vec_elt_at_index (r3->pdr, upf_buffer_opaque (b3)->gtpu.pdr_idx);

	  /* TODO: this should be optimized */
	  far0 = pfcp_get_far_by_id (r0, pdr0->far_id);
	  far1 = pfcp_get_far_by_id (r1, pdr1->far_id);
	  far2 = pfcp_get_far_by_id (r2, pdr2->far_id);
	  far3 = pfcp_get_far_by_id (r3, pdr3->far_id);

	  peer0 = pool_elt_at_index (gtm->peers, far0->forward.peer_idx);
	  peer1 = pool_elt_at_index (gtm->peers, far1->forward.peer_idx);
	  peer2 = pool_elt_at_index (gtm->peers, far2->forward.peer_idx);
	  peer3 = pool_elt_at_index (gtm->peers, far3->forward.peer_idx);

	  /* Note: change to always set next0 if it may be set to drop */
	  next0 = peer0->next_dpo.dpoi_next_node;
	  vnet_buffer (b0)->ip.adj_index[VLIB_TX] =
	    peer0->next_dpo.dpoi_index;
	  next1 = peer1->next_dpo.dpoi_next_node;
	  vnet_buffer (b1)->ip.adj_index[VLIB_TX] =
	    peer1->next_dpo.dpoi_index;
	  next2 = peer2->next_dpo.dpoi_next_node;
	  vnet_buffer (b2)->ip.adj_index[VLIB_TX] =
	    peer2->next_dpo.dpoi_index;
	  next3 = peer3->next_dpo.dpoi_next_node;
	  vnet_buffer (b3)->ip.adj_index[VLIB_TX] =
	    peer3->next_dpo.dpoi_index;

	  if (PREDICT_FALSE ((upf_buffer_opaque (b0)->gtpu.hdr_flags & GTPU_E_S_PN_BIT) != 0))
	    vlib_buffer_advance (b0, -upf_buffer_opaque (b0)->gtpu.ext_hdr_len);
	  if (PREDICT_FALSE ((upf_buffer_opaque (b1)->gtpu.hdr_flags & GTPU_E_S_PN_BIT) != 0))
	    vlib_buffer_advance (b1, -upf_buffer_opaque (b1)->gtpu.ext_hdr_len);
	  if (PREDICT_FALSE ((upf_buffer_opaque (b2)->gtpu.hdr_flags & GTPU_E_S_PN_BIT) != 0))
	    vlib_buffer_advance (b2, -upf_buffer_opaque (b2)->gtpu.ext_hdr_len);
	  if (PREDICT_FALSE ((upf_buffer_opaque (b3)->gtpu.hdr_flags & GTPU_E_S_PN_BIT) != 0))
	    vlib_buffer_advance (b3, -upf_buffer_opaque (b3)->gtpu.ext_hdr_len);

	  /* Apply the rewrite string. $$$$ vnet_rewrite? */
	  vlib_buffer_advance (b0, -(word) _vec_len (far0->forward.rewrite));
	  vlib_buffer_advance (b1, -(word) _vec_len (far1->forward.rewrite));
	  vlib_buffer_advance (b2, -(word) _vec_len (far2->forward.rewrite));
	  vlib_buffer_advance (b3, -(word) _vec_len (far3->forward.rewrite));

	  if (is_ip4)
	    {
	      ip4_0 = vlib_buffer_get_current (b0);
	      ip4_1 = vlib_buffer_get_current (b1);
	      ip4_2 = vlib_buffer_get_current (b2);
	      ip4_3 = vlib_buffer_get_current (b3);

	      /* Copy the fixed header */
	      copy_dst0 = (u64 *) ip4_0;
	      copy_src0 = (u64 *) far0->forward.rewrite;
	      copy_dst1 = (u64 *) ip4_1;
	      copy_src1 = (u64 *) far1->forward.rewrite;
	      copy_dst2 = (u64 *) ip4_2;
	      copy_src2 = (u64 *) far2->forward.rewrite;
	      copy_dst3 = (u64 *) ip4_3;
	      copy_src3 = (u64 *) far3->forward.rewrite;

	      /* Copy first 32 octets 8-bytes at a time */
#define _(offs) copy_dst0[offs] = copy_src0[offs];
	      foreach_fixed_header4_offset;
#undef _
#define _(offs) copy_dst1[offs] = copy_src1[offs];
	      foreach_fixed_header4_offset;
#undef _
#define _(offs) copy_dst2[offs] = copy_src2[offs];
	      foreach_fixed_header4_offset;
#undef _
#define _(offs) copy_dst3[offs] = copy_src3[offs];
	      foreach_fixed_header4_offset;
#undef _
	      /* Last 4 octets. Hopefully gcc will be our friend */
	      copy_dst_last0 = (u32 *) (&copy_dst0[4]);
	      copy_src_last0 = (u32 *) (&copy_src0[4]);
	      copy_dst_last0[0] = copy_src_last0[0];
	      copy_dst_last1 = (u32 *) (&copy_dst1[4]);
	      copy_src_last1 = (u32 *) (&copy_src1[4]);
	      copy_dst_last1[0] = copy_src_last1[0];
	      copy_dst_last2 = (u32 *) (&copy_dst2[4]);
	      copy_src_last2 = (u32 *) (&copy_src2[4]);
	      copy_dst_last2[0] = copy_src_last2[0];
	      copy_dst_last3 = (u32 *) (&copy_dst3[4]);
	      copy_src_last3 = (u32 *) (&copy_src3[4]);
	      copy_dst_last3[0] = copy_src_last3[0];

	      /* Fix the IP4 checksum and length */
	      sum0 = ip4_0->checksum;
	      new_l0 =		/* old_l0 always 0, see the rewrite setup */
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b0));
	      sum0 = ip_csum_update (sum0, old_l0, new_l0, ip4_header_t,
				     length /* changed member */ );
	      ip4_0->checksum = ip_csum_fold (sum0);
	      ip4_0->length = new_l0;
	      sum1 = ip4_1->checksum;
	      new_l1 =		/* old_l1 always 0, see the rewrite setup */
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b1));
	      sum1 = ip_csum_update (sum1, old_l1, new_l1, ip4_header_t,
				     length /* changed member */ );
	      ip4_1->checksum = ip_csum_fold (sum1);
	      ip4_1->length = new_l1;
	      sum2 = ip4_2->checksum;
	      new_l2 =		/* old_l0 always 0, see the rewrite setup */
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b2));
	      sum2 = ip_csum_update (sum2, old_l2, new_l2, ip4_header_t,
				     length /* changed member */ );
	      ip4_2->checksum = ip_csum_fold (sum2);
	      ip4_2->length = new_l2;
	      sum3 = ip4_3->checksum;
	      new_l3 =		/* old_l1 always 0, see the rewrite setup */
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b3));
	      sum3 = ip_csum_update (sum3, old_l3, new_l3, ip4_header_t,
				     length /* changed member */ );
	      ip4_3->checksum = ip_csum_fold (sum3);
	      ip4_3->length = new_l3;

	      /* Fix UDP length and set source port */
	      udp0 = (udp_header_t *) (ip4_0 + 1);
	      new_l0 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b0) -
				      sizeof (*ip4_0));
	      udp0->length = new_l0;
	      udp0->src_port = flow_hash0;
	      udp1 = (udp_header_t *) (ip4_1 + 1);
	      new_l1 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b1) -
				      sizeof (*ip4_1));
	      udp1->length = new_l1;
	      udp1->src_port = flow_hash1;
	      udp2 = (udp_header_t *) (ip4_2 + 1);
	      new_l2 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b2) -
				      sizeof (*ip4_2));
	      udp2->length = new_l2;
	      udp2->src_port = flow_hash2;
	      udp3 = (udp_header_t *) (ip4_3 + 1);
	      new_l3 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b3) -
				      sizeof (*ip4_3));
	      udp3->length = new_l3;
	      udp3->src_port = flow_hash3;

	      /* Fix GTPU length */
	      gtpu0 = (gtpu_header_t *) (udp0 + 1);
	      new_l0 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b0) -
				      sizeof (*ip4_0) - sizeof (*udp0) -
				      GTPU_V1_HDR_LEN);
	      gtpu0->length = new_l0;
	      gtpu0->ver_flags |= (upf_buffer_opaque (b0)->gtpu.hdr_flags & GTPU_E_S_PN_BIT);
	      gtpu1 = (gtpu_header_t *) (udp1 + 1);
	      new_l1 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b1) -
				      sizeof (*ip4_1) - sizeof (*udp1) -
				      GTPU_V1_HDR_LEN);
	      gtpu1->length = new_l1;
	      gtpu1->ver_flags |= (upf_buffer_opaque (b1)->gtpu.hdr_flags & GTPU_E_S_PN_BIT);
	      gtpu2 = (gtpu_header_t *) (udp2 + 1);
	      new_l2 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b2) -
				      sizeof (*ip4_2) - sizeof (*udp2) -
				      GTPU_V1_HDR_LEN);
	      gtpu2->length = new_l2;
	      gtpu2->ver_flags |= (upf_buffer_opaque (b2)->gtpu.hdr_flags & GTPU_E_S_PN_BIT);
	      gtpu3 = (gtpu_header_t *) (udp3 + 1);
	      new_l3 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b3) -
				      sizeof (*ip4_3) - sizeof (*udp3) -
				      GTPU_V1_HDR_LEN);
	      gtpu3->length = new_l3;
	      gtpu3->ver_flags |= (upf_buffer_opaque (b3)->gtpu.hdr_flags & GTPU_E_S_PN_BIT);
	    }
	  else			/* ipv6 */
	    {
	      int bogus = 0;

	      ip6_0 = vlib_buffer_get_current (b0);
	      ip6_1 = vlib_buffer_get_current (b1);
	      ip6_2 = vlib_buffer_get_current (b2);
	      ip6_3 = vlib_buffer_get_current (b3);

	      /* Copy the fixed header */
	      copy_dst0 = (u64 *) ip6_0;
	      copy_src0 = (u64 *) far0->forward.rewrite;
	      copy_dst1 = (u64 *) ip6_1;
	      copy_src1 = (u64 *) far1->forward.rewrite;
	      copy_dst2 = (u64 *) ip6_2;
	      copy_src2 = (u64 *) far2->forward.rewrite;
	      copy_dst3 = (u64 *) ip6_3;
	      copy_src3 = (u64 *) far3->forward.rewrite;
	      /* Copy first 56 (ip6) octets 8-bytes at a time */
#define _(offs) copy_dst0[offs] = copy_src0[offs];
	      foreach_fixed_header6_offset;
#undef _
#define _(offs) copy_dst1[offs] = copy_src1[offs];
	      foreach_fixed_header6_offset;
#undef _
#define _(offs) copy_dst2[offs] = copy_src2[offs];
	      foreach_fixed_header6_offset;
#undef _
#define _(offs) copy_dst3[offs] = copy_src3[offs];
	      foreach_fixed_header6_offset;
#undef _
	      /* Fix IP6 payload length */
	      new_l0 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b0)
				      - sizeof (*ip6_0));
	      ip6_0->payload_length = new_l0;
	      new_l1 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b1)
				      - sizeof (*ip6_1));
	      ip6_1->payload_length = new_l1;
	      new_l2 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b2)
				      - sizeof (*ip6_2));
	      ip6_2->payload_length = new_l2;
	      new_l3 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b3)
				      - sizeof (*ip6_3));
	      ip6_3->payload_length = new_l3;

	      /* Fix UDP length  and set source port */
	      udp0 = (udp_header_t *) (ip6_0 + 1);
	      udp0->length = new_l0;
	      udp0->src_port = flow_hash0;
	      udp1 = (udp_header_t *) (ip6_1 + 1);
	      udp1->length = new_l1;
	      udp1->src_port = flow_hash1;
	      udp2 = (udp_header_t *) (ip6_2 + 1);
	      udp2->length = new_l2;
	      udp2->src_port = flow_hash2;
	      udp3 = (udp_header_t *) (ip6_3 + 1);
	      udp3->length = new_l3;
	      udp3->src_port = flow_hash3;

	      /* Fix GTPU length */
	      gtpu0 = (gtpu_header_t *) (udp0 + 1);
	      new_l0 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b0) -
				      sizeof (*ip6_0) - sizeof (*udp0) -
				      GTPU_V1_HDR_LEN);
	      gtpu0->length = new_l0;
	      gtpu0->ver_flags |= (upf_buffer_opaque (b0)->gtpu.hdr_flags & GTPU_E_S_PN_BIT);
	      gtpu1 = (gtpu_header_t *) (udp1 + 1);
	      new_l1 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b1) -
				      sizeof (*ip6_1) - sizeof (*udp1) -
				      GTPU_V1_HDR_LEN);
	      gtpu1->length = new_l1;
	      gtpu1->ver_flags |= (upf_buffer_opaque (b1)->gtpu.hdr_flags & GTPU_E_S_PN_BIT);
	      gtpu2 = (gtpu_header_t *) (udp2 + 1);
	      new_l2 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b2) -
				      sizeof (*ip6_2) - sizeof (*udp2) -
				      GTPU_V1_HDR_LEN);
	      gtpu2->length = new_l2;
	      gtpu2->ver_flags |= (upf_buffer_opaque (b2)->gtpu.hdr_flags & GTPU_E_S_PN_BIT);
	      gtpu3 = (gtpu_header_t *) (udp3 + 1);
	      new_l3 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b3) -
				      sizeof (*ip6_3) - sizeof (*udp3) -
				      GTPU_V1_HDR_LEN);
	      gtpu3->length = new_l3;
	      gtpu3->ver_flags |= (upf_buffer_opaque (b3)->gtpu.hdr_flags & GTPU_E_S_PN_BIT);

	      /* IPv6 UDP checksum is mandatory */
	      udp0->checksum = ip6_tcp_udp_icmp_compute_checksum (vm, b0,
								  ip6_0,
								  &bogus);
	      if (udp0->checksum == 0)
		udp0->checksum = 0xffff;
	      udp1->checksum = ip6_tcp_udp_icmp_compute_checksum (vm, b1,
								  ip6_1,
								  &bogus);
	      if (udp1->checksum == 0)
		udp1->checksum = 0xffff;
	      udp2->checksum = ip6_tcp_udp_icmp_compute_checksum (vm, b2,
								  ip6_2,
								  &bogus);
	      if (udp2->checksum == 0)
		udp2->checksum = 0xffff;
	      udp3->checksum = ip6_tcp_udp_icmp_compute_checksum (vm, b3,
								  ip6_3,
								  &bogus);
	      if (udp3->checksum == 0)
		udp3->checksum = 0xffff;
	    }

	  /* clear the checksum offload flags */
	  b0->flags &= csum_mask;
	  b1->flags &= csum_mask;
	  b2->flags &= csum_mask;
	  b3->flags &= csum_mask;

	  pkts_encapsulated += 4;

	  /* save inner packet flow_hash for load-balance node */
	  vnet_buffer (b0)->ip.flow_hash = flow_hash0;
	  vnet_buffer (b1)->ip.flow_hash = flow_hash1;
	  vnet_buffer (b2)->ip.flow_hash = flow_hash2;
	  vnet_buffer (b3)->ip.flow_hash = flow_hash3;

	  if (PREDICT_FALSE (b0->flags & VLIB_BUFFER_IS_TRACED))
	    {
	      upf_encap_trace_t *tr =
		vlib_add_trace (vm, node, b0, sizeof (*tr));
	      tr->session_index = s0 - gtm->sessions;
	      tr->teid = far0->forward.outer_header_creation.teid;
	    }

	  if (PREDICT_FALSE (b1->flags & VLIB_BUFFER_IS_TRACED))
	    {
	      upf_encap_trace_t *tr =
		vlib_add_trace (vm, node, b1, sizeof (*tr));
	      tr->session_index = s1 - gtm->sessions;
	      tr->teid = far1->forward.outer_header_creation.teid;
	    }

	  if (PREDICT_FALSE (b2->flags & VLIB_BUFFER_IS_TRACED))
	    {
	      upf_encap_trace_t *tr =
		vlib_add_trace (vm, node, b2, sizeof (*tr));
	      tr->session_index = s2 - gtm->sessions;
	      tr->teid = far2->forward.outer_header_creation.teid;
	    }

	  if (PREDICT_FALSE (b3->flags & VLIB_BUFFER_IS_TRACED))
	    {
	      upf_encap_trace_t *tr =
		vlib_add_trace (vm, node, b3, sizeof (*tr));
	      tr->session_index = s3 - gtm->sessions;
	      tr->teid = far3->forward.outer_header_creation.teid;
	    }

	  vlib_validate_buffer_enqueue_x4 (vm, node, next_index,
					   to_next, n_left_to_next,
					   bi0, bi1, bi2, bi3,
					   next0, next1, next2, next3);
	}

      while (n_left_from > 0 && n_left_to_next > 0)
	{
	  u32 bi0;
	  vlib_buffer_t *b0;
	  u32 flow_hash0;
	  ip4_header_t *ip4_0;
	  ip6_header_t *ip6_0;
	  udp_header_t *udp0;
	  gtpu_header_t *gtpu0;
	  u64 *copy_src0, *copy_dst0;
	  u32 *copy_src_last0, *copy_dst_last0;
	  u16 new_l0;
	  ip_csum_t sum0;

	  bi0 = from[0];
	  to_next[0] = bi0;
	  from += 1;
	  to_next += 1;
	  n_left_from -= 1;
	  n_left_to_next -= 1;

	  b0 = vlib_get_buffer (vm, bi0);
          UPF_CHECK_INNER_NODE (b0);

	  flow_hash0 = vnet_l2_compute_flow_hash (b0);

	  /* Get next node index and adj index from tunnel next_dpo */
	  s0 = &gtm->sessions[upf_buffer_opaque (b0)->gtpu.session_index];

	  r0 = pfcp_get_rules (s0, PFCP_ACTIVE);

	  /* TODO: this should be optimized */
	  pdr0 = vec_elt_at_index (r0->pdr, upf_buffer_opaque (b0)->gtpu.pdr_idx);
	  far0 = pfcp_get_far_by_id (r0, pdr0->far_id);

	  peer0 = pool_elt_at_index (gtm->peers, far0->forward.peer_idx);

	  /* Note: change to always set next0 if it may be set to drop */
	  next0 = peer0->next_dpo.dpoi_next_node;
	  vnet_buffer (b0)->ip.adj_index[VLIB_TX] =
	    peer0->next_dpo.dpoi_index;

	  if (PREDICT_FALSE ((upf_buffer_opaque (b0)->gtpu.hdr_flags & GTPU_E_S_PN_BIT) != 0))
	    vlib_buffer_advance (b0, -upf_buffer_opaque (b0)->gtpu.ext_hdr_len);

	  /* Apply the rewrite string. $$$$ vnet_rewrite? */
	  vlib_buffer_advance (b0, -(word) _vec_len (far0->forward.rewrite));

	  if (is_ip4)
	    {
	      ip4_0 = vlib_buffer_get_current (b0);

	      /* Copy the fixed header */
	      copy_dst0 = (u64 *) ip4_0;
	      copy_src0 = (u64 *) far0->forward.rewrite;
	      /* Copy first 32 octets 8-bytes at a time */
#define _(offs) copy_dst0[offs] = copy_src0[offs];
	      foreach_fixed_header4_offset;
#undef _
	      /* Last 4 octets. Hopefully gcc will be our friend */
	      copy_dst_last0 = (u32 *) (&copy_dst0[4]);
	      copy_src_last0 = (u32 *) (&copy_src0[4]);
	      copy_dst_last0[0] = copy_src_last0[0];

	      /* Fix the IP4 checksum and length */
	      sum0 = ip4_0->checksum;
	      new_l0 =		/* old_l0 always 0, see the rewrite setup */
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b0));
	      sum0 = ip_csum_update (sum0, old_l0, new_l0, ip4_header_t,
				     length /* changed member */ );
	      ip4_0->checksum = ip_csum_fold (sum0);
	      ip4_0->length = new_l0;

	      /* Fix UDP length and set source port */
	      udp0 = (udp_header_t *) (ip4_0 + 1);
	      new_l0 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b0) -
				      sizeof (*ip4_0));
	      udp0->length = new_l0;
	      udp0->src_port = flow_hash0;

	      /* Fix GTPU length */
	      gtpu0 = (gtpu_header_t *) (udp0 + 1);
	      new_l0 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b0) -
				      sizeof (*ip4_0) - sizeof (*udp0) -
				      GTPU_V1_HDR_LEN);
	      gtpu0->length = new_l0;
	      gtpu0->ver_flags |= (upf_buffer_opaque (b0)->gtpu.hdr_flags & GTPU_E_S_PN_BIT);
	    }

	  else			/* ip6 path */
	    {
	      int bogus = 0;

	      ip6_0 = vlib_buffer_get_current (b0);
	      /* Copy the fixed header */
	      copy_dst0 = (u64 *) ip6_0;
	      copy_src0 = (u64 *) far0->forward.rewrite;
	      /* Copy first 56 (ip6) octets 8-bytes at a time */
#define _(offs) copy_dst0[offs] = copy_src0[offs];
	      foreach_fixed_header6_offset;
#undef _
	      /* Fix IP6 payload length */
	      new_l0 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b0)
				      - sizeof (*ip6_0));
	      ip6_0->payload_length = new_l0;

	      /* Fix UDP length  and set source port */
	      udp0 = (udp_header_t *) (ip6_0 + 1);
	      udp0->length = new_l0;
	      udp0->src_port = flow_hash0;

	      /* Fix GTPU length */
	      gtpu0 = (gtpu_header_t *) (udp0 + 1);
	      new_l0 =
		clib_host_to_net_u16 (vlib_buffer_length_in_chain (vm, b0) -
				      sizeof (*ip6_0) - sizeof (*udp0) -
				      GTPU_V1_HDR_LEN);
	      gtpu0->length = new_l0;
	      gtpu0->ver_flags |= (upf_buffer_opaque (b0)->gtpu.hdr_flags & GTPU_E_S_PN_BIT);

	      /* IPv6 UDP checksum is mandatory */
	      udp0->checksum = ip6_tcp_udp_icmp_compute_checksum (vm, b0,
								  ip6_0,
								  &bogus);
	      if (udp0->checksum == 0)
		udp0->checksum = 0xffff;
	    }

	  /* clear the checksum offload flags */
	  b0->flags &= csum_mask;

	  pkts_encapsulated++;

	  /* save inner packet flow_hash for load-balance node */
	  vnet_buffer (b0)->ip.flow_hash = flow_hash0;

	  if (PREDICT_FALSE (b0->flags & VLIB_BUFFER_IS_TRACED))
	    {
	      upf_encap_trace_t *tr =
		vlib_add_trace (vm, node, b0, sizeof (*tr));
	      tr->session_index = s0 - gtm->sessions;
	      tr->teid = far0->forward.outer_header_creation.teid;
	    }
	  vlib_validate_buffer_enqueue_x1 (vm, node, next_index,
					   to_next, n_left_to_next,
					   bi0, next0);
	}

      vlib_put_next_frame (vm, node, next_index, n_left_to_next);
    }

  /* Do we still need this now that tunnel tx stats is kept? */
  vlib_node_increment_counter (vm, node->node_index,
			       UPF_ENCAP_ERROR_ENCAPSULATED,
			       pkts_encapsulated);

  return from_frame->n_vectors;
}

VLIB_NODE_FN (upf4_encap_node) (vlib_main_t * vm,
				vlib_node_runtime_t * node,
				vlib_frame_t * from_frame)
{
  return upf_encap_inline (vm, node, from_frame, /* is_ip4 */ 1);
}

VLIB_NODE_FN (upf6_encap_node) (vlib_main_t * vm,
				vlib_node_runtime_t * node,
				vlib_frame_t * from_frame)
{
  return upf_encap_inline (vm, node, from_frame, /* is_ip4 */ 0);
}

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (upf4_encap_node) = {
  .name = "upf4-encap",
  .vector_size = sizeof (u32),
  .format_trace = format_upf_encap_trace,
  .type = VLIB_NODE_TYPE_INTERNAL,
  .n_errors = ARRAY_LEN(upf_encap_error_strings),
  .error_strings = upf_encap_error_strings,
  .n_next_nodes = UPF_ENCAP_N_NEXT,
  .next_nodes = {
#define _(s,n) [UPF_ENCAP_NEXT_##s] = n,
    foreach_upf_encap_next
#undef _
  },
};
/* *INDENT-ON* */

/* *INDENT-OFF* */
VLIB_REGISTER_NODE (upf6_encap_node) = {
  .name = "upf6-encap",
  .vector_size = sizeof (u32),
  .format_trace = format_upf_encap_trace,
  .type = VLIB_NODE_TYPE_INTERNAL,
  .n_errors = ARRAY_LEN(upf_encap_error_strings),
  .error_strings = upf_encap_error_strings,
  .n_next_nodes = UPF_ENCAP_N_NEXT,
  .next_nodes = {
#define _(s,n) [UPF_ENCAP_NEXT_##s] = n,
    foreach_upf_encap_next
#undef _
  },
};
/* *INDENT-ON* */
