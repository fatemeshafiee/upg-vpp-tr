//
// Created by Fatemeh Shafiei Ardestani on 2024-08-11.
//

#ifndef UPG_VPP_UPF_PREPARE_DATA_H
#define UPG_VPP_UPF_PREPARE_DATA_H
void prepare_ee_data(flowtable_main_t *fm);
void u32_to_ip(uint32_t ip, char *str_ip);
void prepare_ee_data_per_packet(u8 is_ip4,u8 * p0, vlib_buffer_t * b0);
#endif //UPG_VPP_UPF_PREPARE_DATA_H
