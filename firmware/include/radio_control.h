#ifndef RADIO_CONTROL_H
#define RADIO_CONTROL_H
#include "radio_protocol.h"
#define RC_MODULE_MIN 433000000u
#define RC_MODULE_MAX 532000000u
/* Primary tuning window; no Meshtastic slot calculation here. */
#define RC_POLICY_MIN 433000000u
#define RC_POLICY_MAX 434000000u
#define RC_BAND868_MIN 863000000u
#define RC_BAND868_MAX 870000000u
/* A single SET_FREQUENCY selects the band. No separate mode/enable command. */
static inline uint8_t rc_frequency_bounds(uint32_t hz,uint32_t *lower,uint32_t *upper) {
    if(hz>=RC_POLICY_MIN && hz<=RC_POLICY_MAX) {
        *lower=RC_POLICY_MIN;*upper=RC_POLICY_MAX;return RP_OK;
    }
#ifdef UART_RADIO_LIVE
    if(hz>=RC_BAND868_MIN && hz<=RC_BAND868_MAX) {
        *lower=RC_BAND868_MIN;*upper=RC_BAND868_MAX;return RP_OK;
    }
#endif
    return hz<RC_MODULE_MIN || hz>RC_MODULE_MAX?RP_UNSUPPORTED:RP_RANGE;
}
#ifdef UART_RADIO_LIVE
/* SX1262 command range; regional policy belongs to the host. */
#define RC_POWER_MAX 22
#else
#define RC_POWER_MAX 10
#endif
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
