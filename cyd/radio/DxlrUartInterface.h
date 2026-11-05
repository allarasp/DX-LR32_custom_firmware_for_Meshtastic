#pragma once
#ifdef USE_DXLR_UART
#include "RadioInterface.h"
#include "MeshPacketQueue.h"
#include "concurrency/OSThread.h"
#include <HardwareSerial.h>
#include "concurrency/Lock.h"

class DxlrUartInterface : public RadioInterface, protected concurrency::OSThread {
 public:
  DxlrUartInterface();
  static bool linkConnected() {return instance&&instance->ready;}
  static bool transmitting() {return instance&&(instance->sendingPacket||(instance->lastTxStart&&(uint32_t)(millis()-instance->lastTxStart)<1200));}
  bool init() override;
  bool reconfigure() override;
  bool canSleep() override { return false; }
  ErrorCode send(meshtastic_MeshPacket *p) override;
  using RadioInterface::getPacketTime;
  uint32_t getPacketTime(uint32_t length,bool received=false) override;
  meshtastic_QueueStatus getQueueStatus() override;
  bool cancelSending(NodeNum from,PacketId id) override;
  bool findInTxQueue(NodeNum from,PacketId id) override;
  bool removePendingTXPacket(NodeNum from,PacketId id,uint32_t hop_limit_lt) override;
  void clampToLateRebroadcastWindow(NodeNum from,PacketId id) override;
 protected:
  int32_t runOnce() override;
 private:
  static DxlrUartInterface *instance;
  concurrency::Lock lock;
  HardwareSerial serial{2};
  MeshPacketQueue txQueue{MAX_TX_QUEUE};
  uint8_t body[281];
  uint16_t used=0,sequence=0,pendingSequence=0,txSequence=0,cadSequence=0;
  uint8_t pendingCommand=0,stage=0;
  uint32_t requestAt=0,lastAttempt=0,lastProbe=0,rxAt=0,txDeadline=0,cadAt=0,cadTimeout=0;
  uint32_t nextTxAt=0;
  uint32_t lastHeartbeat=0;
  meshtastic_MeshPacket *scheduled=nullptr;
  bool collecting=false,escaped=false,discard=false,ready=false,armOk=false,txRxStopped=false;
  bool activityClear=false,cadPending=false,cadClear=false,txScheduled=false;
  void consume(uint8_t value);
  void frame();
  void request(uint8_t command,const uint8_t *payload=nullptr,uint16_t length=0);
  void configureNext();
  void dropLink();
  void releaseTx();
  void resumeRx();
  void scheduleTx();
  bool airtimeAllowed();
  void event(uint8_t command,uint16_t seq,const uint8_t *p,uint16_t length);
};
#endif
