#ifndef RADIO_PHY_H
#define RADIO_PHY_H
#include "radio_control.h"
#include "sx126x.h"
#define PHY_GFSK_WIRE_SIZE 38u
#define PHY_GFSK_MASK 0x1bu
struct phy_gfsk {
    uint32_t bitrate,deviation,bandwidth;
    uint16_t preamble,crc_seed,crc_polynomial,whitening_seed;
    uint8_t sync[8];
    uint8_t shape,detector,sync_bits,address_filter,header,payload,crc,whitening,node,broadcast;
};
extern uint8_t phy_pending_type,phy_active_type;
extern struct phy_gfsk phy_pending,phy_active;
void phy_init(void);
uint8_t phy_validate(const struct rc_config *common);
bool phy_apply(void);
uint8_t phy_stage(const uint8_t *p);
void phy_encode(uint8_t *p,const struct phy_gfsk *g);
bool phy_packet_limit(uint8_t length);
uint32_t phy_airtime(uint8_t length);
#endif
