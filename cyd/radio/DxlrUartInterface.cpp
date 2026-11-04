#include "configuration.h"
#ifdef USE_DXLR_UART
#include "DxlrUartInterface.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Throttle.h"
#include "concurrency/LockGuard.h"
#include <cmath>
#include <cstring>
static uint16_t u16(const uint8_t *p) {return p[0]|(uint16_t)p[1]<<8;}
static uint32_t u32(const uint8_t *p) {return u16(p)|(uint32_t)u16(p+2)<<16;}
static void put16(uint8_t *p,uint16_t n) {p[0]=n;p[1]=n>>8;}
static void put32(uint8_t *p,uint32_t n) {put16(p,n);put16(p+2,n>>16);}
static uint16_t crcByte(uint16_t crc,uint8_t b) {
 crc^=(uint16_t)b<<8;for(unsigned i=0;i<8;++i)crc=(crc<<1)^((crc&0x8000)?0x1021:0);return crc;
}
DxlrUartInterface::DxlrUartInterface():OSThread("DXLR-UART",5) {}
bool DxlrUartInterface::init() {
 RadioInterface::init();serial.setRxBufferSize(2048);serial.begin(115200,SERIAL_8N1,DXLR_RX_PIN,DXLR_TX_PIN);
 LOG_INFO("DXLR UART prepared RX=%d TX=%d; waiting for v0.4.0/v0.5.x controller",DXLR_RX_PIN,DXLR_TX_PIN);return true;
}
void DxlrUartInterface::releaseTx() {
 if(sendingPacket) {packetPool.release(sendingPacket);sendingPacket=nullptr;}
 if(queued) {packetPool.release(queued);queued=nullptr;}armOk=false;
}
void DxlrUartInterface::dropLink() {ready=false;stage=0;pendingCommand=0;lastProbe=millis();releaseTx();}
bool DxlrUartInterface::reconfigure() {concurrency::LockGuard guard(&lock);RadioInterface::reconfigure();dropLink();return true;}
ErrorCode DxlrUartInterface::send(meshtastic_MeshPacket *p) {
 concurrency::LockGuard guard(&lock);
 if(disabled || !config.lora.tx_enabled || !ready) {packetPool.release(p);return ERRNO_DISABLED;}
 if(queued || sendingPacket || (attempted&&Throttle::isWithinTimespanMs(lastAttempt,31000))) {
  packetPool.release(p);return ERRNO_UNKNOWN;
 }
 queued=p;return ERRNO_OK;
}
meshtastic_QueueStatus DxlrUartInterface::getQueueStatus() {
 concurrency::LockGuard guard(&lock);
 meshtastic_QueueStatus s={};s.maxlen=1;s.free=(queued||sendingPacket)?0:1;return s;
}
uint32_t DxlrUartInterface::getPacketTime(uint32_t pl,bool received) {
 (void)received;float symbol=(1u<<sf)/bw;bool de=symbol>=16.0f;
 float count=8+fmaxf(0,ceilf((8.0f*pl-4*sf+44)/(4*(sf-2*de)))*cr);
 return (uint32_t)ceilf((preambleLength+4.25f+count)*symbol);
}
void DxlrUartInterface::request(uint8_t command,const uint8_t *p,uint16_t n) {
 uint8_t head[7]={1,0,command,0,0,0,0};put16(head+3,++sequence);put16(head+5,n);
 pendingCommand=command;pendingSequence=sequence;requestAt=millis();
 auto write=[this](uint8_t b) {if(b==0x7e||b==0x7d) {serial.write(0x7d);serial.write(b^0x20);}else serial.write(b);};
 uint16_t crc=0xffff;serial.write(0x7e);
 for(uint8_t b:head) {crc=crcByte(crc,b);write(b);}
 for(unsigned i=0;i<n;++i) {crc=crcByte(crc,p[i]);write(p[i]);}
 write(crc);write(crc>>8);serial.write(0x7e);
}
void DxlrUartInterface::configureNext() {
 uint8_t p[8]={0};
 switch(stage) {
 case 0:request(1);break;
 case 1:request(2);break;
 case 2:request(10);break;
 case 3:put32(p,(uint32_t)llround((double)getFreq()*1000000.0));request(4,p,4);break;
 case 4:p[0]=sf;put32(p+1,(uint32_t)lround(bw*1000));p[5]=cr;p[6]=((1u<<sf)/bw)>=16;request(5,p,7);break;
 case 5:put16(p,preambleLength);put16(p+2,0x24b4);p[5]=1;p[7]=255;request(6,p,8);break;
 case 6:p[0]=(uint8_t)-9;request(7,p,1);break;
 case 7:request(8);break;
 case 8:request(9,p,4);break;
 }
}
void DxlrUartInterface::consume(uint8_t b) {
 if(b==0x7e) {
  if(collecting&&used&&!discard&&!escaped)frame();
  used=0;collecting=true;escaped=discard=false;rxAt=millis();return;
 }
 if(!collecting||discard)return;
 rxAt=millis();
 if(escaped) {if(b!=0x5d&&b!=0x5e) {discard=true;return;}b^=0x20;escaped=false;}
 else if(b==0x7d) {escaped=true;return;}
 if(used>=sizeof(body)) {discard=true;return;}body[used++]=b;
}
void DxlrUartInterface::frame() {
 if(used<9||body[0]!=1)return;
 uint16_t n=u16(body+5);if(n>272||used!=n+9)return;
 uint16_t crc=0xffff;for(unsigned i=0;i+2u<used;++i)crc=crcByte(crc,body[i]);
 if(crc!=u16(body+used-2))return;
 uint8_t kind=body[1],cmd=body[2];uint16_t seq=u16(body+3);const uint8_t *p=body+7;
 if(kind==2) {event(cmd,seq,p,n);return;}
 if(kind!=1||cmd!=pendingCommand||seq!=pendingSequence||!n)return;
 pendingCommand=0;
 if(p[0]) {LOG_WARN("DXLR command %u failed: %u",cmd,p[0]);dropLink();return;}
 if(stage<9) {
  if(stage==0 && (n!=5||p[1]!=0||(p[2]!=4&&p[2]!=5)||(p[2]==4?p[3]!=0:p[3]>1)||p[4]!=1)) {dropLink();return;}
  if(stage==1 && (n!=76||(u32(p+2)&0x300)!=0x300)) {dropLink();return;}
  if(stage==7 && (n!=9||u16(p+1)!=15||u16(p+3)!=15)) {dropLink();return;}
  ++stage;if(stage==9) {ready=true;LOG_INFO("DXLR radio connected and receiving; TX=%s",config.lora.tx_enabled?"enabled":"disabled");}
 } else if(cmd==21)armOk=true;
}
void DxlrUartInterface::event(uint8_t cmd,uint16_t seq,const uint8_t *p,uint16_t n) {
 if(cmd==0x80) {dropLink();return;}
 if(cmd==0x83) {LOG_WARN("DXLR asynchronous radio error");dropLink();return;}
 if(cmd==0x82) {
  if(n!=9||seq!=txSequence||!sendingPacket)return;
  LOG_INFO("DXLR TX complete result=%u elapsed=%u",p[0],u32(p+1));releaseTx();return;
 }
 if(cmd!=0x81||!ready||n<31||n!=15u+p[14]||(p[13]&15)!=15)return;
 unsigned len=p[14];if(len<=16||len>255)return;
 PacketHeader h;memcpy(&h,p+15,sizeof(h));
 if(!h.from || h.from==UINT32_MAX)return;
 auto mp=packetPool.allocZeroed();if(!mp)return;
 mp->from=h.from;mp->to=h.to;mp->id=h.id;mp->channel=h.channel;
 mp->hop_limit=h.flags&7;mp->hop_start=(h.flags>>5)&7;mp->want_ack=!!(h.flags&8);mp->via_mqtt=!!(h.flags&16);
 mp->next_hop=mp->hop_start?h.next_hop:0;mp->relay_node=mp->hop_start?h.relay_node:0;
 mp->rx_rssi=(int16_t)u16(p+8)/4;mp->rx_snr=(int8_t)p[10]/4.0f;
 mp->which_payload_variant=meshtastic_MeshPacket_encrypted_tag;mp->encrypted.size=len-16;
 memcpy(mp->encrypted.bytes,p+31,len-16);deliverToReceiver(mp);
}
int32_t DxlrUartInterface::runOnce() {
 concurrency::LockGuard guard(&lock);
 for(unsigned count=0;count<1024&&serial.available();++count)consume(serial.read());
 if(used&&!Throttle::isWithinTimespanMs(rxAt,100)) {used=0;collecting=false;}
 if(pendingCommand) {if(!Throttle::isWithinTimespanMs(requestAt,3000))dropLink();return 5;}
 if(sendingPacket) {if(!Throttle::isWithinTimespanMs(lastAttempt,5000))dropLink();return 5;}
 if(!ready) {
  if(config.lora.region!=meshtastic_Config_LoRaConfig_RegionCode_EU_433||sf!=11||bw!=250||cr!=5)return 500;
  if(!stage && lastProbe && Throttle::isWithinTimespanMs(lastProbe,2000))return 50;
  configureNext();return 5;
 }
 if(queued) {
  if(!config.lora.tx_enabled) {releaseTx();return 5;}
  if(!armOk) {const uint8_t token[]={'T','X','1','!'};request(21,token,4);return 5;}
  auto p=queued;queued=nullptr;unsigned n=beginSending(p);
  if(n>255||getPacketTime(n)>2000) {releaseTx();return 5;}
  request(11,reinterpret_cast<const uint8_t *>(&radioBuffer),n);txSequence=sequence;
  attempted=true;lastAttempt=millis();armOk=false;
 }
 return 5;
}
#endif
