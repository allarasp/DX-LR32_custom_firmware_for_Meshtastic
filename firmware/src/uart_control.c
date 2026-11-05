#include "uart_diag.h"
#include "radio_protocol.h"
#include "radio_control.h"
#include "radio_diag.h"
static struct rp_parser parser;
#ifdef UART_RADIO_LIVE
#include "radio_live.h"
static bool can_emit(uint8_t kind,uint16_t size) {
    /* Reserve room for a maximum command response while emitting events. */
    return diag_uart_tx_free()>=size+(kind==2?180u:0u);
}
static void emit_live(uint8_t event,uint16_t seq,const uint8_t *p,uint16_t n) {rp_emit(&parser,2,event,seq,p,n);}
#endif
extern uint32_t diag_uart_loss_count(void);
static void put(uint8_t b) {(void)diag_uart_put(b);}
static uint16_t handle(uint8_t cmd,const uint8_t *p,uint16_t n,uint8_t *out) {
#ifdef UART_RADIO_LIVE
    if(cmd==RP_GET_TRANSPORT_STATS) {
        if(n) {out[0]=RP_LENGTH;return 1;}
        out[0]=RP_OK;rp_put32(out+1,parser.tx_drops);
        rp_put32(out+5,diag_uart_tx_faults());rp_put16(out+9,(uint16_t)diag_uart_tx_free());return 11;
    }
#endif
    if(cmd==RP_GET_LINK_STATS) { /* GET_LINK_STATS, independent of radio health */
        if(n) {out[0]=RP_LENGTH;return 1;}
        out[0]=0;rp_put32(out+1,parser.frames);rp_put32(out+5,parser.errors);
        rp_put32(out+9,parser.timeouts);rp_put32(out+13,diag_uart_loss_count());return 17;
    }
#ifdef UART_RADIO_LIVE
    uint16_t result=live_handle(cmd,p,n,out,rp_u16(parser.body+3));
    if(result!=0xffff)return result;
#endif
    uint16_t count=rc_handle(cmd,p,n,out);
#ifdef UART_RADIO_LIVE
    if(cmd==RP_GET_CAPABILITIES && out[0]==RP_OK)rp_put32(out+2,0x7ffbffu); /* Adds GET_FREQUENCY_RANGES; never TCXO */
#endif
    return count;
}
int main(void) {
    diag_uart_init();rp_init(&parser,put,handle);
#ifdef UART_RADIO_LIVE
    parser.can_emit=can_emit;
#endif
    uint8_t boot[4]={RP_FW_MAJOR,RP_FW_MINOR,RP_FW_PATCH,rc_init()};
#ifdef UART_RADIO_LIVE
    live_init(emit_live);
#endif
    diagnostic.magic=DIAG_BOOT_MAGIC;diagnostic.stage=5;
    rp_emit(&parser,2,0x80,0,boot,sizeof(boot));
    uint32_t losses=diag_uart_loss_count();
    for(;;) {
        ++diagnostic.loops;diag_watchdog_feed();
        uint32_t current=diag_uart_loss_count();
        if(current!=losses) {losses=current;rp_abort(&parser);}
#ifdef UART_RADIO_LIVE
        if(!diag_uart_tx_poll())live_link_fault();
        live_poll();
#endif
        uint8_t b;
        if(diag_uart_get(&b)) {rp_feed(&parser,b,radio_io_millis());}
        rp_poll(&parser,radio_io_millis());
    }
}
