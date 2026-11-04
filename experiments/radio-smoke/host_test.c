#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "sx126x_hal.h"
static struct result output;
volatile struct result *const result=&output;
static unsigned mode,started,starts,pad_state,resets;
static bool fail_cal,stuck,fail_gpio,fail_cleanup;
uint32_t now(void) {return result->ms;}
bool delay_ms(uint32_t n) {result->ms+=n;return true;}
bool pads(unsigned s) {pad_state=s;return !(fail_gpio&&s==2);}
void force_radio_reset(void) {++resets;mode=2;}
static unsigned irq(void) {
 if(stuck)return 0;
 if(starts && now()-started>=(TEST_TX?40u:500u)) {mode=2;return TEST_TX?1:512;}
 return 0;
}
sx126x_hal_status_t sx126x_hal_write(const void *ctx,const uint8_t *c,uint16_t n,const uint8_t *p,uint16_t size) {
 (void)ctx;
 switch(c[0]) {
 case 0x80: assert(n==2&&c[1]==0&&!size);if(fail_cleanup)return SX126X_HAL_STATUS_ERROR;mode=2;break;
 case 0x98: assert(n==3&&c[1]==0x6b&&c[2]==0x6f&&!size);if(fail_cal)return SX126X_HAL_STATUS_ERROR;break;
 case 0x93: assert(n==2&&c[1]==0x20);break;
 case 0x8f: assert(n==3&&!c[1]&&!c[2]);break;
 case 0x08: assert(n==9&&c[1]==3&&c[2]==255);for(unsigned i=3;i<9;++i)assert(!c[i]);break;
 case 0x02: assert(n==3&&c[1]==3&&c[2]==255);break;
 case 0x95: assert(TEST_TX&&n==5&&c[1]==2&&c[2]==2&&!c[3]&&c[4]==1);break;
 case 0x8e: assert(TEST_TX&&n==3&&c[1]==247&&c[2]==4);break;
 case 0x0d: assert(TEST_TX&&n==3&&c[1]==8&&c[2]==0xd8&&size==1&&p[0]==0x1e);break;
 case 0x0e: assert(TEST_TX&&n==2&&!c[1]&&size==4&&!memcmp(p,"DXT1",4));break;
 case 0x82: assert(!TEST_TX&&n==4&&!c[1]&&c[2]==0x7d&&!c[3]&&pad_state==1);mode=5;started=now();++starts;break;
 case 0x83: assert(TEST_TX&&n==4&&!c[1]&&c[2]==0x19&&!c[3]&&pad_state==2);mode=6;started=now();++starts;break;
 default: assert(!"unexpected/forbidden command");
 }
 return SX126X_HAL_STATUS_OK;
}
sx126x_hal_status_t sx126x_hal_read(const void *ctx,const uint8_t *c,uint16_t n,uint8_t *p,uint16_t size) {
 (void)ctx;memset(p,0,size);
 switch(c[0]) {
 case 0xc0: assert(n==1&&size==1);p[0]=(mode<<4)|2;break;
 case 0x17: assert(n==2&&!c[1]&&size==2);break;
 case 0x12: {assert(n==2&&!c[1]&&size==2);unsigned v=irq();p[0]=v>>8;p[1]=v;break;}
 case 0x15: assert(!TEST_TX&&n==2&&!c[1]&&size==1&&mode==5);p[0]=210;break;
 case 0x1d: assert(TEST_TX&&n==4&&c[1]==8&&c[2]==0xd8&&!c[3]&&size==1);break;
 default: assert(!"unexpected read");
 }
 return SX126X_HAL_STATUS_OK;
}
static void setup(void) {memset(&output,0,sizeof output);mode=2;starts=pad_state=resets=0;fail_cal=stuck=fail_gpio=fail_cleanup=false;}
int main(void) {
 setup();run_test();assert(!output.failure&&output.phase==7&&output.done&&starts==1&&pad_state==0&&mode==2);
 assert(output.irq==(TEST_TX?1u:512u));
 if(!TEST_TX) {assert(output.samples==16);for(unsigned i=0;i<16;++i)assert(output.rssi[i]==-105);}
 setup();fail_cal=true;run_test();assert(output.failure==2&&!starts&&pad_state==0);
 setup();fail_cal=fail_cleanup=true;run_test();assert(output.failure==2&&resets==1&&pad_state==0);
 setup();stuck=true;run_test();assert(output.failure==5&&starts==1&&pad_state==0&&now()<1000);
 setup();mode=6;run_test();assert(output.failure==1&&!starts&&pad_state==0);
 if(TEST_TX) {setup();fail_gpio=true;run_test();assert(output.failure==3&&!starts&&pad_state==0);}
 puts(TEST_TX?"TX RAM-test host checks passed":"RX RAM-test host checks passed");
}
