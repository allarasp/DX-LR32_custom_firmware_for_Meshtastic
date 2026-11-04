#ifndef SMOKE_H
#define SMOKE_H
#include <stdint.h>
#include <stdbool.h>
struct result {
 uint32_t magic,mode,phase,failure,ms,hal_error,irq,errors,initial,operating,final,samples;
 int16_t rssi[16];
 uint32_t pads_before,pads_active,pads_after,done,transactions;
};
extern volatile struct result *const result;
uint32_t now(void);
bool delay_ms(uint32_t n);
bool pads(unsigned state); /* 0 inputs; 1 RX; 2 TX */
void force_radio_reset(void);
void run_test(void);
#endif
