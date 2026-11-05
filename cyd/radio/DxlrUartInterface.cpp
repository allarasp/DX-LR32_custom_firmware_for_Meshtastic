#include "configuration.h"
#ifdef USE_DXLR_UART
#include "DxlrUartInterface.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "airtime.h"
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
DxlrUartInterface *DxlrUartInterface::instance=nullptr;
DxlrUartInterface::DxlrUartInterface():OSThread("DXLR-UART",5) {instance=this;}
bool DxlrUartInterface::init() {
 RadioInterface::init();serial.setRxBufferSize(2048);serial.begin(115200,SERIAL_8N1,DXLR_RX_PIN,DXLR_TX_PIN);
 LOG_INFO("DXLR UART prepared RX=%d TX=%d; waiting for v0.8.0 host-managed controller",DXLR_RX_PIN,DXLR_TX_PIN);return true;
}
void DxlrUartInterface::releaseTx() {
 if(sendingPacket) {packetPool.release(sendingPacket);sendingPacket=nullptr;}
 while(auto p=txQueue.dequeue())packetPool.release(p);
 armOk=txRxStopped=activityClear=cadPending=cadClear=txScheduled=false;scheduled=nullptr;
}
void DxlrUartInterface::dropLink() {ready=false;stage=0;pendingCommand=0;lastProbe=millis();releaseTx();}
bool DxlrUartInterface::reconfigure() {concurrency::LockGuard guard(&lock);RadioInterface::reconfigure();dropLink();return true;}
ErrorCode DxlrUartInterface::send(meshtastic_MeshPacket *p) {
 concurrency::LockGuard guard(&lock);
 if(disabled || !config.lora.tx_enabled || !ready) {packetPool.release(p);return ERRNO_DISABLED;}
 if(!txQueue.enqueue(p)) {packetPool.release(p);return ERRNO_UNKNOWN;}
 return ERRNO_OK;
}
meshtastic_QueueStatus DxlrUartInterface::getQueueStatus() {
 concurrency::LockGuard guard(&lock);
 meshtastic_QueueStatus s={};s.maxlen=txQueue.getMaxLen();s.free=txQueue.getFree();return s;
}
bool DxlrUartInterface::cancelSending(NodeNum from,PacketId id) {
 concurrency::LockGuard guard(&lock);
 auto p=txQueue.remove(from,id);if(!p)return false;
 packetPool.release(p);txScheduled=false;scheduled=nullptr;return true;
}
bool DxlrUartInterface::findInTxQueue(NodeNum from,PacketId id) {
 concurrency::LockGuard guard(&lock);return txQueue.find(from,id);
}
bool DxlrUartInterface::removePendingTXPacket(NodeNum from,PacketId id,uint32_t hops) {
 concurrency::LockGuard guard(&lock);
 auto p=txQueue.remove(from,id,true,true,hops);if(!p)return false;
 packetPool.release(p);txScheduled=false;scheduled=nullptr;return true;
}
void DxlrUartInterface::clampToLateRebroadcastWindow(NodeNum from,PacketId id) {
 concurrency::LockGuard guard(&lock);
 auto p=txQueue.remove(from,id,true,false);if(!p)return;
 p->tx_after=millis()+getTxDelayMsecWeightedWorst(p->rx_snr);
 if(!txQueue.enqueue(p))packetPool.release(p);
 txScheduled=false;scheduled=nullptr;
}
void DxlrUartInterface::resumeRx() {
 armOk=txRxStopped=activityClear=cadClear=cadPending=txScheduled=false;scheduled=nullptr;
 const uint8_t continuous[4]={0};request(9,continuous,4);
}
void DxlrUartInterface::scheduleTx() {
 auto p=txQueue.getFront();if(!p)return;
 uint32_t delay=(p->rx_rssi||p->rx_snr)?getTxDelayMsecWeighted(p):getTxDelayMsec();
 if(p->tx_after) {
  uint32_t now=millis(),latest=now+2*getTxDelayMsecWeightedWorst(p->rx_snr);
  uint32_t candidate=p->tx_after+delay;
  if((int32_t)(candidate-(now+delay))<0)candidate=now+delay;
  if((int32_t)(candidate-latest)>0)candidate=latest;
  p->tx_after=candidate;nextTxAt=candidate;
 }else nextTxAt=millis()+delay;
 scheduled=p;txScheduled=true;
}
bool DxlrUartInterface::airtimeAllowed() {
 return airTime->isTxAllowedChannelUtil() && (config.lora.override_duty_cycle ||
   myRegion->dutyCycle>=100 || airTime->utilizationTXPercent()<myRegion->dutyCycle);
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
 case 3:p[0]=config.lora.sx126x_rx_boosted_gain;request(28,p,1);break;
 case 4:p[0]=1;request(37,p,1);break;
 case 5:put32(p,(uint32_t)llround((double)getFreq()*1000000.0));request(4,p,4);break;
 case 6:p[0]=sf;put32(p+1,(uint32_t)lround(bw*1000));p[5]=cr;p[6]=((1u<<sf)/bw)>=16;request(5,p,7);break;
 case 7:put16(p,preambleLength);put16(p+2,0x24b4);p[5]=1;p[7]=255;request(6,p,8);break;
 case 8:limitPower(22);p[0]=(uint8_t)power;request(7,p,1);break;
 case 9:request(8);break;
 case 10:request(9,p,4);break;
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
 if(stage<11) {
  if(stage==0 && (n!=5||p[1]!=0||p[2]!=8||p[3]!=0||p[4]!=1)) {dropLink();return;}
  constexpr uint32_t required=0x01880300; // live RX/TX, CAD, host policy and RX activity
  if(stage==1 && (n!=76||p[1]!=1||(u32(p+2)&required)!=required)) {dropLink();return;}
  if(stage==3 && (n!=3||p[1]!=config.lora.sx126x_rx_boosted_gain)) {dropLink();return;}
  if(stage==4 && (n!=2||p[1]!=1)) {dropLink();return;}
  if(stage==9 && (n!=9||u16(p+1)!=15||u16(p+3)!=15)) {dropLink();return;}
  ++stage;if(stage==11) {ready=true;lastHeartbeat=millis();LOG_INFO("DXLR host-managed radio connected; queue=%u TX=%s",MAX_TX_QUEUE,config.lora.tx_enabled?"enabled":"disabled");}
 } else if(cmd==10)txRxStopped=true;
 else if(cmd==21)armOk=true;
 else if(cmd==20 && (n!=5||memcmp(p+1,"PONG",4))) {dropLink();return;}
 else if(cmd==38) {
  if(n!=4||p[1]>1) {dropLink();return;}
  activityClear=p[1]==0;
  if(!activityClear) {txScheduled=false;scheduled=nullptr;}
 }

}
void DxlrUartInterface::event(uint8_t cmd,uint16_t seq,const uint8_t *p,uint16_t n) {
 if(cmd==0x80) {dropLink();return;}
 if(cmd==0x83) {LOG_WARN("DXLR asynchronous radio error");dropLink();return;}
 if(cmd==0x82) {
  if(n!=9||seq!=txSequence||!sendingPacket)return;
  LOG_INFO("DXLR TX complete id=%u result=%u elapsed=%u",sendingPacket->id,p[0],u32(p+1));
  packetPool.release(sendingPacket);sendingPacket=nullptr;
  if(p[0]) {dropLink();return;}
  resumeRx();return;
 }
 if(cmd==0x85) {
  if(n!=10||seq!=cadSequence||!cadPending)return;
  cadPending=false;
  if(p[0]||p[1]>1) {dropLink();return;}
  LOG_INFO("DXLR CAD %s",p[1]?"busy":"clear");
  if(p[1]) {airTime->logAirtime(RX_ALL_LOG,u32(p+2));resumeRx();}
  else cadClear=true;
  return;
 }
 if(cmd!=0x81||!ready||n<31||n!=15u+p[14]||(p[13]&15)!=15)return;
 unsigned len=p[14];
 if(len<=16||len>255) {airTime->logAirtime(RX_ALL_LOG,getPacketTime(len));return;}
 PacketHeader h;memcpy(&h,p+15,sizeof(h));
 if(!h.from || h.from==UINT32_MAX) {airTime->logAirtime(RX_ALL_LOG,getPacketTime(len));return;}
 auto mp=packetPool.allocZeroed();if(!mp)return;
 mp->from=h.from;mp->to=h.to;mp->id=h.id;mp->channel=h.channel;
 mp->hop_limit=h.flags&7;mp->hop_start=(h.flags>>5)&7;mp->want_ack=!!(h.flags&8);mp->via_mqtt=!!(h.flags&16);
 mp->next_hop=mp->hop_start?h.next_hop:0;mp->relay_node=mp->hop_start?h.relay_node:0;
 mp->rx_rssi=(int16_t)u16(p+8)/4;mp->rx_snr=(int8_t)p[10]/4.0f;
 mp->which_payload_variant=meshtastic_MeshPacket_encrypted_tag;mp->encrypted.size=len-16;
 memcpy(mp->encrypted.bytes,p+31,len-16);
 airTime->logAirtime(RX_LOG,getPacketTime(len));
 deliverToReceiver(mp);
 if(!txRxStopped&&!cadPending) {txScheduled=false;scheduled=nullptr;activityClear=false;}
}
int32_t DxlrUartInterface::runOnce() {
 concurrency::LockGuard guard(&lock);
 for(unsigned count=0;count<1024&&serial.available();++count)consume(serial.read());
 if(used&&!Throttle::isWithinTimespanMs(rxAt,100)) {used=0;collecting=false;}
 if(pendingCommand) {if(!Throttle::isWithinTimespanMs(requestAt,3000)) {LOG_WARN("DXLR UART timeout command=%u stage=%u",pendingCommand,stage);dropLink();}return 5;}
 if(sendingPacket) {
  if((int32_t)(millis()-txDeadline)>=0) {LOG_WARN("DXLR TX watchdog; no automatic replay");dropLink();}
  return 5;
 }
 if(cadPending) {if(!Throttle::isWithinTimespanMs(cadAt,cadTimeout+1000))dropLink();return 5;}
 if(!ready) {
  double hz=(double)getFreq()*1000000.0;
  if(disabled||config.lora.region==0||!std::isfinite(hz)||
     !((hz>=433000000&&hz<=434000000)||(hz>=863000000&&hz<=870000000)))return 500;
  if(!stage && lastProbe && Throttle::isWithinTimespanMs(lastProbe,2000))return 50;
  configureNext();return 5;
 }
 auto front=txQueue.getFront();
 if(!front) {
  if(txRxStopped)resumeRx();
  else if(!Throttle::isWithinTimespanMs(lastHeartbeat,2000)) {lastHeartbeat=millis();request(20);}
  return 5;
 }
 if(!config.lora.tx_enabled) {dropLink();return 5;}
 // Queue cancellation/priority replacement invalidates any completed channel scan.
 // Return to RX before waiting for the new head, then perform a fresh activity check/CAD.
 if(!txScheduled||scheduled!=front) {
  if(txRxStopped)resumeRx();
  else {activityClear=false;scheduleTx();}
  return 5;
 }
 if((int32_t)(millis()-nextTxAt)<0)return 5;
 if(!airtimeAllowed()) {
  if(txRxStopped)resumeRx();
  else nextTxAt=millis()+1000;
  return 5;
 }
 if(!activityClear&&!txRxStopped) {request(38);return 5;}
 if(!txRxStopped) {request(10);return 5;}
 if(!cadClear) {
  uint8_t cad[7]={1,(uint8_t)(sf+13),10}; // same SX126x thresholds as RadioLib; 2 symbols
  cadTimeout=(uint32_t)ceil(((1u<<sf)/bw)*6)+100;
  put32(cad+3,cadTimeout);request(29,cad,7);
  cadSequence=sequence;cadAt=millis();cadPending=true;return 5;
 }
 if(!armOk) {const uint8_t token[]={'T','X','1','!'};request(21,token,4);return 5;}
 auto p=txQueue.dequeue();scheduled=nullptr;txScheduled=false;
 unsigned n=beginSending(p);uint32_t airtime=getPacketTime(n);
 if(n>255||!airtime||airtime>60000) {dropLink();return 5;}
 // Charge once before SEND: even an uncertain completion must not undercount RF airtime.
 airTime->logAirtime(TX_LOG,airtime);
 LOG_INFO("DXLR TX start id=%u bytes=%u airtime=%u",p->id,n,airtime);
 request(11,reinterpret_cast<const uint8_t *>(&radioBuffer),n);txSequence=sequence;
 lastAttempt=lastTxStart=millis();txDeadline=lastAttempt+airtime+1500;armOk=false;
 return 5;
}
#endif
