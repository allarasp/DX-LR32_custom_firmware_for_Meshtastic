#include <assert.h>
#include <stdio.h>
#include "radio_diag.h"
#include "sx126x_hal.h"
#include "radio_protocol.h"
static uint32_t now;
static bool selected,admit;
static unsigned written;
uint32_t radio_io_millis(void) {return now;}
void radio_io_idle(void) {++now;}
bool radio_io_delay(uint32_t ms) {now+=ms;return true;}
bool radio_io_busy(void) {return false;}
void radio_io_select(bool s) {selected=s;}
bool radio_io_spi(uint8_t tx,uint8_t *rx) {(void)tx;assert(selected);*rx=0;return true;}
void radio_io_reset(bool low) {(void)low;}
static bool allowed(uint8_t kind,uint16_t size) {assert(kind==2&&size==564);return admit;}
static void put(uint8_t b) {(void)b;++written;}
int main(void) {
    for(int power=-10;power<=23;++power) {
        uint8_t b[3]={0x8e,(uint8_t)power,4};
        bool ok=sx126x_hal_write(0,b,3,0,0)==SX126X_HAL_STATUS_OK;
        assert(ok==(power>=-9&&power<=22));assert(!selected);
    }
    uint8_t pa[]={0x95,4,7,0,1};
    assert(sx126x_hal_write(0,pa,sizeof(pa),0,0)==SX126X_HAL_STATUS_OK);
    pa[1]=5;assert(sx126x_hal_write(0,pa,sizeof(pa),0,0)==SX126X_HAL_STATUS_ERROR);
    uint8_t dio2[]={0x9d,1},dio3[]={0x97,0,0,0,1},freq[]={0x86,0x39,0x30,0,0};
    assert(sx126x_hal_write(0,dio2,sizeof(dio2),0,0)==SX126X_HAL_STATUS_ERROR);
    assert(sx126x_hal_write(0,dio3,sizeof(dio3),0,0)==SX126X_HAL_STATUS_ERROR);
    assert(sx126x_hal_write(0,freq,sizeof(freq),0,0)==SX126X_HAL_STATUS_ERROR);
    uint8_t cal433[]={0x98,0x6b,0x6f},cal868[]={0x98,0xd7,0xdb};
    assert(sx126x_hal_write(0,cal433,3,0,0)==SX126X_HAL_STATUS_OK);
    assert(sx126x_hal_write(0,cal868,3,0,0)==SX126X_HAL_STATUS_OK);
    cal868[2]=0xdc;assert(sx126x_hal_write(0,cal868,3,0,0)==SX126X_HAL_STATUS_ERROR);
    const uint32_t frequencies[]={433000000,434000000,863000000,868000000,870000000};
    for(unsigned i=0;i<sizeof(frequencies)/sizeof(frequencies[0]);++i) {
        uint32_t word=(uint32_t)(((uint64_t)frequencies[i]<<25)/32000000);
        uint8_t f[]={0x86,word>>24,word>>16,word>>8,word};
        assert(sx126x_hal_write(0,f,sizeof(f),0,0)==SX126X_HAL_STATUS_OK);
    }
    struct rp_parser parser;rp_init(&parser,put,0);parser.can_emit=allowed;
    uint8_t payload[272];for(unsigned i=0;i<sizeof(payload);++i)payload[i]=0x7e;
    assert(!rp_emit(&parser,2,0x81,1,payload,sizeof(payload))&&!written&&parser.tx_drops==1);
    admit=true;assert(rp_emit(&parser,2,0x81,1,payload,sizeof(payload))&&written<=564&&written>272);
    puts("Live transport tests passed: configurable bounded power, DIO2/DIO3/band guards and atomic frame admission/drop counting");
}
