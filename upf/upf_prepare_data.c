// Added by: Fatemeh Shafiei Ardestani
// Date: 2024-08-18.
//  See Git history for complete list of changes.

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
#include "upf/upf-ee/EE-init.h"

#if CLIB_DEBUG > 1
#define flow_debug clib_warning
#else
#define flow_debug(...)				\
  do { } while (0)
#endif
#include "upf.h"
#include <vnet/vnet.h>
#include <vnet/tcp/tcp_packet.h>
#include <vnet/udp/udp_packet.h>
#include <vnet/ip/ip.h>
#include <vnet/tcp/tcp_inlines.h>
#include <vnet/fib/ip4_fib.h>
#include <vnet/fib/ip6_fib.h>
//extern struct {char* key; usage_report_per_flow_t* value;} *usage_hash;
#define TCP_PROTOCOL 6
#define UDP_PROTOCOL 17

void prepare_ee_data(flow_entry_t *flows){
  flow_entry_t *flow;
  pthread_mutex_lock(&ee_lock);
  u32 num_flows = vec_len(flows);
  u32 num = pool_len(flows);
//  shfree(usage_hash);
  sh_new_strdup(usage_hash);
  shdefault(usage_hash, NULL);
    for(u32 i=0; i < num; i++){
//      clib_warning("in the for the i is %d", i);
      flow = pool_elt_at_index (flows, i);
      if (flow->stats[0].pkts!=0 || flow->stats[1].pkts!=0){
        usage_report_per_flow_t *new_data = malloc(sizeof(usage_report_per_flow_t));
        flow_key_t key = flow->key;
        if(key.proto == 58){
          new_data->dst_ip = malloc( 40 * sizeof(char));
          new_data->src_ip = malloc(40 * sizeof(char));

          u8* f = key.ip[1].as_u8;
          sprintf(new_data->src_ip, "%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X",
                  f[0],f[1],f[2],f[3],
                  f[4],f[5],f[6],f[7],
                  f[8],f[9],f[10],f[11],
                  f[12],f[13],f[14],f[15]
                  );

          u8* f2 = key.ip[0].as_u8;
          sprintf(new_data->dst_ip, "%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X:%02X%02X",
                  f2[0],f2[1],f2[2],f2[3],
                  f2[4],f2[5],f2[6],f2[7],
                  f2[8],f2[9],f2[10],f2[11],
                  f2[12],f2[13],f2[14],f2[15]
          );
        }
        else{
          new_data->dst_ip = malloc(16 * sizeof(char));
          new_data->src_ip = malloc(16 * sizeof(char));

          u8* f = key.ip[1].as_u8;
          sprintf(new_data->src_ip, "%d.%d.%d.%d", f[12], f[13], f[14], f[15]);

          u8* f2 = key.ip[0].as_u8;
          sprintf(new_data->dst_ip, "%d.%d.%d.%d", f2[12], f2[13], f2[14], f2[15]);

        }

        clib_warning("[1|flow_info] ip[1].  %s", new_data->src_ip);
        clib_warning("[1|flow_info] ip[0].  %s", new_data->dst_ip);
        clib_warning("[3| flow_info] port[0] %u", key.port[0]);
        clib_warning("[4| flow_info] port[1] %u", key.port[1]);
        clib_warning("[5| flow_info] portocol %u", key.proto);
        clib_warning("[6| flow_info] stst 0 pkts %d", flow->stats[0].pkts);
        clib_warning("[7| flow_info] stst 0 bytes %d", flow->stats[0].bytes);
        clib_warning("[8| flow_info] stst 1 pkts %d", flow->stats[1].pkts);
        clib_warning("[9| flow_info] stst 1 bytes %d", flow->stats[1].bytes);

        new_data->seid = key.seid;
        clib_warning("line 99 of prepare ee data");
        new_data->src_port = key.port[1];
        clib_warning("line 101 of prepare ee data");
        new_data->dst_port = key.port[0];
        clib_warning("line 103 of prepare ee data");
        new_data->proto = key.proto;
        clib_warning("line 105 of prepare ee data");
        new_data->src_pkts = flow->stats[1].pkts;
        clib_warning("line 107 of prepare ee data");
        new_data->src_bytes = flow->stats[1].bytes;
        clib_warning("line 109 of prepare ee data");
        new_data->dst_pkts = flow->stats[0].pkts;
        clib_warning("line 111 of prepare ee data");
        new_data->dst_bytes = flow->stats[0].bytes;
        clib_warning("line 113 of prepare ee data");
        size_t hash_length = shlen(usage_hash);
        clib_warning("line 115 of prepare ee data the hash size is %d",hash_length );
        usage_report_per_flow_t* usage_report_per_flow_vector = shget(usage_hash, new_data->src_ip);
        clib_warning("line 117 of prepare ee data");

        if(usage_report_per_flow_vector == NULL){

          clib_warning("1'in the if of prepare ee data");
          usage_report_per_flow_vector = NULL;
          vec_add1(usage_report_per_flow_vector,*new_data);
          clib_warning("2'in the if of prepare ee data");
        }
        else{
          clib_warning("1'in the else of prepare ee data");
          vec_add1(usage_report_per_flow_vector,*new_data);
          shdel(usage_hash, new_data->src_ip);
          clib_warning("2'in the else of prepare ee data");
        }
        hash_length = shlen(usage_hash);
        clib_warning("[CRASH] before add, the hash size is %d",hash_length);
        shput(usage_hash, new_data->src_ip, usage_report_per_flow_vector);
        hash_length = shlen(usage_hash);
        clib_warning("[CRASH] AFTER adding.the hash size is %d", hash_length);
      }
    }
  clib_warning("end of prepare ee data");

  pthread_mutex_unlock(&ee_lock);
  return;
}

//void prepare_ee_data_per_packet(u8 is_ip4,u8 * p0, vlib_buffer_t * b0, time_t current_time){
//  pthread_mutex_lock(&ee_lock);
//
//}

//void u32_to_ip(uint32_t ip, char *str_ip) {
//  sprintf(str_ip, "%u.%u.%u.%u",
//          (ip >> 24) & 0xFF,
//          (ip >> 16) & 0xFF,
//          (ip >> 8) & 0xFF,
//          ip & 0xFF
//  );
//}
//
//void prepare_ee_data_per_packet(u8 is_ip4,u8 * p0, vlib_buffer_t * b0, time_t current_time){
//
//  pthread_mutex_lock(&ee_lock);
////  usage_packet_hash = NULL;
//  sh_new_strdup(usage_packet_hash);
//  shdefault(usage_packet_hash, NULL);
//  usage_report_per_packet_t *new_data = malloc(sizeof(usage_report_per_packet_t));
//  new_data->highest_layer = malloc(10 * sizeof(char));
////  ip6_header_t *
//  u8* payload;
//  int ip_header_length = 40;
//  if(is_ip4){
//    ip4_header_t* p4= (ip4_header_t*) p0;
//    new_data->key->dst_ip = malloc(16 * sizeof(char));
//    new_data->key->src_ip = malloc(16 * sizeof(char));
//    ip_header_length = ((p4->ip_version_and_header_length >> 4) & 0x0F) * 4;
//    u32_to_ip(p4->src_address.data_u32, new_data->key->src_ip);
//    u32_to_ip (p4->dst_address.data_u32, new_data->key->dst_ip);
//    new_data->ip_flags = p4->flags_and_fragment_offset & 0x7;
//    new_data->packet_length = p4->length; //it is the total length
//    new_data->key->proto = p4->protocol;
//    payload = vlib_buffer_get_current (b0) +
//              upf_buffer_opaque (b0)->gtpu.data_offset + ip_header_length;
//    new_data->highest_layer = "DATA";
//    new_data->is_reverse = (ip4_address_compare (&p4->src_address, &p4->dst_address) < 0) ?
//                           1 : 0;
//  }
//  else{
//    new_data->key->dst_ip = malloc( 40 * sizeof(char));
//    new_data->key->src_ip = malloc(40 * sizeof(char));
//    ip6_header_t * p6 = (ip6_header_t *) p0;
//    u8* f = p6->src_address.as_u8;
//    sprintf(new_data->key->src_ip, "%d.%d.%d.%d", f[12], f[13], f[14], f[15]);
//    f = p6->dst_address.as_u8;
//    sprintf(new_data->key->dst_ip, "%d.%d.%d.%d", f[12], f[13], f[14], f[15]);
//    new_data->ip_flags = 0;
//    new_data->packet_length = p6->payload_length;
//    new_data->key->proto = p6->protocol;
//    payload = vlib_buffer_get_current (b0) +
//                  upf_buffer_opaque (b0)->gtpu.data_offset + 40;
//    new_data->highest_layer = "DATA";
//    new_data->is_reverse = (ip6_address_compare (&p6->src_address, &p6->dst_address) < 0) ?
//                               1 : 0;
//  }
//
//  if(new_data->key->proto == TCP_PROTOCOL){
//    tcp_header_t* tcp = (struct tcp_header_t*) payload;
//    new_data->tcp_ack = tcp->ack_number;
//    new_data->tcp_flags = tcp->flags;
//    new_data->key->src_port = tcp->src_port;
//    new_data->key->dst_port = tcp->dst_port;
//    new_data->tcp_window_size = tcp->window;
//    int tcp_header_length = (tcp->data_offset_and_reserved & 0x0F) * 4;
//    new_data->tcp_length = new_data->packet_length - ip_header_length - tcp_header_length;
//    new_data->udp_length = 0;
//    new_data->ICMP_type = 0;
//    new_data->highest_layer = "TCP";
//    if (new_data->key->src_port == 80 || new_data->key->dst_port == 80) {
//      new_data->highest_layer = "HTTP";
//    } else if (new_data->key->src_port == 443 || new_data->key->dst_port == 443) {
//      new_data->highest_layer = "HTTPS";
//    } else if (new_data->key->src_port == 25 || new_data->key->dst_port == 25) {
//      new_data->highest_layer = "SMTP";
//    } else if (new_data->key->src_port == 110 || new_data->key->dst_port == 110) {
//      new_data->highest_layer = "POP3";
//    } else if (new_data->key->src_port == 143 || new_data->key->dst_port == 143) {
//      new_data->highest_layer = "IMAP";
//    } else if (new_data->key->src_port == 22 || new_data->key->dst_port == 22) {
//      new_data->highest_layer = "SSH";
//    } else if (new_data->key->src_port == 443 || new_data->key->dst_port == 443) {
//      new_data->highest_layer = "TLS";
//    }
//  }
//  else if (new_data->key->proto == UDP_PROTOCOL){
//    udp_header_t* udp = (udp_header_t*) payload;
//    new_data->highest_layer = "UDP";
//
//    new_data->tcp_length = 0;
//    new_data->tcp_flags = 0;
//    new_data->tcp_window_size = 0;
//    new_data->tcp_ack = 0;
//    new_data->ICMP_type = 0;
//
//    new_data->key->src_port = udp->src_port;
//    new_data->key->dst_port = udp-> dst_port;
//    new_data->udp_length = udp->length - 8;
//    if (new_data->key->src_port == 53 || new_data->key->dst_port == 53) {
//      new_data->highest_layer = "DNS";
//    } else if (new_data->key->src_port == 443 || new_data->key->dst_port == 443) {
//      new_data->highest_layer = "DTLS";
//    }
//
//  }
//  else if(new_data->key->proto == 1){
//    new_data->highest_layer = "ICMP";
//    new_data->tcp_length = 0;
//    new_data->tcp_flags = 0;
//    new_data->tcp_window_size = 0;
//    new_data->tcp_ack = 0;
//    new_data->key->src_port = 0;
//    new_data->key->dst_port = 0;
//    new_data->udp_length = 0;
//    new_data->ICMP_type = *payload;
//  }
//  else{
//    new_data->tcp_length = 0;
//    new_data->tcp_flags = 0;
//    new_data->tcp_window_size = 0;
//    new_data->tcp_ack = 0;
//    new_data->udp_length = 0;
//    new_data->ICMP_type = 0;
//    new_data->key->src_port = 0;
//    new_data->key->dst_port = 0;
//    new_data->key->proto = 0;
//  }
//  usage_report_per_packet_t * usage_report_per_packet_vector = shget(usage_packet_hash, new_data->key);
//  if(usage_report_per_packet_vector == NULL){
//    usage_report_per_packet_vector = NULL;
//    vec_add1(usage_report_per_packet_vector,*new_data);
//  }
//  else{
//
//    vec_add1(usage_report_per_packet_vector,*new_data);
//            shdel(usage_packet_hash, new_data->key);
//  }
//  shput(usage_packet_hash, new_data->key, usage_report_per_packet_vector);
//  new_data->packet_time = current_time;
//  pthread_mutex_unlock(&ee_lock);
//
//}