#ifndef RADIO_CONTROL_H
#define RADIO_CONTROL_H
#include "radio_protocol.h"
#define RC_MODULE_MIN 433000000u
#define RC_MODULE_MAX 532000000u
/* Matches inspected Meshtastic EU_433 region; no slot calculation here. */
#define RC_POLICY_MIN 433000000u
#define RC_POLICY_MAX 434000000u
#define RC_POWER_MAX 10
#define RC_CONFIG_SIZE 20u
struct rc_config {
    uint32_t frequency, bandwidth;
    uint16_t preamble, sync;
    uint8_t sf, cr, ldro, header, crc, iq, payload;
    int8_t power;
};
extern struct rc_config rc_pending,rc_active;
extern bool rc_active_valid;
extern uint32_t rc_generation;
uint8_t rc_init(void);
void rc_invalidate(void);
int rc_bandwidth_code(uint32_t bandwidth);
uint16_t rc_handle(uint8_t command,const uint8_t *data,uint16_t length,uint8_t *reply);
#endif
