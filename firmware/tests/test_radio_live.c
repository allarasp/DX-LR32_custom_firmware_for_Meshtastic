#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "radio_live.h"
#include "radio_diag.h"
#include "rf_switch.h"
#include "sx126x_hal.h"
#include "radio_phy.h"
struct rc_config rc_pending,rc_active;
bool rc_active_valid;
uint32_t rc_generation;
struct radio_link_trace radio_link;
void rc_invalidate(void) {rc_active_valid=false;++rc_generation;}
static unsigned now,chip,irq,pad,starts,events,resets,wakes,reads;
static uint8_t regs[0xa00],packet_type;
static uint8_t last_event,last[270],rx_len=32;
static uint16_t event_seq;
static bool fault,read_fault,pad_fault;
static uint16_t request_seq;
static uint8_t transmitted_power,packet_header,packet_crc,packet_iq;
int rc_bandwidth_code(uint32_t b) {return b==250000?5:b==125000?4:-1;}
uint32_t radio_io_millis(void) {return now;}
bool radio_io_dio1(void) {return irq!=0;}
bool radio_io_busy(void) {return chip==0;}
bool live_pads(unsigned s) {if(pad_fault)return false;pad=s;return true;}
void rf_switch_read_gpio(struct rf_switch_gpio *g) {*g=(struct rf_switch_gpio){0};g->direction=pad?24:0;g->input=g->latch=pad==2?16:pad==1?8:0;}
uint16_t rc_handle(uint8_t c,const uint8_t *p,uint16_t n,uint8_t *out) {(void)c;(void)p;(void)n;memset(out,0,51);return 51;}
static void emit(uint8_t e,uint16_t seq,const uint8_t *p,uint16_t n) {assert(n<=270);last_event=e;event_seq=seq;memcpy(last,p,n);++events;}
sx126x_hal_status_t sx126x_hal_reset(const void *c) {(void)c;chip=2;++resets;return SX126X_HAL_STATUS_OK;}
sx126x_hal_status_t sx126x_hal_wakeup(const void *c) {(void)c;if(fault)return SX126X_HAL_STATUS_ERROR;chip=2;++wakes;return SX126X_HAL_STATUS_OK;}
sx126x_hal_status_t sx126x_hal_write(const void *c,const uint8_t *b,uint16_t n,const uint8_t *p,uint16_t size) {
 (void)c;(void)n;(void)p;(void)size;if(fault)return SX126X_HAL_STATUS_ERROR;
 if(b[0]==0x0d) {unsigned a=(unsigned)b[1]<<8|b[2];assert(a+size<=sizeof(regs));memcpy(regs+a,p,size);}
 switch(b[0]) {
 case 0x8a:packet_type=b[1];break;
 case 0x84:chip=0;break;
 case 0xc5:chip=4;break;
 case 0x94:chip=5;break;
 case 0x80:chip=2;break;
 case 0x98:assert(b[1]==(rc_active.frequency>=863000000?0xd7:0x6b));assert(b[2]==(rc_active.frequency>=863000000?0xdb:0x6f));break;
 case 0x82:chip=5;break;
 case 0x83:chip=6;++starts;assert(pad==2);break;
 case 0x02:irq=0;break;
 case 0x95:assert(b[1]==4&&b[2]==7&&b[3]==0&&b[4]==1);break;
 case 0x8e:transmitted_power=b[1];assert((int8_t)b[1]==rc_active.power);break;
 case 0x8c:packet_header=b[3];packet_crc=b[5];packet_iq=b[6];break;
 case 0x97:case 0x9d:assert(!"DIO2/DIO3 must remain disabled");
 default:break;
 }return SX126X_HAL_STATUS_OK;
}
sx126x_hal_status_t sx126x_hal_read(const void *c,const uint8_t *b,uint16_t n,uint8_t *p,uint16_t size) {
 (void)c;(void)n;if(fault || (read_fault&&b[0]==0x1e))return SX126X_HAL_STATUS_ERROR;memset(p,0,size);++reads;
 switch(b[0]) {
 case 0x1d:{unsigned a=(unsigned)b[1]<<8|b[2];assert(a+size<=sizeof(regs));memcpy(p,regs+a,size);break;}
 case 0x11:p[0]=packet_type;break;
 case 0x15:p[0]=180;break;
 case 0x10:p[1]=7;p[3]=2;p[5]=1;break;
 case 0xc0:p[0]=(chip<<4)|2;break;
 case 0x12:p[0]=irq>>8;p[1]=irq;break;
 case 0x13:p[0]=rx_len;break;
 case 0x14:p[0]=phy_active_type?190:2;p[1]=phy_active_type?0xf9:190;p[2]=194;break;
 case 0x1e:for(unsigned i=0;i<size;++i)p[i]=(uint8_t)i;break;
 default:break;
 }return SX126X_HAL_STATUS_OK;
}
static uint8_t command(unsigned c,const uint8_t *p,unsigned n) {uint8_t out[80];assert(live_handle(c,p,n,out,++request_seq)!=0xffff);return out[0];}
static void poll(void) {now+=10;live_poll();}
static void setup(void) {
 now+=40000;chip=2;irq=starts=events=resets=0;fault=read_fault=pad_fault=false;rx_len=32;request_seq=0;wakes=reads=0;packet_type=1;memset(regs,0,sizeof(regs));phy_init();
 rc_active=(struct rc_config){433875000,250000,16,0x24b4,11,5,0,0,1,0,255,-9};rc_active_valid=true;rc_generation=5;
 live_init(emit);
}
int main(void) {
 uint8_t zero[4]={0},arm[4]={'T','X','1','!'},packet[64]={0};
 setup();assert(command(RP_SEND,packet,32)==RP_DISABLED);
 assert(command(RP_START_RX,zero,0)==RP_LENGTH);
 assert(command(RP_START_RX,zero,4)==RP_OK&&pad==1&&chip==5);
 irq=2;poll();assert(last_event==0x81&&last[14]==32&&rp_u16(last+8)==(uint16_t)-380&&last[10]==0xf9&&pad==1&&chip==5);
 rx_len=255;irq=2;poll();assert(last[14]==255&&last[269]==254);
 unsigned before=events;irq=0x42;poll();assert(events==before&&chip==5);
 assert(command(RP_STOP_RX,0,0)==RP_OK&&pad==0&&chip==2);
 assert(command(21,arm,4)==RP_OK);assert(command(RP_START_RX,zero,4)==RP_OK);
 assert(command(RP_SEND,packet,32)==RP_OK&&starts==1&&chip==6);
 uint16_t sent_seq=request_seq;
 assert(command(RP_RESET,0,0)==RP_NOT_READY);
 irq=1;poll();assert(last_event==0x82&&event_seq==sent_seq&&last[0]==RP_OK&&chip==5&&pad==1);
 assert(command(RP_SEND,packet,32)==RP_DISABLED);
 assert(command(21,arm,4)==RP_OK);assert(command(RP_SEND,packet,32)==RP_NOT_READY); /* duty */
 now+=40000;assert(command(21,arm,4)==RP_OK);assert(command(RP_SEND,packet,32)==RP_OK);
 now+=3000;irq=0;poll();assert(chip==2&&pad==0); /* TX watchdog stops; explicit host recovery */
 setup();assert(command(21,arm,4)==RP_OK);now+=30001;assert(command(RP_SEND,packet,32)==RP_DISABLED);
 setup();assert(command(21,arm,4)==RP_OK);rc_active.sf=7;rc_active.bandwidth=125000;
 rc_active.cr=8;rc_active.sync=0x1424;rc_active.header=1;rc_active.payload=32;rc_active.crc=0;rc_active.iq=1;rc_active.power=10;
 assert(command(RP_SEND,packet,31)==RP_COMBINATION&&!starts);
 assert(command(RP_SEND,packet,32)==RP_OK&&starts==1&&transmitted_power==10&&packet_header==1&&!packet_crc&&packet_iq==1);
 uint8_t out[80];assert(live_handle(RP_SEND,packet,32,out,request_seq)==1&&out[0]==RP_DUPLICATE&&starts==1);
 setup();rc_active.sf=12;rc_active.bandwidth=125000;rc_active.ldro=1;
 assert(command(21,arm,4)==RP_OK);assert(command(RP_SEND,packet,64)==RP_RANGE&&!starts);
 setup();assert(command(RP_START_RX,zero,4)==RP_OK);read_fault=true;irq=2;poll();
 assert(last_event==0x83&&last[0]==RP_START_RX&&chip==5);
 assert(live_handle(RP_GET_RADIO_STATS,0,0,out,0)==37&&rp_u32(out+9)==1);
 setup();assert(command(RP_START_RX,zero,4)==RP_OK);irq=0x200;poll();assert(last_event==0x84&&last[0]==RP_TIMEOUT&&pad==0);
 setup();pad_fault=true;assert(command(RP_STOP_RX,0,0)==RP_RADIO_ERROR&&!rc_active_valid);
 assert(command(RP_START_RX,zero,4)==RP_NOT_READY);
 setup();assert(command(RP_START_RX,zero,4)==RP_OK);live_link_fault();assert(chip==2&&pad==0&&!rc_active_valid);
 assert(live_handle(RP_GET_VERSION,0,0,out,0)==5&&out[2]==7&&out[3]==0);
 setup();assert(command(21,arm,4)==RP_OK);assert(command(RP_START_RX,zero,4)==RP_OK);irq=4;assert(command(RP_SEND,packet,32)==RP_NOT_READY&&!starts);
 setup();fault=true;assert(command(RP_START_RX,zero,4)==RP_RADIO_ERROR&&pad==0&&resets);
 setup();rc_active_valid=false;assert(command(RP_START_RX,zero,4)==RP_NOT_READY);
 for(int power=-9;power<=22;++power) {
  setup();rc_active.power=(int8_t)power;assert(command(RP_ARM_TX,arm,4)==RP_OK);
  assert(command(RP_SEND,packet,32)==RP_OK&&starts==1&&(int8_t)transmitted_power==power);
 }
 setup();rc_active.power=23;assert(command(RP_ARM_TX,arm,4)==RP_OK);assert(command(RP_SEND,packet,32)==RP_RANGE&&!starts);
 setup();assert(live_handle(RP_GET_FREQUENCY_RANGES,0,0,out,0)==19&&out[1]==1&&out[2]==2&&rp_u32(out+11)==863000000&&rp_u32(out+15)==870000000);
 assert(command(RP_START_RX,zero,4)==RP_OK);assert(command(RP_STOP_RX,0,0)==RP_OK);
 rc_active.frequency=868000000;assert(command(RP_START_RX,zero,4)==RP_OK);assert(command(RP_STOP_RX,0,0)==RP_OK);
 assert(command(RP_ARM_TX,arm,4)==RP_OK);assert(command(RP_SEND,packet,32)==RP_OK&&starts==1);
 setup();rc_active.frequency=868000000;assert(command(RP_START_RX,zero,4)==RP_OK);assert(command(RP_STOP_RX,0,0)==RP_OK);
 rc_active.frequency=433875000;assert(command(RP_START_RX,zero,4)==RP_OK);
 /* New stateful commands, exact fractional RSSI, no hidden SPI while asleep/sniffing. */
 setup();assert(command(RP_GET_RSSI,0,0)==RP_NOT_READY);
 uint8_t enable=1;assert(command(RP_SET_RX_GAIN,&enable,1)==RP_OK&&regs[0x8ac]==0x96);
 assert(command(RP_START_RX,zero,4)==RP_OK);
 assert(live_handle(RP_GET_RSSI,0,0,out,0)==8&&out[0]==RP_OK&&(int16_t)rp_u16(out+1)==-360);
 assert(command(RP_SLEEP,0,0)==RP_NOT_READY&&command(RP_SET_RX_GAIN,&enable,1)==RP_NOT_READY);
 assert(live_handle(RP_GET_PHY_STATS,0,0,out,0)==8&&rp_u16(out+2)==7&&rp_u16(out+4)==2);
 assert(command(RP_RESET_PHY_STATS,0,0)==RP_OK);assert(command(RP_STOP_RX,0,0)==RP_OK);
 assert(command(RP_SLEEP,0,0)==RP_OK&&chip==0&&!rc_active_valid&&pad==0);
 unsigned snapshot=reads;poll();assert(reads==snapshot);
 assert(live_handle(RP_GET_STATUS,0,0,out,0)==28&&out[1]==1&&out[2]==255&&rp_u16(out+3)==65535&&reads==snapshot);
 assert(command(RP_GET_ERRORS,0,0)==RP_NOT_READY&&reads==snapshot);
 assert(command(RP_WAKE,0,0)==RP_OK&&chip==2&&wakes==1);
 assert(command(RP_START_RX,zero,4)==RP_NOT_READY);
 setup();uint8_t cad[7]={2,22,10};rp_put32(cad+3,1000);
 assert(command(RP_START_CAD,cad,7)==RP_OK&&pad==1);
 assert(command(RP_START_RX,zero,4)==RP_NOT_READY&&command(RP_APPLY,0,0)==RP_NOT_READY);
 irq=0x180;poll();assert(last_event==0x85&&last[0]==0&&last[1]==1&&pad==0);
 setup();assert(command(RP_START_CAD,cad,7)==RP_OK);now+=1001;poll();assert(last_event==0x83&&pad==0);
 setup();assert(command(RP_START_CAD,cad,7)==RP_OK);before=events;
 assert(command(RP_STOP_RX,0,0)==RP_OK&&events==before+1&&last_event==0x85&&last[0]==RP_DISABLED&&pad==0);
 setup();uint8_t duty[12];rp_put32(duty,100);rp_put32(duty+4,100);rp_put32(duty+8,500);
 assert(command(RP_START_DUTY_RX,duty,12)==RP_OK&&pad==1);
 snapshot=reads;poll();assert(reads==snapshot);
 assert(live_handle(RP_GET_STATUS,0,0,out,0)==28&&out[1]==5&&out[2]==255&&reads==snapshot);
 assert(command(RP_GET_PHY_STATS,0,0)==RP_NOT_READY&&reads==snapshot);
 irq=2;poll();assert(last_event==0x81&&pad==1);
 irq=0;now+=501;poll();assert(last_event==0x84&&pad==0&&wakes==1);
 setup();uint8_t wire[PHY_GFSK_WIRE_SIZE];phy_encode(wire,&phy_pending);
 assert(phy_stage(wire)==RP_OK&&phy_validate(&rc_active)==RP_OK);
 packet_type=0;assert(phy_apply());phy_active_type=0;
 assert(phy_airtime(32)>0&&phy_packet_limit(32));
 assert(command(RP_START_CAD,cad,7)==RP_UNSUPPORTED);
 assert(command(RP_START_RX,zero,4)==RP_OK);irq=2;poll();
 assert(last_event==0x86&&last[14]==32&&(int16_t)rp_u16(last+8)==-388&&(int16_t)rp_u16(last+10)==-380);
 assert(command(RP_ARM_TX,arm,4)==RP_OK&&command(RP_SEND,packet,32)==RP_OK&&starts==1);
 wire[0]=wire[1]=wire[2]=wire[3]=0;assert(phy_stage(wire)==RP_RANGE); /* divide-by-zero rejected */
 puts("Live radio host tests passed: RX metadata/255 bytes, CRC discard, single-arm TX, duty, IRQ busy, watchdog, cleanup and recovery");
}
