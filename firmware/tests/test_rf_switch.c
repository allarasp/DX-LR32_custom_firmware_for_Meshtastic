#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "rf_switch.h"
static struct rf_switch_gpio gpio;
void rf_switch_read_gpio(struct rf_switch_gpio *out) {*out=gpio;}
int main(void) {
    struct rf_switch_report report;
    gpio=(struct rf_switch_gpio){0};
    assert(rf_switch_init()==RF_SWITCH_OK);
    assert(rf_switch_inspect(&report)==RF_SWITCH_OK && report.state==RF_SWITCH_UNDRIVEN);
    for(unsigned i=0;i<10;++i) {
        assert(rf_switch_request(RF_SWITCH_RX)==RF_SWITCH_UNVERIFIED_LOAD);
        assert(rf_switch_request(RF_SWITCH_TX)==RF_SWITCH_UNVERIFIED_LOAD);
        assert(!gpio.direction && !gpio.pulls && !gpio.alternate && !gpio.latch);
    }
    assert(rf_switch_request((enum rf_switch_state)42)==RF_SWITCH_BAD_STATE);
    assert(rf_switch_request(RF_SWITCH_UNDRIVEN)==RF_SWITCH_OK);
    struct rf_switch_plan plan;
    assert(rf_switch_factory_plan(RF_SWITCH_RX,RF_SWITCH_TX,&plan)==RF_SWITCH_OK);
    unsigned levels=8;levels=(levels|plan.set[0])&~plan.clear[0];assert(levels==0);
    levels=(levels|plan.set[1])&~plan.clear[1];assert(levels==16 && plan.count==2);
    assert(rf_switch_factory_plan(RF_SWITCH_TX,RF_SWITCH_RX,&plan)==RF_SWITCH_OK);
    levels=(levels|plan.set[0])&~plan.clear[0];assert(levels==24);
    levels=(levels|plan.set[1])&~plan.clear[1];assert(levels==8);
    assert(rf_switch_factory_plan(RF_SWITCH_RX,RF_SWITCH_RX,&plan)==RF_SWITCH_OK && !plan.count);
    assert(rf_switch_factory_plan(RF_SWITCH_UNDRIVEN,RF_SWITCH_TX,&plan)==RF_SWITCH_BAD_STATE && !plan.count);
    for(unsigned fault=0;fault<3;++fault) {
        gpio=(struct rf_switch_gpio){0};
        if(fault==0) gpio.direction=8;
        if(fault==1) gpio.pulls=0x40;
        if(fault==2) gpio.alternate=0x1000;
        struct rf_switch_gpio original=gpio;
        assert(rf_switch_init()==RF_SWITCH_BAD_GPIO);
        assert(rf_switch_inspect(&report)==RF_SWITCH_BAD_GPIO && report.state==RF_SWITCH_UNKNOWN);
        assert(rf_switch_request(RF_SWITCH_TX)==RF_SWITCH_BAD_GPIO);
        assert(memcmp(&gpio,&original,sizeof(gpio))==0);
    }
    puts("RF-switch tests passed: high-impedance validation, denied actuation, factory-only transition model and intermediate states");
}
