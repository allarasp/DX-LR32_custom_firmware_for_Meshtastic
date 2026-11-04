#include "cx32l003.h"
#include "radio_live.h"
#include "radio_diag.h"
bool live_pads(unsigned s) {
 /* Release must remain possible even after configuration corruption. */
 if(!s) {GPIOC->DIRCR&=~24u;GPIOC->ODCLR=24;return (GPIOC->DIRCR&24)==0;}
 if(s>2)return false;
 if((GPIOC->OTYPER&24)||(GPIOC->PUPDR&0x3c0)||(GPIOC->AFR&0xff000))return false;
 if(s==1) {GPIOC->ODSET=8;GPIOC->ODCLR=16;GPIOC->DIRCR|=24;}
 else {GPIOC->ODCLR=8;GPIOC->ODSET=16;}
 if(!radio_io_delay(1))return false;
 return (GPIOC->IDR&24)==(s==1?8u:16u);
}
