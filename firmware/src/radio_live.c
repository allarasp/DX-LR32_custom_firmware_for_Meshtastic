#include "radio_live.h"
#include "radio_diag.h"
#include "rf_switch.h"
#include "sx126x.h"
#include "sx126x_hal.h"
static live_emit_fn emit;
static uint8_t event_buffer[270];
static unsigned mode;
static uint32_t stats[9];
static bool have_tx;
static uint16_t previous_seq,previous_crc,previous_len;
static bool armed,rx_again,duty_active;
static uint32_t arm_at,start,limit,next_tx,last_poll,rx_timeout;
static uint16_t tx_seq,rx_seq;
static bool ok(sx126x_status_t s) {return s==SX126X_STATUS_OK;}
static bool clear_irq(void) {return ok(sx126x_clear_irq_status(0,0x03ff));}
static bool packet_limit(uint8_t length) {
 sx126x_pkt_params_lora_t p={rc_active.preamble,(sx126x_lora_pkt_len_modes_t)rc_active.header,length,rc_active.crc!=0,rc_active.iq!=0};
 return ok(sx126x_set_lora_pkt_params(0,&p));
}
static bool stop(void) {
 bool a=ok(sx126x_set_standby(0,SX126X_STANDBY_CFG_RC));
 if(!a) {(void)sx126x_reset(0);rc_invalidate();}
 bool b=live_pads(0);mode=b?0:255;
 if(!a||!b) {++stats[7];armed=false;rc_invalidate();}return a&&b;
}
static void error(uint8_t origin) {uint8_t p[2]={origin,RP_RADIO_ERROR};emit(0x83,origin==RP_SEND?tx_seq:0,p,2);}
void live_link_fault(void) {armed=false;rx_again=false;if(mode==4)++stats[6];(void)stop();rc_invalidate();}
static bool prepare(void) {
 uint32_t lower,upper;
 if(!rc_active_valid || rc_frequency_bounds(rc_active.frequency,&lower,&upper)!=RP_OK)return false;
 uint8_t cal_low=lower==RC_BAND868_MIN?0xd7:0x6b;
 uint8_t cal_high=lower==RC_BAND868_MIN?0xdb:0x6f;
 return ok(sx126x_set_standby(0,SX126X_STANDBY_CFG_RC)) &&
 ok(sx126x_cal_img(0,cal_low,cal_high)) && ok(sx126x_set_rx_tx_fallback_mode(0,SX126X_FALLBACK_STDBY_RC)) &&
 ok(sx126x_set_buffer_base_address(0,0,0)) && ok(sx126x_set_dio_irq_params(0,0x03ff,0x0203,0,0)) && clear_irq();
}
static bool receive(void) {
 if(!live_pads(1)||!packet_limit(rc_active.payload)||!clear_irq())return false;
 bool a=rx_timeout?ok(sx126x_set_rx(0,rx_timeout)):ok(sx126x_set_rx_with_timeout_in_rtc_step(0,SX126X_RX_CONTINUOUS));
 if(a) {mode=3;start=radio_io_millis();}return a;
}
void live_init(live_emit_fn fn) {emit=fn;mode=0;armed=rx_again=duty_active=false;rx_seq=0;last_poll=rx_timeout=0;have_tx=false;
 for(unsigned i=0;i<9;++i)stats[i]=0;
 if(!live_pads(0)) {mode=255;++stats[7];rc_invalidate();}}
static void complete(uint8_t status) {
 uint32_t elapsed=radio_io_millis()-start;
 bool again=rx_again;
 if(!stop())status=RP_RADIO_ERROR;
 uint8_t p[9]={status};rp_put32(p+1,elapsed);rp_put32(p+5,rc_generation);
 emit(0x82,tx_seq,p,9);
 if(status!=RP_OK) {++stats[6];error(RP_SEND);}else ++stats[5];
 if(status==RP_OK && again && rc_active_valid && !receive()) {(void)stop();error(RP_START_RX);}
}
void live_poll(void) {
 uint32_t now=radio_io_millis();
 if(armed && (uint32_t)(now-arm_at)>30000)armed=false;
 if(!mode || mode==255 || (uint32_t)(now-last_poll)<3)return;
 last_poll=now;sx126x_irq_mask_t irq=0;
 if(!ok(sx126x_get_irq_status(0,&irq))) {if(mode==3)++stats[2];if(mode==4)complete(RP_RADIO_ERROR);else {(void)stop();error(RP_START_RX);}return;}
 if(mode==4) {
  if(irq&SX126X_IRQ_TX_DONE)complete(RP_OK);
  else if((irq&SX126X_IRQ_TIMEOUT) || (uint32_t)(now-start)>limit+200)complete(RP_RADIO_ERROR);
  return;
 }
 if(irq&SX126X_IRQ_RX_DONE) {
  bool valid=!(irq&(SX126X_IRQ_CRC_ERROR|SX126X_IRQ_HEADER_ERROR));
  bool rejected=!valid;
  sx126x_rx_buffer_status_t b={0};uint8_t raw[3];const uint8_t command[2]={0x14,0};
  if(!ok(sx126x_set_standby(0,SX126X_STANDBY_CFG_RC)))valid=false;
  if(valid)valid=ok(sx126x_get_rx_buffer_status(0,&b)) && b.pld_len_in_bytes>0 &&
   sx126x_hal_read(0,command,2,raw,3)==SX126X_HAL_STATUS_OK &&
   ok(sx126x_read_buffer(0,b.buffer_start_pointer,event_buffer+15,b.pld_len_in_bytes));
  if(valid) {
   rp_put32(event_buffer,rc_generation);rp_put32(event_buffer+4,now);
   rp_put16(event_buffer+8,(uint16_t)(int16_t)(-2*(int)raw[0]));event_buffer[10]=raw[1];
   rp_put16(event_buffer+11,irq);event_buffer[13]=rc_active.crc?15:14;event_buffer[14]=b.pld_len_in_bytes;
  }
  if(!receive()) {(void)stop();error(RP_START_RX);}
  if(valid) {++stats[0];emit(0x81,++rx_seq,event_buffer,(uint16_t)(15+b.pld_len_in_bytes));}
  else if(rejected)++stats[1];
  else {++stats[2];error(RP_START_RX);}
  return;
 }
 if(irq&SX126X_IRQ_TIMEOUT) {++stats[3];(void)stop();uint8_t p[1]={RP_TIMEOUT};emit(0x84,0,p,1);return;}
 if(irq&(SX126X_IRQ_CRC_ERROR|SX126X_IRQ_HEADER_ERROR)) {
  ++stats[1];
  if(!receive()) {(void)stop();error(RP_START_RX);}
 }
 if(mode==3 && rx_timeout && (uint32_t)(now-start)>rx_timeout+200) {++stats[3];(void)stop();error(RP_START_RX);}
}
uint16_t live_handle(uint8_t cmd,const uint8_t *p,uint16_t n,uint8_t *out,uint16_t seq) {
 out[0]=RP_OK;
 if(cmd==RP_GET_VERSION) {if(n)goto length;out[1]=RP_FW_MAJOR;out[2]=RP_FW_MINOR;out[3]=RP_FW_PATCH;out[4]=RP_VERSION;return 5;}
 if(cmd==RP_GET_FREQUENCY_RANGES) {
  if(n)goto length;
  out[1]=1;out[2]=2;
  rp_put32(out+3,RC_POLICY_MIN);rp_put32(out+7,RC_POLICY_MAX);
  rp_put32(out+11,RC_BAND868_MIN);rp_put32(out+15,RC_BAND868_MAX);return 19;
 }
 if(cmd==RP_GET_RADIO_STATS) {
  if(n)goto length;
  for(unsigned i=0;i<9;++i)rp_put32(out+1+4*i,stats[i]);
  return 37;
 }
 if(cmd==RP_ARM_TX) { /* ARM_TX: explicit one-shot token, expires in 30 seconds. */
  if(n!=4)goto length;
  if(rp_u32(p)!=0x21315854u) {out[0]=RP_RANGE;return 1;}
  armed=true;arm_at=radio_io_millis();return 1;
 }
 if(cmd==RP_GET_STATUS) {
  if(n)goto length;
  sx126x_chip_status_t s;sx126x_errors_mask_t e=0xffff;
  bool a=ok(sx126x_get_status(0,&s))&&ok(sx126x_get_device_errors(0,&e));
  (void)rc_handle(RP_GET_CONFIG,0,0,out);uint8_t is_dirty=out[42];
  for(unsigned i=0;i<28;++i)out[i]=0;
  out[0]=a?RP_OK:RP_RADIO_ERROR;out[1]=(uint8_t)mode;out[2]=a?radio_link.rx[1]:0xff;
  /* The HAL trace was overwritten by GetDeviceErrors; reconstruct status. */
  if(a)out[2]=(uint8_t)(((unsigned)s.chip_mode<<4)|((unsigned)s.cmd_status<<1));
  rp_put16(out+3,e);out[5]=rc_active_valid;out[6]=is_dirty;
  rp_put16(out+7,rc_active_valid?15:0);rp_put16(out+9,rc_active_valid?15:0);rp_put32(out+11,rc_generation);
  rp_put32(out+15,radio_io_millis());rp_put32(out+19,radio_link.busy_timeouts);rp_put32(out+23,radio_link.spi_failures);out[27]=(uint8_t)radio_link.error;return 28;
 }
 if(cmd==RP_GET_RF_SWITCH) {
  if(n)goto length;
  struct rf_switch_gpio g;rf_switch_read_gpio(&g);
  bool valid=(mode==0||mode==3||mode==4)&&!(g.pulls&RF_SWITCH_PULLS)&&!(g.alternate&RF_SWITCH_AF)&&!(g.type&RF_SWITCH_PINS)&&
   ((g.direction&24)==(mode?24u:0u))&&(!mode||((g.input&24)==(mode==4?16u:8u)&&(g.latch&24)==(mode==4?16u:8u)));
  out[0]=valid?RP_OK:RP_READBACK;out[1]=valid?(mode==4?RF_SWITCH_TX:mode==3?RF_SWITCH_RX:RF_SWITCH_UNDRIVEN):RF_SWITCH_UNKNOWN;
  out[2]=valid?0:RF_SWITCH_BAD_GPIO;out[3]=1;out[4]=g.direction&24;out[5]=g.type&24;
  rp_put16(out+6,g.pulls&RF_SWITCH_PULLS);rp_put32(out+8,g.alternate&RF_SWITCH_AF);out[12]=g.input&24;out[13]=g.latch&24;return 14;
 }
 if(cmd==RP_START_RX) {
  if(n!=4)goto length;
  if((mode!=0 && mode!=3) || !rc_active_valid)goto not_ready;
  uint32_t t=rp_u32(p);if(t>262143)goto range;
  rx_timeout=t;rx_again=true;
  if(!prepare()||!receive()) {out[0]=RP_RADIO_ERROR;(void)stop();}return 1;
 }
 if(cmd==RP_STOP_RX || (cmd==RP_SET_MODE && n==1 && p[0]==0)) {
  if(cmd==RP_STOP_RX && n)goto length;
  if(mode==4)goto not_ready;
  rx_again=false;out[0]=stop()?RP_OK:RP_RADIO_ERROR;return 1;
 }
 if(cmd==RP_SEND) {
  if(!n || n>255)goto length;
  uint16_t crc=0xffff;for(unsigned i=0;i<n;++i)crc=rp_crc_byte(crc,p[i]);
  if(have_tx && seq==previous_seq) {++stats[8];out[0]=(n==previous_len&&crc==previous_crc)?RP_DUPLICATE:RP_COMBINATION;return 1;}
  uint32_t now=radio_io_millis();
  if(!armed || now-arm_at>30000) {armed=false;out[0]=RP_DISABLED;return 1;}
  if((mode!=0 && mode!=3) || !rc_active_valid || (duty_active && (int32_t)(now-next_tx)<0))goto not_ready;
  sx126x_irq_mask_t irq=0;
  if(mode==3 && (!ok(sx126x_get_irq_status(0,&irq)) || (irq&0x001e)))goto not_ready;
  if(rc_active.header && n!=rc_active.payload) {out[0]=RP_COMBINATION;return 1;}
  int bw=rc_bandwidth_code(rc_active.bandwidth);
  if(bw<0 || rc_active.power < -9 || rc_active.power>RC_POWER_MAX)goto range;
  const sx126x_mod_params_lora_t m={(sx126x_lora_sf_t)rc_active.sf,(sx126x_lora_bw_t)bw,
      (sx126x_lora_cr_t)(rc_active.cr-4),rc_active.ldro};
  const sx126x_pkt_params_lora_t pkt={rc_active.preamble,(sx126x_lora_pkt_len_modes_t)rc_active.header,n,rc_active.crc!=0,rc_active.iq!=0};
  uint32_t airtime=sx126x_get_lora_time_on_air_in_ms(&pkt,&m);
  if(airtime>2000)goto range;
  bool again=mode==3;armed=false;tx_seq=seq;rx_again=again;
  have_tx=true;previous_seq=seq;previous_crc=crc;previous_len=n;++stats[4];
  /* Charge a conservative full 3-second attempt before any RF command. */
  next_tx=now+30000;duty_active=true;limit=airtime+100;
  /* Nominal SX1262 full-range PA, datasheet table 13-21. No regional cap. */
  const sx126x_pa_cfg_params_t pa={4,7,0,1};
  if(!prepare() || !live_pads(1) || !packet_limit((uint8_t)n) || !ok(sx126x_cfg_tx_clamp(0)) ||
   !ok(sx126x_set_pa_cfg(0,&pa)) || !ok(sx126x_set_tx_params(0,rc_active.power,SX126X_RAMP_200_US)) ||
   !ok(sx126x_write_buffer(0,0,p,(uint8_t)n)) || !live_pads(2))goto radio_error;
  start=radio_io_millis();
  if(!ok(sx126x_set_tx(0,limit)))goto radio_error;
  mode=4;return 1;
 }
 if(mode && (cmd==RP_APPLY || cmd==RP_RESET || cmd==RP_TEST_XOSC))goto not_ready;
 return 0xffff; /* Dispatch unchanged commands to the v0.3.x controller. */
length: out[0]=RP_LENGTH;return 1;
range: out[0]=RP_RANGE;return 1;
not_ready: out[0]=RP_NOT_READY;return 1;
radio_error: ++stats[6];out[0]=RP_RADIO_ERROR;(void)stop();return 1;
}
