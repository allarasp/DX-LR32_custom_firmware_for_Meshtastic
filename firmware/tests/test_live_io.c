#include <assert.h>
#include <stdio.h>
#include "cx32l003.h"
#include "radio_live.h"
#include "uart_tx_ring.h"
struct test_gpio test_gpio;
static bool delay_ok=true;
bool radio_io_delay(uint32_t ms) {assert(ms==1);return delay_ok;}
int main(void) {
    test_gpio.IDR=8;
    assert(live_pads(1)&&test_gpio.DIRCR==24&&test_gpio.ODSET==8&&test_gpio.ODCLR==16);
    test_gpio.IDR=16;
    assert(live_pads(2)&&test_gpio.ODCLR==8&&test_gpio.ODSET==16);
    test_gpio.OTYPER=24;test_gpio.PUPDR=0x3c0;test_gpio.AFR=0xff000;
    assert(!live_pads(1));assert(live_pads(0)&&test_gpio.DIRCR==0);
    assert(!live_pads(3)&&test_gpio.DIRCR==0);
    test_gpio=(struct test_gpio){0};assert(!live_pads(1));
    test_gpio.IDR=8;delay_ok=false;assert(!live_pads(1));assert(live_pads(0));
    struct uart_tx_ring q={0};uint8_t b;
    assert(uart_tx_free(&q)==1023&&!uart_tx_pop(&q,&b));
    for(unsigned round=0;round<5;++round) {
        for(unsigned i=0;i<1023;++i)assert(uart_tx_push(&q,(uint8_t)i));
        assert(!uart_tx_push(&q,42)&&uart_tx_free(&q)==0);
        for(unsigned i=0;i<1023;++i)assert(uart_tx_pop(&q,&b)&&b==(uint8_t)i);
        assert(uart_tx_free(&q)==1023&&!uart_tx_pop(&q,&b));
    }
    puts("Live GPIO and UART ring tests passed: factory ordering, corrupt-config release, invalid state, delay/readback faults, capacity and wrap");
}
