#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <deque>
#include <vector>
#include <set>
#include <algorithm>
template<typename... Args> inline void stubLog(Args&&...) {}
#define LOG_INFO(...) stubLog(__VA_ARGS__)
#define LOG_WARN(...) stubLog(__VA_ARGS__)
#define LOG_DEBUG(...) stubLog(__VA_ARGS__)
#define MAX_TX_QUEUE 16
#define meshtastic_MeshPacket_decoded_tag 3
#define meshtastic_PortNum_ROUTING_APP 5
#define meshtastic_PortNum_TEXT_MESSAGE_APP 1
#define meshtastic_PortNum_ADMIN_APP 6
using NodeNum=uint32_t;
using PacketId=uint32_t;
enum {meshtastic_MeshPacket_Priority_UNSET=0,meshtastic_MeshPacket_Priority_DEFAULT=64,meshtastic_MeshPacket_Priority_RELIABLE=70,meshtastic_MeshPacket_Priority_RESPONSE=80,meshtastic_MeshPacket_Priority_HIGH=100,meshtastic_MeshPacket_Priority_ACK=120};
#define ERRNO_OK 0
#define ERRNO_DISABLED 34
#define ERRNO_UNKNOWN 32
#define meshtastic_Config_LoRaConfig_RegionCode_EU_433 2
#define meshtastic_MeshPacket_encrypted_tag 4
using ErrorCode=int;
inline uint32_t fakeNow=1;
inline uint32_t millis() {return fakeNow;}
struct FakeConfig {struct {bool tx_enabled=false;int region=2;bool override_duty_cycle=false;bool sx126x_rx_boosted_gain=true;} lora;};
inline FakeConfig config;
struct PacketHeader {uint32_t to,from,id;uint8_t flags,channel,next_hop,relay_node;};
static_assert(sizeof(PacketHeader)==16);
struct meshtastic_MeshPacket {
 uint32_t to=0,from=0,id=0;uint8_t channel=0,hop_limit=0,hop_start=0,next_hop=0,relay_node=0;
 uint32_t priority=64,tx_after=0;
 struct {int portnum=1;uint32_t request_id=0;bool want_response=false;} decoded;
 bool want_ack=false,via_mqtt=false;int rx_rssi=0;float rx_snr=0;int which_payload_variant=4;
 struct {size_t size=0;uint8_t bytes[239]={0};} encrypted;
};
inline NodeNum getFrom(const meshtastic_MeshPacket *p) {return p->from;}
inline bool isFromUs(const meshtastic_MeshPacket *p) {return p->from==0x12345678;}
struct meshtastic_QueueStatus {uint32_t res=0,mesh_packet_id=0,free=0,maxlen=0;};
struct Pool {
 std::set<meshtastic_MeshPacket *> live;
 meshtastic_MeshPacket *allocZeroed() {auto p=new meshtastic_MeshPacket;live.insert(p);return p;}
 void release(meshtastic_MeshPacket *p) {assert(live.erase(p)==1);delete p;}
} inline packetPool;
inline meshtastic_MeshPacket *received=nullptr;
class RadioInterface {
 protected:
 bool disabled=false;int8_t power=10;uint32_t lastTxStart=0;float bw=250;uint8_t sf=11,cr=5;uint16_t preambleLength=16;
 meshtastic_MeshPacket *sendingPacket=nullptr;
 struct {PacketHeader header;uint8_t payload[240];} radioBuffer{};
 size_t beginSending(meshtastic_MeshPacket *p) {assert(!sendingPacket);sendingPacket=p;radioBuffer.header={p->to,p->from,p->id,0,p->channel,0,0};memcpy(radioBuffer.payload,p->encrypted.bytes,p->encrypted.size);return 16+p->encrypted.size;}
 void deliverToReceiver(meshtastic_MeshPacket *p) {assert(!received);received=p;}
 public:
 virtual ~RadioInterface()=default;
 void limitPower(int8_t maximum) {power=std::min(power,maximum);}
 virtual bool init() {return true;}
 virtual bool reconfigure() {return true;}
 virtual bool canSleep() {return true;}
 virtual ErrorCode send(meshtastic_MeshPacket *)=0;
 virtual uint32_t getPacketTime(uint32_t,bool=false)=0;
 virtual meshtastic_QueueStatus getQueueStatus() {return {};}
 virtual bool cancelSending(NodeNum,PacketId) {return false;}
 virtual bool findInTxQueue(NodeNum,PacketId) {return false;}
 virtual bool removePendingTXPacket(NodeNum,PacketId,uint32_t) {return false;}
 virtual void clampToLateRebroadcastWindow(NodeNum,PacketId) {}
 uint32_t getPacketTime(const meshtastic_MeshPacket *p,bool received=false) {return getPacketTime(p->encrypted.size+16,received);}
 uint32_t getTxDelayMsec() {return 100;}
 uint32_t getTxDelayMsecWeighted(meshtastic_MeshPacket *) {return 200;}
 uint32_t getTxDelayMsecWeightedWorst(float) {return 500;}
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

enum reportTypes {TX_LOG,RX_LOG,RX_ALL_LOG};
struct FakeRegion {float dutyCycle=10;} inline fakeRegion;
inline FakeRegion *myRegion=&fakeRegion;
struct FakeAirTime {
 uint32_t totals[3]={};float txPercent=0;bool channelAllowed=true;
 void logAirtime(reportTypes type,uint32_t ms) {totals[type]+=ms;}
 bool isTxAllowedChannelUtil() {return channelAllowed;}
 float utilizationTXPercent() {return txPercent;}
} inline fakeAirTime;
inline FakeAirTime *airTime=&fakeAirTime;
