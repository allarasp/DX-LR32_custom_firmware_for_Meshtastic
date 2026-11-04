#include "cx32l003.h"
#include "test.h"
#include "sx126x_hal.h"
volatile struct result *const result=(void *)0x20000b00;
uint32_t now(void) {
 if(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)++result->ms;
 if(IWDG->CFGR & IWDG_CFGR_IWDGRUNF_Msk)IWDG->CMDCR=0xaa;
 return result->ms;
}
bool delay_ms(uint32_t n) {
 uint32_t start=now();
 for(unsigned guard=2000000;guard;--guard)if(now()-start>=n)return true;
 result->hal_error=3;return false;
}
static bool ready(void) {
 uint32_t start=now();
 for(unsigned guard=2000000;guard;--guard) {
  if(!(GPIOB->IDR&(1u<<5)))return true;
  if(now()-start>=100) {result->hal_error=1;return false;}
 }
 result->hal_error=3;return false;
}
static bool byte(uint8_t tx,uint8_t *rx) {
 uint32_t start=now();SPI->DATA=tx;
 for(unsigned guard=2000000;guard;--guard) {
  uint32_t flags=SPI->SR;
  if(flags&(SPI_SR_WCOL_Msk|SPI_SR_MDF_Msk|SPI_SR_SSERR_Msk))break;
  if(flags&SPI_SR_SPIF_Msk) {*rx=(uint8_t)SPI->DATA;return true;}
  if(now()-start>=10)break;
 }
 result->hal_error=2;return false;
}
static sx126x_hal_status_t transfer(const uint8_t *cmd,uint16_t n,const uint8_t *tx,uint8_t *rx,uint16_t size) {
 if(!ready())return SX126X_HAL_STATUS_ERROR;
 GPIOA->ODCLR=4;SPI->SSN=0;
 bool ok=true;uint8_t discard;
 for(unsigned i=0;ok&&i<n;++i)ok=byte(cmd[i],&discard);
 for(unsigned i=0;ok&&i<size;++i)ok=byte(tx?tx[i]:0,rx?rx+i:&discard);
 GPIOA->ODSET=4;SPI->SSN=SPI_SSN_SSN_Msk;
 if(ok)ok=delay_ms(2)&&ready();
 if(ok)++result->transactions;
 return ok?SX126X_HAL_STATUS_OK:SX126X_HAL_STATUS_ERROR;
}
sx126x_hal_status_t sx126x_hal_write(const void *c,const uint8_t *cmd,uint16_t n,const uint8_t *data,uint16_t size) {(void)c;return transfer(cmd,n,data,0,size);}
sx126x_hal_status_t sx126x_hal_read(const void *c,const uint8_t *cmd,uint16_t n,uint8_t *data,uint16_t size) {(void)c;return transfer(cmd,n,0,data,size);}
bool pads(unsigned state) {
 if((GPIOC->OTYPER&24)||(GPIOC->PUPDR&0x3c0)||(GPIOC->AFR&0xff000))return false;
 if(!state) {
  if(!result->pads_before)result->pads_before=GPIOC->IDR&24;
  GPIOC->DIRCR&=~24u;GPIOC->ODCLR=24;
  result->pads_after=GPIOC->IDR&24;return true;
 }
 if(state==1) {GPIOC->ODSET=8;GPIOC->ODCLR=16;GPIOC->DIRCR|=24;}
 else {GPIOC->ODCLR=8;GPIOC->ODSET=16;}
 if(!delay_ms(1))return false;
 result->pads_active=GPIOC->IDR&24;
 return result->pads_active==(state==1?8u:16u);
}
void force_radio_reset(void) {GPIOB->ODCLR=16;(void)delay_ms(2);GPIOB->ODSET=16;(void)delay_ms(10);}
void ram_main(void) {
 volatile uint32_t *p=(void *)result;for(unsigned i=0;i<sizeof(*result)/4;++i)p[i]=0;
 SysTick->CTRL=0;SysTick->LOAD=22120-1;SysTick->VAL=0;SysTick->CTRL=5;
 run_test();SysTick->CTRL=0;
}
