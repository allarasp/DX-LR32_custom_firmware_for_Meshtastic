#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <deque>
#include <vector>
#include <set>
#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)
#define ERRNO_OK 0
#define ERRNO_DISABLED 34
#define ERRNO_UNKNOWN 32
#define meshtastic_Config_LoRaConfig_RegionCode_EU_433 2
#define meshtastic_MeshPacket_encrypted_tag 4
using ErrorCode=int;
inline uint32_t fakeNow=1;
inline uint32_t millis() {return fakeNow;}
struct FakeConfig {struct {bool tx_enabled=false;int region=2;} lora;};
inline FakeConfig config;
struct PacketHeader {uint32_t to,from,id;uint8_t flags,channel,next_hop,relay_node;};
static_assert(sizeof(PacketHeader)==16);
struct meshtastic_MeshPacket {
 uint32_t to=0,from=0,id=0;uint8_t channel=0,hop_limit=0,hop_start=0,next_hop=0,relay_node=0;
 bool want_ack=false,via_mqtt=false;int rx_rssi=0;float rx_snr=0;int which_payload_variant=4;
 struct {size_t size=0;uint8_t bytes[239]={0};} encrypted;
};
struct meshtastic_QueueStatus {uint32_t res=0,mesh_packet_id=0,free=0,maxlen=0;};
struct Pool {
 std::set<meshtastic_MeshPacket *> live;
 meshtastic_MeshPacket *allocZeroed() {auto p=new meshtastic_MeshPacket;live.insert(p);return p;}
 void release(meshtastic_MeshPacket *p) {assert(live.erase(p)==1);delete p;}
} inline packetPool;
inline meshtastic_MeshPacket *received=nullptr;
class RadioInterface {
 protected:
 bool disabled=false;float bw=250;uint8_t sf=11,cr=5;uint16_t preambleLength=16;
 meshtastic_MeshPacket *sendingPacket=nullptr;
 struct {PacketHeader header;uint8_t payload[240];} radioBuffer{};
 size_t beginSending(meshtastic_MeshPacket *p) {assert(!sendingPacket);sendingPacket=p;radioBuffer.header={p->to,p->from,p->id,0,p->channel,0,0};memcpy(radioBuffer.payload,p->encrypted.bytes,p->encrypted.size);return 16+p->encrypted.size;}
 void deliverToReceiver(meshtastic_MeshPacket *p) {assert(!received);received=p;}
 public:
 virtual ~RadioInterface()=default;
 virtual bool init() {return true;}
 virtual bool reconfigure() {return true;}
 virtual bool canSleep() {return true;}
 virtual ErrorCode send(meshtastic_MeshPacket *)=0;
 virtual uint32_t getPacketTime(uint32_t,bool=false)=0;
 virtual meshtastic_QueueStatus getQueueStatus() {return {};}
 float getFreq() {return 433.875f;}
};
class HardwareSerial {
 public:
 inline static HardwareSerial *latest=nullptr;
 std::deque<uint8_t> incoming;std::vector<uint8_t> outgoing;
 explicit HardwareSerial(int) {latest=this;}
 void setRxBufferSize(unsigned) {}
 void begin(unsigned baud,int,int rx,int tx) {assert(baud==115200&&rx==22&&tx==27);}
 unsigned available() {return incoming.size();}
 uint8_t read() {auto b=incoming.front();incoming.pop_front();return b;}
 void write(uint8_t b) {outgoing.push_back(b);}
};
#define SERIAL_8N1 0
namespace concurrency {
 class Lock {};
 class LockGuard {public:explicit LockGuard(Lock *) {}};
 class OSThread {public:OSThread(const char *,unsigned) {} virtual ~OSThread()=default;protected:virtual int32_t runOnce()=0;};
}
class Throttle {public:static bool isWithinTimespanMs(uint32_t last,uint32_t n) {return uint32_t(millis()-last)<n;}};
