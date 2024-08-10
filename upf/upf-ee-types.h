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
    u32 pkts;
    u64 bytes;

} usage_report_per_flow_t;