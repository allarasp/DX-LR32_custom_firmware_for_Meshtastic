#include "radio_phy.h"
#include "sx126x_regs.h"
#include "memory.h"
uint8_t phy_pending_type,phy_active_type;
struct phy_gfsk phy_pending,phy_active;
static const uint32_t bandwidths[]={4800,5800,7300,9700,11700,14600,19500,23400,29300,39000,46900,58600,78200,93800,117300,156200,187200,234300,312000,373600,467000};
static bool ok(sx126x_status_t s) {return s==SX126X_STATUS_OK;}
static bool bandwidth(uint32_t hz,uint8_t *code) {
    bool found=false;for(unsigned i=0;i<sizeof(bandwidths)/sizeof(bandwidths[0]);++i)if(hz==bandwidths[i])found=true;
    static const uint8_t codes[]={0x1f,0x17,0x0f,0x1e,0x16,0x0e,0x1d,0x15,0x0d,0x1c,0x14,0x0c,0x1b,0x13,0x0b,0x1a,0x12,0x0a,0x19,0x11,0x09};
    if(found)for(unsigned i=0;i<sizeof(codes);++i)if(hz==bandwidths[i]) {*code=codes[i];return true;}
    return false;
}
static bool fields(const struct phy_gfsk *g) {
    uint8_t code;
    return g->bitrate>=600&&g->bitrate<=300000&&g->deviation>0&&g->deviation<=200000&&bandwidth(g->bandwidth,&code)&&
        (g->shape==0||(g->shape>=8&&g->shape<=11))&&g->preamble>=8&&
        (g->detector==0||(g->detector>=4&&g->detector<=7))&&g->sync_bits<=64&&g->sync_bits%8==0&&
        g->address_filter<=2&&g->header<=1&&g->payload>0&&
        (g->crc==0||g->crc==1||g->crc==2||g->crc==4||g->crc==6)&&g->whitening<=1&&g->whitening_seed<=511&&g->crc_polynomial!=0;
}
void phy_init(void) {
    phy_pending_type=phy_active_type=1;
    phy_pending=(struct phy_gfsk){50000,25000,117300,32,0x1d0f,0x1021,0x1ff,{0x2d,0xd4},9,5,16,0,1,255,6,1,0,255};
    phy_active=phy_pending;
}
uint8_t phy_stage(const uint8_t *p) {
    struct phy_gfsk g={0};
    g.bitrate=rp_u32(p);g.deviation=rp_u32(p+4);g.bandwidth=rp_u32(p+8);g.preamble=rp_u16(p+12);
    g.crc_seed=rp_u16(p+14);g.crc_polynomial=rp_u16(p+16);g.whitening_seed=rp_u16(p+18);
    memcpy(g.sync,p+20,8);g.shape=p[28];g.detector=p[29];g.sync_bits=p[30];g.address_filter=p[31];
    g.header=p[32];g.payload=p[33];g.crc=p[34];g.whitening=p[35];g.node=p[36];g.broadcast=p[37];
    if(!fields(&g))return RP_RANGE;
    phy_pending=g;return RP_OK;
}
void phy_encode(uint8_t *p,const struct phy_gfsk *g) {
    rp_put32(p,g->bitrate);rp_put32(p+4,g->deviation);rp_put32(p+8,g->bandwidth);rp_put16(p+12,g->preamble);
    rp_put16(p+14,g->crc_seed);rp_put16(p+16,g->crc_polynomial);rp_put16(p+18,g->whitening_seed);memcpy(p+20,g->sync,8);
    p[28]=g->shape;p[29]=g->detector;p[30]=g->sync_bits;p[31]=g->address_filter;
    p[32]=g->header;p[33]=g->payload;p[34]=g->crc;p[35]=g->whitening;p[36]=g->node;p[37]=g->broadcast;
}
uint8_t phy_validate(const struct rc_config *common) {
    uint32_t lower,upper;uint8_t status=rc_frequency_bounds(common->frequency,&lower,&upper);
    if(status)return status;
    if(!fields(&phy_pending)||common->power< -9||common->power>RC_POWER_MAX)return RP_RANGE;
    if(common->frequency-lower<phy_pending.bandwidth/2||upper-common->frequency<phy_pending.bandwidth/2||
        phy_pending.bitrate+2*phy_pending.deviation>phy_pending.bandwidth)return RP_COMBINATION;
    if(phy_pending.detector && phy_pending.preamble<(uint16_t)(8*(phy_pending.detector-3)))return RP_COMBINATION;
    return RP_OK;
}
static sx126x_pkt_params_gfsk_t packet(const struct phy_gfsk *g,uint8_t n) {
    sx126x_pkt_params_gfsk_t p={g->preamble,(sx126x_gfsk_preamble_detector_t)g->detector,g->sync_bits,
        (sx126x_gfsk_address_filtering_t)g->address_filter,(sx126x_gfsk_pkt_len_modes_t)g->header,n,
        (sx126x_gfsk_crc_types_t)g->crc,(sx126x_gfsk_dc_free_t)g->whitening};return p;
}
bool phy_apply(void) {
    uint8_t code;if(!bandwidth(phy_pending.bandwidth,&code))return false;
    sx126x_mod_params_gfsk_t m={phy_pending.bitrate,phy_pending.deviation,(sx126x_gfsk_pulse_shape_t)phy_pending.shape,(sx126x_gfsk_bw_t)code};
    sx126x_pkt_params_gfsk_t p=packet(&phy_pending,phy_pending.payload);
    if(!ok(sx126x_set_gfsk_mod_params(0,&m))||!ok(sx126x_set_gfsk_pkt_params(0,&p))||
       !ok(sx126x_set_gfsk_sync_word(0,phy_pending.sync,8))||!ok(sx126x_set_gfsk_crc_seed(0,phy_pending.crc_seed))||
       !ok(sx126x_set_gfsk_crc_polynomial(0,phy_pending.crc_polynomial))||!ok(sx126x_set_gfsk_whitening_seed(0,phy_pending.whitening_seed))||
       !ok(sx126x_set_gfsk_pkt_address(0,phy_pending.node,phy_pending.broadcast))||!ok(sx126x_workaround_gfsk_reset(0)))return false;
    if(phy_pending.bitrate==600&&!ok(sx126x_workaround_gfsk_0_6_kbps(0)))return false;
    if(phy_pending.bitrate==1200&&!ok(sx126x_workaround_gfsk_1_2_kbps(0)))return false;
    uint8_t sync[8],seed[2],poly[2],white[2],address[2],txmod;
    sx126x_pkt_type_t type;
    if(!ok(sx126x_get_pkt_type(0,&type))||type!=SX126X_PKT_TYPE_GFSK||
       !ok(sx126x_read_register(0,0x6c0,sync,8))||memcmp(sync,phy_pending.sync,8)||
       !ok(sx126x_read_register(0,0x6bc,seed,2))||((uint16_t)seed[0]<<8|seed[1])!=phy_pending.crc_seed||
       !ok(sx126x_read_register(0,0x6be,poly,2))||((uint16_t)poly[0]<<8|poly[1])!=phy_pending.crc_polynomial||
       !ok(sx126x_read_register(0,0x6b8,white,2))||(((uint16_t)(white[0]&1)<<8)|white[1])!=phy_pending.whitening_seed||
       !ok(sx126x_read_register(0,0x6cd,address,2))||address[0]!=phy_pending.node||address[1]!=phy_pending.broadcast||
       !ok(sx126x_read_register(0,0x889,&txmod,1))||!(txmod&4))return false;
    phy_active=phy_pending;return true;
}
bool phy_packet_limit(uint8_t length) {
    sx126x_pkt_params_gfsk_t p=packet(&phy_active,length);return ok(sx126x_set_gfsk_pkt_params(0,&p));
}
uint32_t phy_airtime(uint8_t length) {
    uint8_t code;if(!bandwidth(phy_active.bandwidth,&code))return 0;
    sx126x_mod_params_gfsk_t m={phy_active.bitrate,phy_active.deviation,(sx126x_gfsk_pulse_shape_t)phy_active.shape,(sx126x_gfsk_bw_t)code};
    sx126x_pkt_params_gfsk_t p=packet(&phy_active,length);return sx126x_get_gfsk_time_on_air_in_ms(&p,&m);
}
