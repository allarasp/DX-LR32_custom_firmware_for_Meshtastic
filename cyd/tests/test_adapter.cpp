#include "DxlrUartInterface.h"
#include <iostream>
class Harness:public DxlrUartInterface {public:using DxlrUartInterface::runOnce;};
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
static void handshake(Harness &h,uint8_t minor=5) {
 h.runOnce();
 for(unsigned i=0;i<9;++i) {
  auto b=take();assert(!b.empty());const uint8_t expected[]={1,2,10,4,5,6,7,8,9};assert(b[2]==expected[i]);
  std::vector<uint8_t> p{0};
  if(i==0) {p={0,0,minor,0,1};const uint8_t golden[]={1,0,1,1,0,0,0,0x4a,0x95};assert(std::equal(b.begin(),b.end(),golden));}
  if(i==1) {p.resize(76);p[1]=1;p[2]=0xff;p[3]=0x0b;}
  if(i==3) {uint32_t f=b[7]|uint32_t(b[8])<<8|uint32_t(b[9])<<16|uint32_t(b[10])<<24;assert(f==433875000);}
  if(i==4)assert(b[7]==11&&b[12]==5&&b[13]==0);
  if(i==5)assert(b[7]==16&&b[9]==0xb4&&b[10]==0x24&&b[12]==1&&b[14]==255);
  if(i==6)assert(b[7]==247);
  if(i==7) {p.resize(9);p[1]=p[3]=15;}
  inject(1,b[2],seq(b),p);h.runOnce();
 }
}
static meshtastic_MeshPacket *packet() {auto p=packetPool.allocZeroed();p->from=0x12345678;p->to=UINT32_MAX;p->id=42;p->channel=8;p->encrypted.size=4;memcpy(p->encrypted.bytes,"test",4);return p;}
int main() {
 Harness h;assert(h.init());handshake(h);assert(h.getQueueStatus().free==1);
 assert(h.send(packet())==ERRNO_DISABLED&&packetPool.live.empty());
 std::vector<uint8_t> rx(34,0);rx[8]=0x84;rx[9]=0xfe;rx[10]=0xf9;rx[13]=15;rx[14]=19;
 PacketHeader header{UINT32_MAX,0x12345678,42,0x23,8,0,0};memcpy(rx.data()+15,&header,16);memcpy(rx.data()+31,"abc",3);
 inject(2,0x81,1,rx,true);h.runOnce();assert(!received);
 inject(2,0x81,1,rx);h.runOnce();assert(received&&received->from==0x12345678&&received->rx_rssi==-95&&received->rx_snr==-1.75f&&received->encrypted.size==3);
 packetPool.release(received);received=nullptr;
 config.lora.tx_enabled=true;assert(h.send(packet())==ERRNO_OK);h.runOnce();auto arm=take();assert(arm[2]==21&&!memcmp(arm.data()+7,"TX1!",4));
 inject(1,21,seq(arm),{0});h.runOnce();auto tx=take();assert(tx[2]==11&&tx[5]==20);assert(h.getQueueStatus().free==0);
 inject(1,11,seq(tx),{0});h.runOnce();assert(packetPool.live.size()==1);
 inject(2,0x82,seq(tx)+1,std::vector<uint8_t>(9));h.runOnce();assert(packetPool.live.size()==1);
 inject(2,0x82,seq(tx),std::vector<uint8_t>(9));h.runOnce();assert(packetPool.live.empty());
 assert(h.send(packet())==ERRNO_UNKNOWN);fakeNow+=32000;assert(h.send(packet())==ERRNO_OK);h.runOnce();arm=take();
 inject(1,21,seq(arm),{13});h.runOnce();assert(packetPool.live.empty());
 fakeNow+=6000;h.runOnce();auto probe=take();assert(probe[2]==1);fakeNow+=4000;h.runOnce();assert(packetPool.live.empty());
 assert(h.getPacketTime(32)>0&&h.getPacketTime(255)>h.getPacketTime(32));
 std::cout<<"CYD UART adapter tests passed: handshake, CRC, integer frequency, RX metadata, packet ownership, arm/TX completion, cooldown, rejection and reconnect\n";
}
