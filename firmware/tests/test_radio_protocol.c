#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "radio_protocol.h"
#include "radio_control.h"
#include "radio_diag.h"
#include "sx126x.h"
#include "sx126x_hal.h"

static uint8_t registers[0x900],frame_bytes[16],packet_type,raw=0x22;
static unsigned frame_size,transfers,calls;
static bool selected,reset_low,busy,spi_fault,timer_fault,corrupt_sync;
static uint32_t now;
static uint32_t pll_word;
void radio_io_init(void) {}
uint32_t radio_io_millis(void) {return now;}
void radio_io_idle(void) {++now;}
bool radio_io_delay(uint32_t ms) {now+=ms;return !timer_fault;}
bool radio_io_busy(void) {return busy;}
bool radio_io_dio1(void) {return false;}
void radio_io_reset(bool low) {reset_low=low;if(low) {packet_type=0;memset(registers,0,sizeof(registers));}}
uint32_t radio_io_spi_flags(void) {return 0;}
void radio_io_select(bool active) {
    if(!active && selected) {
        ++transfers;
        if(frame_bytes[0]==0x8a && frame_size==2) packet_type=frame_bytes[1];
        if(frame_bytes[0]==0x86 && frame_size==5)
            pll_word=(uint32_t)frame_bytes[1]<<24 | (uint32_t)frame_bytes[2]<<16 | (uint32_t)frame_bytes[3]<<8 | frame_bytes[4];
        if(frame_bytes[0]==0x0d && frame_size>=4) {
            unsigned address=(unsigned)frame_bytes[1]<<8 | frame_bytes[2];
            for(unsigned i=3;i<frame_size;++i) {assert(address+i-3<sizeof(registers));registers[address+i-3]=frame_bytes[i];}
        }
        assert(frame_bytes[0]!=0x83 && frame_bytes[0]!=0x82 && frame_bytes[0]!=0xc1 && frame_bytes[0]!=0x9d && frame_bytes[0]!=0x97 && frame_bytes[0]!=0x8e && frame_bytes[0]!=0x95);
    }
    selected=active;if(active) frame_size=0;
}
bool radio_io_spi(uint8_t tx,uint8_t *rx) {
    assert(selected);if(spi_fault) return false;
    unsigned i=frame_size++;assert(i<sizeof(frame_bytes));frame_bytes[i]=tx;*rx=i==1?0xa2:0;
    uint8_t cmd=frame_bytes[0];
    if(cmd==0xc0 && i==1) *rx=raw;
    if(cmd==0x11 && i==2) *rx=packet_type;
    if(cmd==0x1d && i>=4) {
        unsigned address=((unsigned)frame_bytes[1]<<8 | frame_bytes[2])+i-4;
        assert(address<sizeof(registers));*rx=registers[address];
        /* Corrupt only verification reads, not the driver's RMW reads. */
        if(corrupt_sync && address==0x740) *rx^=1;
    }
    return true;
}
static struct rp_parser parser;
static uint8_t wire[4096],request[1024],decoded[RP_BODY_MAX];
static unsigned wire_size;
static bool bridge;
static void put(uint8_t b) {if(bridge) {(void)fputc(b,stdout);return;}assert(wire_size<sizeof(wire));wire[wire_size++]=b;}
static uint16_t handle(uint8_t cmd,const uint8_t *p,uint16_t n,uint8_t *reply) {++calls;return rc_handle(cmd,p,n,reply);}
static unsigned prepare(uint8_t cmd,const uint8_t *p,unsigned n,uint16_t seq) {
    wire_size=0;rp_emit(&parser,0,cmd,seq,p,(uint16_t)n);
    assert(wire_size<=sizeof(request));memcpy(request,wire,wire_size);unsigned size=wire_size;wire_size=0;return size;
}
static void feed(const uint8_t *p,unsigned n) {for(unsigned i=0;i<n;++i) rp_feed(&parser,p[i],now);}
static uint8_t result(void) {
    unsigned used=0;bool escape=false;assert(wire_size>=2 && wire[0]==0x7e);
    for(unsigned i=1;i<wire_size;++i) {
        uint8_t b=wire[i];if(b==0x7e) break;
        if(escape) {b^=0x20;escape=false;}
        else if(b==0x7d) {escape=true;continue;}
        assert(used<sizeof(decoded));decoded[used++]=b;
    }
    assert(used>=10 && decoded[0]==1 && decoded[1]==1 && used==rp_u16(decoded+5)+9u);
    uint16_t crc=0xffff;for(unsigned i=0;i<used-2;++i) crc=rp_crc_byte(crc,decoded[i]);
    assert(crc==rp_u16(decoded+used-2));return decoded[7];
}
static uint8_t command(uint8_t cmd,const uint8_t *p,unsigned n) {
    unsigned size=prepare(cmd,p,n,0x7e7d);feed(request,size);uint8_t status=result();assert(rp_u16(decoded+3)==0x7e7d);return status;
}
static void frequency(uint32_t hz,uint8_t expected) {uint8_t p[4];rp_put32(p,hz);assert(command(RP_SET_FREQUENCY,p,4)==expected);}
static void lora(uint8_t sf,uint32_t bw,uint8_t cr,uint8_t ldro,uint8_t expected) {
    uint8_t p[7]={sf,0,0,0,0,cr,ldro};rp_put32(p+1,bw);assert(command(RP_SET_LORA,p,7)==expected);
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--bridge")) {
        bridge=true;rp_init(&parser,put,handle);assert(rc_init()==RP_OK);
        int byte;while((byte=fgetc(stdin))!=EOF) rp_feed(&parser,(uint8_t)byte,now++);
        return 0;
    }
    uint16_t crc=0xffff;for(const char *p="123456789";*p;++p) crc=rp_crc_byte(crc,(uint8_t)*p);
    assert(crc==0x29b1);
    rp_init(&parser,put,handle);assert(rc_init()==RP_OK);
    assert(command(RP_GET_VERSION,NULL,0)==RP_OK && decoded[8]==0 && decoded[9]==3 && decoded[10]==0);
    assert(rp_u16(decoded+3)==0x7e7d);
    assert(command(RP_GET_CAPABILITIES,NULL,0)==RP_OK && rp_u32(decoded+7+10)==RC_MODULE_MIN);
    assert(command(RP_APPLY,NULL,0)==RP_OK && rc_active_valid && pll_word==sx126x_convert_freq_in_hz_to_pll_step(433875000));
    assert(registers[0x740]==0x24 && registers[0x741]==0xb4 && packet_type==1);
    unsigned before=transfers;
    frequency(868000000,RP_UNSUPPORTED);frequency(432999999,RP_UNSUPPORTED);frequency(532000001,RP_UNSUPPORTED);
    frequency(500000000,RP_RANGE);lora(4,250000,5,0,RP_RANGE);lora(7,99999,5,0,RP_RANGE);lora(7,250000,9,0,RP_RANGE);
    uint8_t power=11;assert(command(RP_SET_POWER,&power,1)==RP_RANGE);power=(uint8_t)-10;assert(command(RP_SET_POWER,&power,1)==RP_RANGE);
    assert(transfers==before);
    uint32_t generation=rc_generation;
    frequency(433010000,RP_OK);assert(command(RP_APPLY,NULL,0)==RP_COMBINATION);
    assert(rc_active_valid && rc_generation==generation && transfers==before);
    frequency(433500000,RP_OK);lora(12,125000,5,0,RP_OK);assert(command(RP_APPLY,NULL,0)==RP_COMBINATION);
    lora(12,125000,5,1,RP_OK);assert(command(RP_APPLY,NULL,0)==RP_OK);
    lora(5,250000,5,0,RP_OK);
    uint8_t packet[8]={8,0,0x7e,0x7d,0,1,1,255};assert(command(RP_SET_PACKET,packet,8)==RP_OK);
    assert(command(RP_APPLY,NULL,0)==RP_COMBINATION);packet[0]=12;assert(command(RP_SET_PACKET,packet,8)==RP_OK);
    assert(command(RP_APPLY,NULL,0)==RP_OK && registers[0x740]==0x7d && registers[0x741]==0x7e && !(registers[0x736]&4));
    assert(command(RP_GET_CONFIG,NULL,0)==RP_OK && decoded[7+41]==1);
    before=transfers;
    uint8_t payload[RP_MAX_PAYLOAD];memset(payload,0x7e,sizeof(payload));
    assert(command(RP_SEND,payload,sizeof(payload))==RP_DISABLED);
    assert(command(RP_START_RX,NULL,0)==RP_DISABLED);
    uint8_t mode=4;assert(command(RP_SET_MODE,&mode,1)==RP_DISABLED);assert(transfers==before);
    assert(command(0xfe,NULL,0)==RP_UNKNOWN);assert(command(RP_GET_VERSION,payload,1)==RP_LENGTH);
    unsigned count=calls;
    wire_size=0;rp_emit(&parser,1,1,1,NULL,0);
    unsigned echoed=wire_size;memcpy(request,wire,echoed);wire_size=0;feed(request,echoed);
    assert(wire_size==0 && calls==count); /* Responses never cause response loops. */
    unsigned size=prepare(RP_SET_FREQUENCY,payload,4,1);request[3]^=0x40;feed(request,size);
    assert(result()==RP_CRC_ERROR && calls==count);
    size=prepare(RP_GET_VERSION,NULL,0,1);feed(request,size-2);now+=100;rp_poll(&parser,now);
    assert(result()==RP_TIMEOUT);assert(command(RP_GET_VERSION,NULL,0)==RP_OK);
    size=prepare(RP_GET_VERSION,NULL,0,1);request[size-1]=0x7d;feed(request,size);
    rp_feed(&parser,0x7e,now);assert(result()==RP_FRAME_ERROR);
    size=prepare(RP_GET_VERSION,NULL,0,1);feed(request,8);rp_feed(&parser,0x7d,now);rp_feed(&parser,0x01,now);rp_feed(&parser,0x7e,now);
    assert(result()==RP_FRAME_ERROR);
    wire_size=0;uint8_t head[]={0x7e,1,0,1,1,0,0xff,0xff};feed(head,sizeof(head));
    for(unsigned i=0;i<RP_BODY_MAX+10;++i) rp_feed(&parser,0,now);
    rp_feed(&parser,0x7e,now);assert(result()==RP_OVERFLOW);
    assert(command(RP_GET_VERSION,NULL,0)==RP_OK);
    size=prepare(RP_SEND,payload,sizeof(payload),1);
    for(unsigned i=0;i<25;++i) {rp_feed(&parser,request[i],now);now+=50;}
    assert(result()==RP_TIMEOUT);
    assert(command(RP_GET_VERSION,NULL,0)==RP_OK);
    now=0xfffffff0u;size=prepare(RP_GET_VERSION,NULL,0,1);feed(request,8);now+=101;rp_poll(&parser,now);assert(result()==RP_TIMEOUT);
    assert(command(RP_GET_VERSION,NULL,0)==RP_OK);
    corrupt_sync=true;assert(command(RP_APPLY,NULL,0)==RP_READBACK && !rc_active_valid);corrupt_sync=false;
    spi_fault=true;assert(command(RP_APPLY,NULL,0)==RP_RADIO_ERROR && !selected && !rc_active_valid);spi_fault=false;
    busy=true;assert(command(RP_APPLY,NULL,0)==RP_RADIO_ERROR && !selected);busy=false;
    raw=0xff;assert(command(RP_APPLY,NULL,0)==RP_RADIO_ERROR);raw=0x22;
    timer_fault=true;assert(command(RP_RESET,NULL,0)==RP_RADIO_ERROR && !reset_low);timer_fault=false;
    assert(command(RP_RESET,NULL,0)==RP_OK && !rc_active_valid);assert(command(RP_APPLY,NULL,0)==RP_OK);
    before=transfers;uint8_t unsafe[][5]={{0x83},{0x82},{0xc1},{0x9d},{0x97},{0x8e},{0x95},{0x86,0x36,0x40,0,0}};
    for(unsigned i=0;i<sizeof(unsafe)/sizeof(unsafe[0]);++i) assert(sx126x_hal_write(NULL,unsafe[i],5,NULL,0)==SX126X_HAL_STATUS_ERROR);
    assert(transfers==before);
    /* Random malformed traffic must remain bounded and recover to a valid frame. */
    uint32_t rng=1;for(unsigned i=0;i<50000;++i) {rng=rng*1664525u+1013904223u;wire_size=0;rp_feed(&parser,(uint8_t)(rng>>24),now++);}
    wire_size=0;rp_abort(&parser);assert(command(RP_GET_VERSION,NULL,0)==RP_OK);
    puts("Binary protocol/config tests passed: CRC, escaping, limits, deadlines/wrap, recovery, staging, readback failures and RF rejection");
}
