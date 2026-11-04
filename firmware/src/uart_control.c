#include "uart_diag.h"
#include "radio_protocol.h"
#include "radio_control.h"
#include "radio_diag.h"
static struct rp_parser parser;
extern uint32_t diag_uart_loss_count(void);
static void put(uint8_t b) {(void)diag_uart_put(b);}
static uint16_t handle(uint8_t cmd,const uint8_t *p,uint16_t n,uint8_t *out) {
    if(cmd==RP_GET_LINK_STATS) { /* GET_LINK_STATS, independent of radio health */
        if(n) {out[0]=RP_LENGTH;return 1;}
        out[0]=0;rp_put32(out+1,parser.frames);rp_put32(out+5,parser.errors);
        rp_put32(out+9,parser.timeouts);rp_put32(out+13,diag_uart_loss_count());return 17;
    }
    return rc_handle(cmd,p,n,out);
}
int main(void) {
    diag_uart_init();rp_init(&parser,put,handle);
    uint8_t boot[4]={0,3,0,rc_init()};
    diagnostic.magic=DIAG_BOOT_MAGIC;diagnostic.stage=5;
    rp_emit(&parser,2,0x80,0,boot,sizeof(boot));
    uint32_t losses=diag_uart_loss_count();
    for(;;) {
        ++diagnostic.loops;diag_watchdog_feed();
        uint32_t current=diag_uart_loss_count();
        if(current!=losses) {losses=current;rp_abort(&parser);}
        uint8_t b;
        if(diag_uart_get(&b)) {rp_feed(&parser,b,radio_io_millis());}
        rp_poll(&parser,radio_io_millis());
    }
}
