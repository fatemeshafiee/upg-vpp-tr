//
// Created by Fatemeh Shafiei Ardestani on 2024-08-08.
//

#ifndef UPG_VPP_UPF_EE_TYPES_H
#define UPG_VPP_UPF_EE_TYPES_H

#endif //UPG_VPP_UPF_EE_TYPES_H

typedef struct
{
    u64 seid;
    ip46_address_t src_ip;
    ip46_address_t dst_ip;
    u16 src_port;
    u16 dst_port;
    u8 proto;
    u32 src_pkts;
    u64 src_bytes;
    u32 dst_pkts;
    u64 dst_bytes;

} usage_report_per_flow_t;

typedef struct
{
    u64 seid;
    time_t packet_time;
    u16 packet_length;
    char * highest_layer;
    int ip_flags;
    ip46_address_t src_ip;
    ip46_address_t dst_ip;
    int tcp_length;
    int tcp_ack;
    int tcp_flags;
    int tcp_window_size;
    int udp_length;
    int ICMP_type;
} usage_report_per_packet_t;