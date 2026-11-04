#ifndef RADIO_LIVE_H
#define RADIO_LIVE_H
#include "radio_control.h"
/* General raw-LoRa controller (meshctrl build name retained); legacy uartctrl stays v0.3.1. */
typedef void (*live_emit_fn)(uint8_t event,uint16_t sequence,const uint8_t *payload,uint16_t length);
void live_init(live_emit_fn emit);
void live_poll(void);
void live_link_fault(void);
uint16_t live_handle(uint8_t command,const uint8_t *data,uint16_t length,uint8_t *reply,uint16_t sequence);
bool live_pads(unsigned state);
#endif
