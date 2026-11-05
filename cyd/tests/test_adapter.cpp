#include "DxlrUartInterface.h"
#include <iostream>
class Harness:public DxlrUartInterface {public:bool autoKeepalive=true;int32_t runOnce() override;};
static uint16_t crc(uint16_t c,uint8_t b) {c^=uint16_t(b)<<8;for(unsigned i=0;i<8;++i)c=(c<<1)^((c&0x8000)?0x1021:0);return c;}
static void inject(uint8_t kind,uint8_t cmd,uint16_t seq,std::vector<uint8_t> p,bool corrupt=false) {
 std::vector<uint8_t> b{1,kind,cmd,uint8_t(seq),uint8_t(seq>>8),uint8_t(p.size()),uint8_t(p.size()>>8)};
 b.insert(b.end(),p.begin(),p.end());uint16_t c=0xffff;for(auto v:b)c=crc(c,v);if(corrupt)c^=1;b.push_back(c);b.push_back(c>>8);
 auto &q=HardwareSerial::latest->incoming;q.push_back(0x7e);
 for(auto v:b) {if(v==0x7e||v==0x7d) {q.push_back(0x7d);q.push_back(v^0x20);}else q.push_back(v);}q.push_back(0x7e);
}
static std::vector<uint8_t> take() {
 auto &out=HardwareSerial::latest->outgoing;if(out.empty())return {};
 assert(out.front()==0x7e&&out.back()==0x7e);std::vector<uint8_t> b;
 for(unsigned i=1;i<out.size()-1;++i) {uint8_t v=out[i];if(v==0x7d)v=out[++i]^0x20;b.push_back(v);}
 uint16_t c=0xffff;for(unsigned i=0;i<b.size()-2;++i)c=crc(c,b[i]);assert(b[b.size()-2]==uint8_t(c)&&b.back()==uint8_t(c>>8));out.clear();return b;
}
static uint16_t seq(const std::vector<uint8_t>& b) {return b[3]|uint16_t(b[4])<<8;}
// The test controller services ordinary heartbeat traffic while scenario commands remain explicit.
int32_t Harness::runOnce() {
 auto delay=DxlrUartInterface::runOnce();
 auto &raw=HardwareSerial::latest->outgoing;
 if(autoKeepalive&&raw.size()>3&&raw[3]==20) {
  auto ping=take();inject(1,20,seq(ping),{0,'P','O','N','G'});DxlrUartInterface::runOnce();
 }
 return delay;
}
static void handshake(Harness &h) {
 h.runOnce();
 for(unsigned i=0;i<11;++i) {
  auto b=take();assert(!b.empty());const uint8_t expected[]={1,2,10,28,37,4,5,6,7,8,9};assert(b[2]==expected[i]);
  std::vector<uint8_t> p{0};
  if(i==0) {p={0,0,8,0,1};const uint8_t golden[]={1,0,1,1,0,0,0,0x4a,0x95};assert(std::equal(b.begin(),b.end(),golden));}
  if(i==1) {p.resize(76);p[1]=1;p[2]=0xff;p[3]=0xfb;p[4]=0xff;p[5]=1;}
  if(i==3) {assert(b[7]==1);p={0,1,0x96};}
  if(i==4) {assert(b[7]==1);p={0,1};}
  if(i==5) {uint32_t f=b[7]|uint32_t(b[8])<<8|uint32_t(b[9])<<16|uint32_t(b[10])<<24;assert(f==433875000);}
  if(i==6)assert(b[7]==11&&b[12]==5&&b[13]==0);
  if(i==7)assert(b[7]==16&&b[9]==0xb4&&b[10]==0x24&&b[12]==1&&b[14]==255);
  if(i==8)assert(b[7]==10); // Actual configured power; no hard-coded -9 dBm.
  if(i==9) {p.resize(9);p[1]=p[3]=15;}
  inject(1,b[2],seq(b),p);h.runOnce();
 }
}
static meshtastic_MeshPacket *packet(uint32_t id=42,uint32_t priority=64) {
 auto p=packetPool.allocZeroed();p->from=0x12345678;p->to=UINT32_MAX;p->id=id;p->channel=8;
 p->priority=priority;p->encrypted.size=4;memcpy(p->encrypted.bytes,"test",4);return p;
}
static std::vector<uint8_t> beginCad(Harness &h) {
 h.runOnce();fakeNow+=1001;h.runOnce();auto activity=take();assert(activity[2]==38);
 inject(1,38,seq(activity),{0,0,0,0});h.runOnce();auto stop=take();assert(stop[2]==10);
 inject(1,10,seq(stop),{0});h.runOnce();auto cad=take();assert(cad[2]==29&&cad[7]==1&&cad[8]==24&&cad[9]==10);
 inject(1,29,seq(cad),{0});h.runOnce();assert(take().empty());return cad;
}
static std::vector<uint8_t> finishCad(Harness &h,const std::vector<uint8_t>& cad,bool busy=false) {
 std::vector<uint8_t> event(10);event[1]=busy;event[2]=60;
 inject(2,0x85,seq(cad)+1,event);h.runOnce();assert(take().empty()); // Sequence isolation.
 inject(2,0x85,seq(cad),event);h.runOnce();auto b=take();
 if(busy) {assert(b[2]==9);inject(1,9,seq(b),{0});h.runOnce();return {};}
 assert(b[2]==21&&!memcmp(b.data()+7,"TX1!",4));
 inject(1,21,seq(b),{0});h.runOnce();auto tx=take();assert(tx[2]==11);return tx;
}
static void complete(Harness &h,const std::vector<uint8_t>& tx) {
 inject(1,11,seq(tx),{0});h.runOnce();
 inject(2,0x82,seq(tx)+1,std::vector<uint8_t>(9));h.runOnce();assert(take().empty());
 inject(2,0x82,seq(tx),std::vector<uint8_t>(9));h.runOnce();auto resume=take();assert(resume[2]==9&&resume[5]==4);
 inject(1,9,seq(resume),{0});h.runOnce();
}
int main() {
 Harness h;assert(h.init());handshake(h);assert(h.getQueueStatus().free==MAX_TX_QUEUE);
 assert(h.send(packet())==ERRNO_DISABLED&&packetPool.live.empty());
 std::vector<uint8_t> rx(34,0);rx[8]=0x84;rx[9]=0xfe;rx[10]=0xf9;rx[13]=15;rx[14]=19;
 PacketHeader header{UINT32_MAX,0x12345678,42,0x23,8,0,0};memcpy(rx.data()+15,&header,16);memcpy(rx.data()+31,"abc",3);
 inject(2,0x81,1,rx,true);h.runOnce();assert(!received&&!fakeAirTime.totals[RX_LOG]);
 inject(2,0x81,1,rx);h.runOnce();assert(received&&received->from==0x12345678&&received->rx_rssi==-95&&received->rx_snr==-1.75f&&received->encrypted.size==3);
 // Upstream counts every log call toward channel utilization: valid RX is logged once.
 assert(fakeAirTime.totals[RX_LOG]==h.getPacketTime(19)&&fakeAirTime.totals[RX_ALL_LOG]==0);
 packetPool.release(received);received=nullptr;
 header.from=0;memcpy(rx.data()+15,&header,16);inject(2,0x81,2,rx);h.runOnce();assert(!received);
 assert(fakeAirTime.totals[RX_ALL_LOG]==h.getPacketTime(19)&&fakeAirTime.totals[RX_LOG]==h.getPacketTime(19));
 config.lora.tx_enabled=true;
 assert(h.send(packet(1))==ERRNO_OK);h.runOnce();assert(take().empty()); // Random delay, never immediate send.
 assert(h.findInTxQueue(0x12345678,1));assert(h.cancelSending(0x12345678,1));assert(packetPool.live.empty());
 assert(h.send(packet(2))==ERRNO_OK);assert(h.send(packet(3,120))==ERRNO_OK);
 auto cad=beginCad(h);auto tx=finishCad(h,cad);assert(tx[15]==3); // ACK priority beats ordinary packet.
 assert(fakeAirTime.totals[TX_LOG]==h.getPacketTime(20));complete(h,tx);
 assert(h.getQueueStatus().free==MAX_TX_QUEUE-1);cad=beginCad(h);tx=finishCad(h,cad);complete(h,tx);
 assert(packetPool.live.empty()); // Two TX in a few seconds, no 31-second pause.
 assert(h.send(packet(4))==ERRNO_OK);cad=beginCad(h);finishCad(h,cad,true);
 assert(h.findInTxQueue(0x12345678,4)&&packetPool.live.size()==1); // Busy CAD defers, retains packet.
 cad=beginCad(h);tx=finishCad(h,cad);complete(h,tx);assert(packetPool.live.empty());
 // Active RX must not be interrupted for CAD.
 assert(h.send(packet(5))==ERRNO_OK);h.runOnce();fakeNow+=1001;h.runOnce();auto activity=take();assert(activity[2]==38);
 inject(1,38,seq(activity),{0,1,4,0});h.runOnce();assert(take().empty());assert(h.cancelSending(0x12345678,5));
 // A canceled queued rebroadcast during CAD must not go over the air.
 assert(h.send(packet(6))==ERRNO_OK);cad=beginCad(h);assert(h.cancelSending(0x12345678,6));
 std::vector<uint8_t> clear(10);inject(2,0x85,seq(cad),clear);h.runOnce();auto resume=take();assert(resume[2]==9);
 inject(1,9,seq(resume),{0});h.runOnce();assert(packetPool.live.empty());
 // A new higher-priority head after CAD needs a fresh scan, not a stale clear result.
 assert(h.send(packet(60))==ERRNO_OK);cad=beginCad(h);
 assert(h.send(packet(61,120))==ERRNO_OK);
 std::vector<uint8_t> clear2(10);inject(2,0x85,seq(cad),clear2);h.runOnce();resume=take();assert(resume[2]==9);
 inject(1,9,seq(resume),{0});h.runOnce();
 assert(h.cancelSending(0x12345678,60)&&h.cancelSending(0x12345678,61));
 // Duty/channel limits are checked at dispatch, after enqueue as well as at the router.
 assert(h.send(packet(7))==ERRNO_OK);fakeAirTime.txPercent=10;h.runOnce();fakeNow+=1001;h.runOnce();assert(take().empty());
 fakeAirTime.txPercent=0;fakeAirTime.channelAllowed=false;fakeNow+=1001;h.runOnce();assert(take().empty());
 fakeAirTime.channelAllowed=true;cad=beginCad(h);tx=finishCad(h,cad);complete(h,tx);
 // Full priority queue replaces lower priority and remains bounded.
 for(unsigned n=0;n<MAX_TX_QUEUE;++n)assert(h.send(packet(100+n))==ERRNO_OK);
 assert(h.getQueueStatus().free==0);assert(h.send(packet(200))==ERRNO_UNKNOWN);
 assert(h.send(packet(201,120))==ERRNO_OK&&packetPool.live.size()==MAX_TX_QUEUE);
 assert(h.cancelSending(0x12345678,201));
 for(unsigned n=0;n<MAX_TX_QUEUE;++n)h.cancelSending(0x12345678,100+n);
 assert(packetPool.live.empty());
 auto late=packet(300);late->rx_rssi=-50;assert(h.send(late)==ERRNO_OK);
 h.clampToLateRebroadcastWindow(0x12345678,300);assert(late->tx_after==fakeNow+500);
 late->hop_limit=2;assert(!h.removePendingTXPacket(0x12345678,300,2));assert(h.removePendingTXPacket(0x12345678,300,3));
 // Unknown TX completion: account once, release everything, never replay.
 assert(h.send(packet(8))==ERRNO_OK);cad=beginCad(h);tx=finishCad(h,cad);
 uint32_t charged=fakeAirTime.totals[TX_LOG];inject(1,11,seq(tx),{0});h.runOnce();fakeNow+=10000;h.runOnce();
 assert(packetPool.live.empty()&&fakeAirTime.totals[TX_LOG]==charged&&take().empty());
 fakeNow+=2001;h.runOnce();auto version=take();assert(version[2]==1); // Recovery starts discovery, not SEND.
 // This is deliberately an old controller: it cannot negotiate host-managed TX.
 inject(1,1,seq(version),{0,0,7,0,1});h.runOnce();assert(take().empty());
 assert(h.send(packet(400))==ERRNO_DISABLED&&packetPool.live.empty());
 // The large-packet path is no longer restricted to two seconds.
 assert(h.getPacketTime(255)>2000);
 // Idle controller loss must become UART OFF without requiring a user send.
 Harness idle;idle.autoKeepalive=false;assert(idle.init());handshake(idle);
 assert(DxlrUartInterface::linkConnected());fakeNow+=2001;idle.runOnce();auto ping=take();assert(ping[2]==20);
 inject(1,20,seq(ping),{0,'P','O','N','G'});idle.runOnce();assert(DxlrUartInterface::linkConnected());
 fakeNow+=2001;idle.runOnce();assert(take()[2]==20);fakeNow+=3001;idle.runOnce();assert(!DxlrUartInterface::linkConnected());
 std::cout<<"CYD tests passed: v0.8 negotiation, real upstream priority queue/cancellation, CAD clear/busy/sequence, active RX deferral, airtime, duty/channel limits, rapid TX, late windows and uncertain-TX recovery\n";
}
