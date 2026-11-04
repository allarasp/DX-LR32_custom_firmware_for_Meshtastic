#include <stddef.h>
#include "radio_protocol.h"
uint16_t rp_crc_byte(uint16_t crc,uint8_t b) {
    crc ^= (uint16_t)b<<8;
    for(unsigned i=0;i<8;++i) crc=(uint16_t)((crc<<1)^((crc&0x8000u)?0x1021u:0));
    return crc;
}
static void escaped(struct rp_parser *p,uint8_t b) {
    if(b==0x7e || b==0x7d) { p->write(0x7d); p->write(b^0x20); }
    else p->write(b);
}
void rp_emit(struct rp_parser *p,uint8_t kind,uint8_t cmd,uint16_t seq,const uint8_t *data,uint16_t n) {
    if(n>RP_MAX_PAYLOAD) return;
    uint8_t header[7]={RP_VERSION,kind,cmd,0,0,0,0};
    rp_put16(header+3,seq); rp_put16(header+5,n);
    uint16_t crc=0xffff;
    p->write(0x7e);
    for(unsigned i=0;i<7;++i) {crc=rp_crc_byte(crc,header[i]);escaped(p,header[i]);}
    for(unsigned i=0;i<n;++i) {crc=rp_crc_byte(crc,data[i]);escaped(p,data[i]);}
    escaped(p,(uint8_t)crc);escaped(p,(uint8_t)(crc>>8));p->write(0x7e);
}
void rp_init(struct rp_parser *p,rp_write_fn write,rp_handler_fn handler) {
    *p=(struct rp_parser){0};p->write=write;p->handler=handler;
}
void rp_abort(struct rp_parser *p) {p->used=0;p->collecting=p->escaped=p->discard=false;++p->errors;}
static void error(struct rp_parser *p,uint8_t status) {
    ++p->errors;
    /* Do not answer noise or response/event frames, preventing response loops. */
    if(p->used>=7 && p->body[1]==0)
        rp_emit(p,1,p->body[2],rp_u16(p->body+3),&status,1);
}
void rp_poll(struct rp_parser *p,uint32_t now) {
    if(p->collecting && (p->used || p->escaped || p->discard) &&
       ((uint32_t)(now-p->last)>=100u || (uint32_t)(now-p->started)>=1000u)) {
        ++p->timeouts;error(p,RP_TIMEOUT);p->collecting=false;p->used=0;p->escaped=p->discard=false;
    }
}
static void frame(struct rp_parser *p) {
    if(p->discard) {error(p,p->fault);return;}
    if(p->escaped || p->used<9) {error(p,RP_FRAME_ERROR);return;}
    uint16_t n=rp_u16(p->body+5);
    if(n>RP_MAX_PAYLOAD || p->used!=n+9u) {error(p,RP_LENGTH);return;}
    uint16_t crc=0xffff;
    for(unsigned i=0;i<p->used-2u;++i) crc=rp_crc_byte(crc,p->body[i]);
    if(crc!=rp_u16(p->body+p->used-2)) {error(p,RP_CRC_ERROR);return;}
    if(p->body[1]!=0) return;
    if(p->body[0]!=RP_VERSION) {error(p,RP_VERSION_ERROR);return;}
    /* Handler responses are bounded by this explicit API contract. */
    uint8_t reply[80];
    uint16_t count=p->handler(p->body[2],p->body+7,n,reply);
    if(count==0 || count>sizeof(reply)) {error(p,RP_OVERFLOW);return;}
    ++p->frames;
    rp_emit(p,1,p->body[2],rp_u16(p->body+3),reply,count);
    if(reply[0]==RP_RADIO_ERROR || reply[0]==RP_READBACK) {
        uint8_t event[2]={p->body[2],reply[0]};
        rp_emit(p,2,0x83,rp_u16(p->body+3),event,sizeof(event));
    }
}
void rp_feed(struct rp_parser *p,uint8_t byte,uint32_t now) {
    rp_poll(p,now);
    if(byte==0x7e) {
        if(p->collecting && (p->used || p->escaped || p->discard)) frame(p);
        p->used=0;p->escaped=p->discard=false;p->collecting=true;p->last=p->started=now;return;
    }
    if(!p->collecting) return;
    if(!p->used && !p->escaped && !p->discard) p->started=now;
    p->last=now;
    if(p->discard) return;
    if(p->escaped) {
        /* Only the two reserved octets have canonical escaped encodings. */
        if(byte!=0x5e && byte!=0x5d) {p->fault=RP_FRAME_ERROR;p->discard=true;return;}
        byte^=0x20;p->escaped=false;
    } else if(byte==0x7d) {p->escaped=true;return;}
    if(p->used==sizeof(p->body)) {p->fault=RP_OVERFLOW;p->discard=true;return;}
    p->body[p->used++]=byte;
}
