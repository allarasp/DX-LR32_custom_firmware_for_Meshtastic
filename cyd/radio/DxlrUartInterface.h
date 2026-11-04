#pragma once
#ifdef USE_DXLR_UART
#include "RadioInterface.h"
#include "concurrency/OSThread.h"
#include <HardwareSerial.h>
#include "concurrency/Lock.h"
class DxlrUartInterface : public RadioInterface, protected concurrency::OSThread {
 public:
  DxlrUartInterface();
  bool init() override;
  bool reconfigure() override;
  bool canSleep() override { return false; }
  ErrorCode send(meshtastic_MeshPacket *p) override;
  uint32_t getPacketTime(uint32_t length,bool received=false) override;
  meshtastic_QueueStatus getQueueStatus() override;
 protected:
  int32_t runOnce() override;
 private:
  concurrency::Lock lock;
  HardwareSerial serial{2};
  uint8_t body[281];
  uint16_t used=0,sequence=0,pendingSequence=0,txSequence=0;
  uint8_t pendingCommand=0,stage=0;
  uint32_t requestAt=0,lastAttempt=0,lastProbe=0,rxAt=0;
  bool collecting=false,escaped=false,discard=false,ready=false,attempted=false,armOk=false;
  meshtastic_MeshPacket *queued=nullptr;
  void consume(uint8_t value);
  void frame();
  void request(uint8_t command,const uint8_t *payload=nullptr,uint16_t length=0);
  void configureNext();
  void dropLink();
  void releaseTx();
  void event(uint8_t command,uint16_t seq,const uint8_t *p,uint16_t length);
};
#endif
