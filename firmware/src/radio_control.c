#include <stddef.h>
#include "radio_control.h"
#include "radio_diag.h"
#include "rf_switch.h"
#include "sx126x.h"
#include "sx126x_regs.h"

struct rc_config rc_pending,rc_active;
bool rc_active_valid;
uint32_t rc_generation;
static bool dirty;
static uint8_t raw_status;
static uint16_t device_errors;
static uint16_t applied_mask,readback_mask;
static const uint32_t bandwidths[]={7800,10400,15600,20800,31250,41700,62500,125000,250000,500000};
static const uint8_t bw_codes[]={0,8,1,9,2,10,3,4,5,6};
static int bandwidth_code(uint32_t bw) {
    for(unsigned i=0;i<sizeof(bandwidths)/sizeof(bandwidths[0]);++i) if(bw==bandwidths[i]) return bw_codes[i];
    return -1;
}
static void encode(uint8_t *p,const struct rc_config *c) {
    rp_put32(p,c->frequency);rp_put32(p+4,c->bandwidth);rp_put16(p+8,c->preamble);rp_put16(p+10,c->sync);
    p[12]=c->sf;p[13]=c->cr;p[14]=c->ldro;p[15]=c->header;p[16]=c->crc;p[17]=c->iq;p[18]=c->payload;p[19]=(uint8_t)c->power;
}
static bool hal(sx126x_status_t s) {return s==SX126X_STATUS_OK;}
static bool mode_status(sx126x_chip_modes_t mode,uint8_t *raw) {
    *raw=0xff;
    sx126x_chip_status_t s;
    if(!hal(sx126x_get_status(NULL,&s))) return false;
    *raw=radio_link.rx[1];
    return !(*raw&0x81u) && s.chip_mode==mode &&
        s.cmd_status!=SX126X_CMD_STATUS_CMD_TIMEOUT && s.cmd_status!=SX126X_CMD_STATUS_CMD_PROCESS_ERROR &&
        s.cmd_status!=SX126X_CMD_STATUS_CMD_EXEC_FAILURE;
}
static bool status(void) {return mode_status(SX126X_CHIP_MODE_STBY_RC,&raw_status);}
static bool errors(void) {
    return hal(sx126x_get_device_errors(NULL,&device_errors)) && !(device_errors&~0x017fu);
}
static uint8_t reset(void) {
    rc_active_valid=false;applied_mask=readback_mask=0;dirty=true;++rc_generation;
    raw_status=0xff;device_errors=0xffff;
    return hal(sx126x_reset(NULL)) && hal(sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC)) &&
        status() && errors() && device_errors==0 ? RP_OK:RP_RADIO_ERROR;
}
uint8_t rc_init(void) {
    rc_pending=(struct rc_config){433875000,250000,16,0x24b4,11,5,0,0,1,0,255,10};
    rc_active=(struct rc_config){0};rc_generation=0;radio_io_init();
    enum rf_switch_error pins=rf_switch_init();
    uint8_t result=reset();return pins==RF_SWITCH_OK?result:RP_READBACK;
}
static uint8_t valid(const struct rc_config *c) {
    if(c->frequency<RC_MODULE_MIN || c->frequency>RC_MODULE_MAX) return RP_UNSUPPORTED;
    if(c->frequency<RC_POLICY_MIN || c->frequency>RC_POLICY_MAX) return RP_RANGE;
    if(bandwidth_code(c->bandwidth)<0 || c->sf<5 || c->sf>12 || c->cr<5 || c->cr>8 || c->ldro>1 ||
       c->header>1 || c->crc>1 || c->iq>1 || c->payload==0 || c->preamble<8 ||
       c->power < -9 || c->power>RC_POWER_MAX) return RP_RANGE;
    if(c->frequency-RC_POLICY_MIN<c->bandwidth/2 || RC_POLICY_MAX-c->frequency<c->bandwidth/2 ||
       (c->sf<=6 && c->preamble<12) || (c->ldro && c->sf<7) ||
       (!c->ldro && ((uint64_t)1u<<c->sf)*1000u>=16u*(uint64_t)c->bandwidth)) return RP_COMBINATION;
    return RP_OK;
}
static uint8_t apply(void) {
    uint8_t result=valid(&rc_pending);
    if(result) return result; /* Invalid candidate never changes the radio/active snapshot. */
    rc_active_valid=false;applied_mask=readback_mask=0;++rc_generation;dirty=true;
    sx126x_mod_params_lora_t m={(sx126x_lora_sf_t)rc_pending.sf,(sx126x_lora_bw_t)bandwidth_code(rc_pending.bandwidth),
        (sx126x_lora_cr_t)(rc_pending.cr-4),rc_pending.ldro};
    sx126x_pkt_params_lora_t pkt={rc_pending.preamble,(sx126x_lora_pkt_len_modes_t)rc_pending.header,
        rc_pending.payload,rc_pending.crc!=0,rc_pending.iq!=0};
    uint8_t sync[2]={(uint8_t)(rc_pending.sync>>8),(uint8_t)rc_pending.sync};
    if(!hal(sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC)) || !status() ||
       !hal(sx126x_set_pkt_type(NULL,SX126X_PKT_TYPE_LORA)) || !status() ||
       !hal(sx126x_set_rf_freq(NULL,rc_pending.frequency)) || !status() ||
       !hal(sx126x_set_lora_mod_params(NULL,&m)) || !status() ||
       !hal(sx126x_set_lora_pkt_params(NULL,&pkt)) || !status() ||
       !hal(sx126x_write_register(NULL,SX126X_REG_LR_SYNCWORD,sync,2)) || !status() ||
       !errors() || device_errors) return RP_RADIO_ERROR;
    sx126x_pkt_type_t type;
    uint8_t read_sync[2],iq,txmod;
    if(!hal(sx126x_get_pkt_type(NULL,&type)) ||
       !hal(sx126x_read_register(NULL,SX126X_REG_LR_SYNCWORD,read_sync,2)) ||
       !hal(sx126x_read_register(NULL,SX126X_REG_IQ_POLARITY,&iq,1)) ||
       !hal(sx126x_read_register(NULL,SX126X_REG_TX_MODULATION,&txmod,1))) return RP_RADIO_ERROR;
    if(type!=SX126X_PKT_TYPE_LORA || read_sync[0]!=sync[0] || read_sync[1]!=sync[1] ||
       ((iq>>2)&1u)!=(rc_pending.iq?0u:1u) || ((txmod>>2)&1u)!=(rc_pending.bandwidth==500000?0u:1u)) return RP_READBACK;
    rc_active=rc_pending;rc_active_valid=true;dirty=false;applied_mask=readback_mask=15;return RP_OK;
}
/* Capture the first failure before cleanup can overwrite the HAL error. */
static void xosc_failure(uint8_t *out,uint8_t phase,uint8_t error) {
    if(out[0]==RP_OK) {out[0]=error;out[12]=phase;out[13]=(uint8_t)radio_link.error;}
}
static uint16_t test_xosc(uint8_t *out) {
    for(unsigned i=0;i<18;++i) out[i]=0;
    out[3]=out[4]=out[5]=0xff;
    rp_put16(out+6,0xffff);rp_put16(out+8,0xffff);rp_put16(out+10,0xffff);
    uint32_t start=radio_io_millis();uint16_t flags=0;
    struct rf_switch_report pins;
    if(rf_switch_inspect(&pins)!=RF_SWITCH_OK) {xosc_failure(out,10,RP_READBACK);out[13]=RADIO_LINK_OK;goto cleanup;}
    if(!hal(sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC))) {xosc_failure(out,1,RP_RADIO_ERROR);goto cleanup;}
    if(!mode_status(SX126X_CHIP_MODE_STBY_RC,out+3)) {xosc_failure(out,2,RP_RADIO_ERROR);goto cleanup;}
    flags|=1;
    if(!errors()) {xosc_failure(out,3,RP_RADIO_ERROR);goto cleanup;}
    rp_put16(out+6,device_errors);
    if(device_errors) {xosc_failure(out,3,RP_RADIO_ERROR);goto cleanup;}
    flags|=2;
    if(!hal(sx126x_set_standby(NULL,SX126X_STANDBY_CFG_XOSC))) {xosc_failure(out,4,RP_RADIO_ERROR);goto cleanup;}
    flags|=4;
    if(!mode_status(SX126X_CHIP_MODE_STBY_XOSC,out+4)) {
        xosc_failure(out,5,radio_link.error==RADIO_LINK_OK?RP_READBACK:RP_RADIO_ERROR);goto cleanup;
    }
    flags|=8;
    if(!errors()) {xosc_failure(out,6,RP_RADIO_ERROR);goto cleanup;}
    rp_put16(out+8,device_errors);
    if(device_errors) {xosc_failure(out,6,RP_RADIO_ERROR);goto cleanup;}
    flags|=16;
cleanup:
    /* Always bounded; no reset, retry, ClearDeviceErrors or GPIO transition. */
    if(hal(sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC))) flags|=32;
    else xosc_failure(out,7,RP_RADIO_ERROR);
    if(mode_status(SX126X_CHIP_MODE_STBY_RC,out+5)) flags|=64;
    else xosc_failure(out,8,RP_RADIO_ERROR);
    if(errors()) {
        rp_put16(out+10,device_errors);
        if(!device_errors) flags|=128;
        else xosc_failure(out,9,RP_RADIO_ERROR);
    } else xosc_failure(out,9,RP_RADIO_ERROR);
    raw_status=out[5];rp_put16(out+1,flags);rp_put32(out+14,radio_io_millis()-start);
    if(out[0]!=RP_OK) {rc_active_valid=false;applied_mask=readback_mask=0;dirty=true;++rc_generation;}
    return 18;
}
uint16_t rc_handle(uint8_t cmd,const uint8_t *p,uint16_t n,uint8_t *out) {
    out[0]=RP_OK;
    struct rc_config candidate=rc_pending;
    switch(cmd) {
    case RP_PING:
        if(n) break;
        out[1]='P';out[2]='O';out[3]='N';out[4]='G';return 5;
    case RP_GET_VERSION:
        if(n) break;
        out[1]=RP_FW_MAJOR;out[2]=RP_FW_MINOR;out[3]=RP_FW_PATCH;out[4]=RP_VERSION;return 5;
    case RP_GET_CAPABILITIES:
        if(n) break;
        /* schema, features, max packet/payload, hardware band, allowed policy band,
         * hardware/policy power, duty percent, bandwidth table, SF/CR masks. */
        out[1]=1;rp_put32(out+2,0x7fu);rp_put16(out+6,255);rp_put16(out+8,RP_MAX_PAYLOAD);
        rp_put32(out+10,RC_MODULE_MIN);rp_put32(out+14,RC_MODULE_MAX);
        rp_put32(out+18,RC_POLICY_MIN);rp_put32(out+22,RC_POLICY_MAX);
        out[26]=22;out[27]=RC_POWER_MAX;out[28]=10;out[29]=10;
        for(unsigned i=0;i<10;++i) rp_put32(out+30+4*i,bandwidths[i]);
        out[70]=5;out[71]=12;out[72]=5;out[73]=8;rp_put16(out+74,15);return 76;
    case RP_GET_STATUS:
        if(n) break;
        if(!status() || !errors()) out[0]=RP_RADIO_ERROR;
        out[1]=((raw_status & 0xf1u)==0x20u)?0:255;out[2]=raw_status;rp_put16(out+3,device_errors);out[5]=rc_active_valid;out[6]=dirty;
        rp_put16(out+7,applied_mask);rp_put16(out+9,readback_mask);rp_put32(out+11,rc_generation);
        rp_put32(out+15,radio_io_millis());rp_put32(out+19,radio_link.busy_timeouts);
        rp_put32(out+23,radio_link.spi_failures);out[27]=(uint8_t)radio_link.error;return 28;
    case RP_GET_CONFIG:
        if(n) break;
        encode(out+1,&rc_pending);encode(out+21,&rc_active);out[41]=rc_active_valid;out[42]=dirty;
        rp_put16(out+43,applied_mask);rp_put16(out+45,readback_mask);rp_put32(out+47,rc_generation);return 51;
    case RP_SET_FREQUENCY:
        if(n!=4) break;
        candidate.frequency=rp_u32(p);
        if(candidate.frequency<RC_MODULE_MIN || candidate.frequency>RC_MODULE_MAX) {out[0]=RP_UNSUPPORTED;return 1;}
        if(candidate.frequency<RC_POLICY_MIN || candidate.frequency>RC_POLICY_MAX) {out[0]=RP_RANGE;return 1;}
        goto stage;
    case RP_SET_LORA:
        if(n!=7) break;
        candidate.sf=p[0];candidate.bandwidth=rp_u32(p+1);candidate.cr=p[5];candidate.ldro=p[6];
        if(candidate.sf<5 || candidate.sf>12 || bandwidth_code(candidate.bandwidth)<0 || candidate.cr<5 || candidate.cr>8 || candidate.ldro>1) {out[0]=RP_RANGE;return 1;}
        goto stage;
    case RP_SET_PACKET:
        if(n!=8) break;
        candidate.preamble=rp_u16(p);candidate.sync=rp_u16(p+2);candidate.header=p[4];candidate.crc=p[5];candidate.iq=p[6];candidate.payload=p[7];
        if(candidate.preamble<8 || candidate.header>1 || candidate.crc>1 || candidate.iq>1 || !candidate.payload) {out[0]=RP_RANGE;return 1;}
        goto stage;
    case RP_SET_POWER:
        if(n!=1) break;
        candidate.power=(int8_t)p[0];
        if(candidate.power < -9 || candidate.power>RC_POWER_MAX) {out[0]=RP_RANGE;return 1;}
        goto stage;
    case RP_APPLY:
        if(n) break;
        out[0]=apply();rp_put16(out+1,applied_mask);rp_put16(out+3,readback_mask);rp_put32(out+5,rc_generation);return 9;
    case RP_START_RX: case RP_SEND:
        /* No path to RF activation exists, even for arbitrary/oversize payloads. */
        out[0]=RP_DISABLED;return 1;
    case RP_STOP_RX:
        if(n) break;
        out[0]=hal(sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC)) && status()?RP_OK:RP_RADIO_ERROR;return 1;
    case RP_SET_MODE:
        if(n!=1) break;
        if(p[0]!=0) {out[0]=RP_DISABLED;return 1;}
        out[0]=hal(sx126x_set_standby(NULL,SX126X_STANDBY_CFG_RC)) && status()?RP_OK:RP_RADIO_ERROR;return 1;
    case RP_GET_ERRORS:
        if(n) break;
        out[0]=errors()?RP_OK:RP_RADIO_ERROR;rp_put16(out+1,device_errors);return 3;
    case RP_GET_READBACK: {
        if(n) break;
        sx126x_pkt_type_t type;
        uint8_t sync[2],iq,txmod;
        if(!hal(sx126x_get_pkt_type(NULL,&type)) ||
           !hal(sx126x_read_register(NULL,SX126X_REG_LR_SYNCWORD,sync,2)) ||
           !hal(sx126x_read_register(NULL,SX126X_REG_IQ_POLARITY,&iq,1)) ||
           !hal(sx126x_read_register(NULL,SX126X_REG_TX_MODULATION,&txmod,1))) {
            out[0]=RP_RADIO_ERROR;return 1;
        }
        out[1]=(uint8_t)type;out[2]=sync[1];out[3]=sync[0];out[4]=iq;out[5]=txmod;return 6;
    }
    case RP_TEST_XOSC:
        if(n) break;
        return test_xosc(out);
    case RP_GET_RF_SWITCH: {
        if(n) break;
        struct rf_switch_report pins;
        out[0]=rf_switch_inspect(&pins)==RF_SWITCH_OK?RP_OK:RP_READBACK;
        out[1]=(uint8_t)pins.state;out[2]=(uint8_t)pins.error;out[3]=0;
        out[4]=(uint8_t)(pins.gpio.direction&RF_SWITCH_PINS);out[5]=(uint8_t)(pins.gpio.type&RF_SWITCH_PINS);
        rp_put16(out+6,(uint16_t)(pins.gpio.pulls&RF_SWITCH_PULLS));rp_put32(out+8,pins.gpio.alternate&RF_SWITCH_AF);
        out[12]=(uint8_t)(pins.gpio.input&RF_SWITCH_PINS);out[13]=(uint8_t)(pins.gpio.latch&RF_SWITCH_PINS);return 14;
    }
    case RP_RESET:
        if(n) break;
        out[0]=reset();return 1;
    default: out[0]=RP_UNKNOWN;return 1;
    }
    out[0]=RP_LENGTH;return 1;
stage:
    rc_pending=candidate;dirty=true;return 1;
}
