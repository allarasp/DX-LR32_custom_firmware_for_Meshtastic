#ifndef RF_SWITCH_H
#define RF_SWITCH_H
#include <stdint.h>
#define RF_SWITCH_PINS 0x18u
#define RF_SWITCH_PULLS 0x3c0u
#define RF_SWITCH_AF 0x000ff000u
enum rf_switch_state { RF_SWITCH_UNDRIVEN, RF_SWITCH_RX, RF_SWITCH_TX, RF_SWITCH_UNKNOWN=255 };
enum rf_switch_error { RF_SWITCH_OK, RF_SWITCH_UNVERIFIED_LOAD, RF_SWITCH_BAD_GPIO, RF_SWITCH_BAD_STATE };
struct rf_switch_gpio { uint32_t direction,type,pulls,alternate,input,latch; };
struct rf_switch_report { struct rf_switch_gpio gpio; enum rf_switch_state state; enum rf_switch_error error; };
struct rf_switch_plan { uint8_t set[2],clear[2],count; };
/* Platform inspection only: no output-driver API exists in this release. */
void rf_switch_read_gpio(struct rf_switch_gpio *gpio);
enum rf_switch_error rf_switch_inspect(struct rf_switch_report *report);
enum rf_switch_error rf_switch_init(void);
enum rf_switch_error rf_switch_request(enum rf_switch_state state);
/* Models factory intent for review/tests; not an approved electrical sequence. */
enum rf_switch_error rf_switch_factory_plan(enum rf_switch_state from,enum rf_switch_state to,struct rf_switch_plan *plan);
#endif
