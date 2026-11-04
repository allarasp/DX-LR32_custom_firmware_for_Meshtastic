#include "rf_switch.h"
static enum rf_switch_error last_error;
enum rf_switch_error rf_switch_inspect(struct rf_switch_report *r) {
    rf_switch_read_gpio(&r->gpio);
    if((r->gpio.direction&RF_SWITCH_PINS) || (r->gpio.pulls&RF_SWITCH_PULLS) || (r->gpio.alternate&RF_SWITCH_AF)) {
        r->state=RF_SWITCH_UNKNOWN;r->error=RF_SWITCH_BAD_GPIO;return r->error;
    }
    r->state=RF_SWITCH_UNDRIVEN;r->error=last_error;return RF_SWITCH_OK;
}
enum rf_switch_error rf_switch_init(void) {
    struct rf_switch_report r;
    last_error=RF_SWITCH_OK;
    /* Validate reset high-impedance state. Never repair it by driving a pad. */
    last_error=rf_switch_inspect(&r);return last_error;
}
enum rf_switch_error rf_switch_request(enum rf_switch_state state) {
    struct rf_switch_report r;
    if(state!=RF_SWITCH_UNDRIVEN && state!=RF_SWITCH_RX && state!=RF_SWITCH_TX)
        return last_error=RF_SWITCH_BAD_STATE;
    if(rf_switch_inspect(&r)!=RF_SWITCH_OK) return last_error=RF_SWITCH_BAD_GPIO;
    if(state==RF_SWITCH_UNDRIVEN) return last_error=RF_SWITCH_OK;
    /* No runtime/compile-time override. Electrical approval and an actuator
     * backend must be separately implemented before this can ever drive. */
    return last_error=RF_SWITCH_UNVERIFIED_LOAD;
}
enum rf_switch_error rf_switch_factory_plan(enum rf_switch_state from,enum rf_switch_state to,struct rf_switch_plan *p) {
    *p=(struct rf_switch_plan){0};
    if((from!=RF_SWITCH_RX && from!=RF_SWITCH_TX) || (to!=RF_SWITCH_RX && to!=RF_SWITCH_TX)) return RF_SWITCH_BAD_STATE;
    if(from==to) return RF_SWITCH_OK;
    p->count=2;
    if(to==RF_SWITCH_TX) {p->clear[0]=8;p->set[1]=16;}
    else {p->set[0]=8;p->clear[1]=16;}
    return RF_SWITCH_OK;
}
