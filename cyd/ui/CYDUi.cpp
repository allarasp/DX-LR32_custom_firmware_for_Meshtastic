#include "configuration.h"
#ifdef CYD_ST7789
#include "CYDUi.h"
#include "CYDUiModel.h"
#include "MessageStore.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Channels.h"
#include "PowerFSM.h"
#include "input/InputBroker.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/draw/MenuHandler.h"
#include "graphics/draw/MessageRenderer.h"
#include "graphics/draw/NotificationRenderer.h"
#include "modules/CannedMessageModule.h"
#include "modules/NodeInfoModule.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

using graphics::menuHandler;
namespace graphics {extern bool hasUnreadMessage;}
namespace {
using Action=std::function<void()>;
Action pending;
uint32_t lastInteraction=0,observedInfoInterval=0;
bool infoIntervalKnown=false;
CydWakeGate wakeGate;
CydUnread unread;
CydConversation selected;
bool pinned=false,manualTime=false,screenWasOn=false;
constexpr unsigned MaxOptions=16;
std::array<std::string,MaxOptions> labels;
std::array<const char*,MaxOptions> pointers;
std::string title;
void defer(Action action){pending=std::move(action);}
void menu(const std::string &heading,const std::vector<std::string>& options,std::function<void(int)> callback,int initial=0) {
 title=heading;
 unsigned count=std::min<unsigned>(options.size(),MaxOptions);
 for(unsigned i=0;i<count;++i){labels[i]=cydMenuLabel(options[i]);pointers[i]=labels[i].c_str();}
 graphics::BannerOverlayOptions b;
 b.message=title.c_str();b.optionsArrayPtr=pointers.data();b.optionsCount=count;b.InitialSelected=initial;b.durationMs=0;
 b.bannerCallback=[callback,heading](int choice){LOG_INFO("CYD menu selection: %s index=%d",heading.c_str(),choice);defer([callback,choice]{callback(choice);});};
 LOG_INFO("CYD menu: %s options=%u",heading.c_str(),count);
 screen->showOverlayBanner(b);
}
void note(const std::string &text,Action back) {menu(text,{"Back"},[back](int){back();});}
void confirm(const std::string &text,Action save,Action cancel) {
 menu(text,{"Cancel","Save"},[save,cancel](int n){if(n==1)save();else cancel();});
}
void saveConfig(bool radio=false){
 if(radio)service->reloadConfig(SEGMENT_CONFIG);else nodeDB->saveToDisk(SEGMENT_CONFIG);
 LOG_INFO("CYD settings saved radio=%u screen_timeout=%u",radio,config.display.screen_on_secs);
}
std::string peerName(uint32_t id) {
 auto n=nodeDB->getMeshNode(id);char hex[12];snprintf(hex,sizeof(hex),"!%08lx",(unsigned long)id);
 return n&&n->has_user&&n->user.long_name[0]?std::string(n->user.long_name):std::string(hex);
}
CydConversation keyFor(const StoredMessage &m){
 if(m.type==MessageType::BROADCAST)return {0,m.channelIndex};
 return {m.sender==nodeDB->getNodeNum()?m.dest:m.sender,0};
}
std::string conversationName(CydConversation key) {
 return key.peer?peerName(key.peer):std::string("#")+channels.getName(key.channel);
}
void openConversation(CydConversation key){
 LOG_INFO("CYD conversation peer=%08lx channel=%u",(unsigned long)key.peer,key.channel);
 selected=key;pinned=true;unread.markRead(key);
 graphics::MessageRenderer::setThreadMode(key.peer?graphics::MessageRenderer::ThreadMode::DIRECT:graphics::MessageRenderer::ThreadMode::CHANNEL,key.peer?-1:key.channel,key.peer);
 graphics::MessageRenderer::clearMessageCache();graphics::MessageRenderer::resetScrollState();
 screen->cydShowMessageFrame();
}
void compose(){
 const uint8_t ch=selected.peer?channels.getPrimaryIndex():selected.channel;
 LOG_INFO("CYD compose destination=%08lx channel=%u",(unsigned long)selected.peer,ch);
 if(cannedMessageModule)cannedMessageModule->LaunchFreetextWithDestination(selected.peer?selected.peer:NODENUM_BROADCAST,ch);
}
void channelsMenu(){
 std::vector<std::string> options{"Back"};std::vector<uint8_t> ids;
 for(unsigned i=0;i<channels.getNumChannels()&&options.size()<MaxOptions;++i) {
  const auto &ch=channels.getByIndex(i);if(ch.role==meshtastic_Channel_Role_DISABLED)continue;
  ids.push_back(i);auto name=conversationName({0,(uint8_t)i});auto count=unread.count({0,(uint8_t)i});
  if(count)name+=" ["+std::to_string(count)+"]";
  options.push_back(name);
 }
 menu("Channels",options,[ids](int n){if(n==0)cydChatMenu();else {openConversation({0,ids[n-1]});cydConversationMenu();}});
}
void nodesMenu(unsigned page=0){
 constexpr unsigned perPage=8;
 std::vector<uint32_t> peers;
 for(unsigned i=0;i<nodeDB->getNumMeshNodes();++i){auto n=nodeDB->getMeshNodeByIndex(i);if(n&&n->num!=nodeDB->getNodeNum())peers.push_back(n->num);}
 if(peers.empty()){note("No other nodes heard yet",cydChatMenu);return;}
 unsigned pages=(peers.size()+perPage-1)/perPage;if(page>=pages)page=pages-1;
 std::vector<std::string> options{"Back"};std::vector<uint32_t> shown;
 for(unsigned i=page*perPage;i<std::min<unsigned>(peers.size(),(page+1)*perPage);++i){
  shown.push_back(peers[i]);auto name=peerName(peers[i]);auto count=unread.count({peers[i],0});
  if(count)name+=" ["+std::to_string(count)+"]";
  options.push_back(name);
 }
 int previous=-1,next=-1;
 if(page){previous=options.size();options.push_back("Previous nodes");}
 if(page+1<pages){next=options.size();options.push_back("More nodes");}
 menu("Direct messages "+std::to_string(page+1)+"/"+std::to_string(pages),options,[shown,page,previous,next](int n){
  if(!n)cydChatMenu();else if(n==previous)nodesMenu(page-1);else if(n==next)nodesMenu(page+1);
  else {openConversation({shown[n-1],0});cydConversationMenu();}
 });
}
void recentMenu(){
 std::vector<CydConversation> keys;std::vector<std::string> options{"Back"};
 const auto &messages=messageStore.getMessages();
 for(auto it=messages.rbegin();it!=messages.rend()&&options.size()<MaxOptions;++it){
  auto key=keyFor(*it);if(std::find(keys.begin(),keys.end(),key)!=keys.end())continue;
  keys.push_back(key);auto name=conversationName(key);auto count=unread.count(key);
  if(count)name+=" ["+std::to_string(count)+"]";
  std::string preview=MessageStore::getText(*it);for(auto &c:preview)if(c=='\n'||c=='\r')c=' ';
  preview=cydMenuLabel(preview,20);
  options.push_back(name+": "+preview);
 }
 if(keys.empty()){note("No recent messages",cydChatMenu);return;}
 menu("Recent conversations",options,[keys](int n){if(!n)cydChatMenu();else openConversation(keys[n-1]);});
}
void choose(const std::string &name,const std::vector<std::string> &names,const std::vector<int> &values,int current,
            std::function<void(int)> apply,Action back,bool radio=false){
 std::vector<std::string> options{"Back"};int initial=0;std::string currentLabel=std::to_string(current);
 for(unsigned i=0;i<names.size();++i){options.push_back((values[i]==current?"* ":"")+names[i]);if(values[i]==current){initial=i+1;currentLabel=names[i];}}
 menu(name+"\nCurrent: "+currentLabel,options,[name,names,values,apply,back,radio](int n){
  if(!n){back();return;}int value=values[n-1];auto label=names[n-1];
  confirm(name+"\n"+label,[apply,value,back,radio]{apply(value);saveConfig(radio);back();},back);
 },initial);
}
void rebroadcast(){
 choose("Rebroadcast mode",{"All","All (skip decoding)","Local channels only","Known nodes only","None","Core ports only"},
  {0,1,2,3,4,5},config.device.rebroadcast_mode,[](int v){config.device.rebroadcast_mode=(meshtastic_Config_DeviceConfig_RebroadcastMode)v;},cydSystemMenu);
}
void interval(){
 const std::vector<int> values{900,1800,3600,10800,21600,43200,86400};
 const std::vector<std::string> names{"15 minutes","30 minutes","1 hour","3 hours","6 hours","12 hours","24 hours"};
 std::vector<std::string> options{"Back"};
 uint32_t current=config.device.node_info_broadcast_secs?config.device.node_info_broadcast_secs:10800;
 for(unsigned i=0;i<values.size();++i)options.push_back((current==(uint32_t)values[i]?"* ":"")+names[i]);
 options.push_back("Custom minutes");
 menu("Node info interval",options,[values,names,current](int n){
  if(!n){cydSystemMenu();return;}
  if(n==8){
   screen->showNumberPicker("Minutes: -/+ then OK",0,5,[](uint32_t v){defer([v]{
    uint32_t minutes=0;if(!cydParseUnsigned(std::to_string(v).c_str(),1,10080,minutes)){note("Use 1 to 10080 minutes",interval);return;}
    confirm("Node info interval\n"+std::to_string(minutes)+" minutes",[minutes]{config.device.node_info_broadcast_secs=minutes*60;saveConfig();cydSystemMenu();},interval);
   });});graphics::NotificationRenderer::currentNumber=current/60;return;
  }
  int seconds=values[n-1];confirm("Node info interval\n"+names[n-1],[seconds]{config.device.node_info_broadcast_secs=seconds;saveConfig();cydSystemMenu();},interval);
 });
}
void screenTimeout(){
 choose("Screen on for (seconds)",{"1 minute","2 minutes","3 minutes","Never"},{60,120,180,0},config.display.screen_on_secs,
  [](int v){config.display.screen_on_secs=v;lastInteraction=millis();},cydSystemMenu);
}
void region(){
 choose("LoRa region",{"EU 433 MHz","EU 868 MHz"},{(int)meshtastic_Config_LoRaConfig_RegionCode_EU_433,(int)meshtastic_Config_LoRaConfig_RegionCode_EU_868},
  config.lora.region,[](int v){config.lora.region=(meshtastic_Config_LoRaConfig_RegionCode)v;},cydLoraMenu,true);
}
void preset(){
 choose("Modem preset",{"Short Turbo","Short Fast","Short Slow","Medium Fast","Medium Slow","Long Turbo","Long Fast","Long Moderate","Lite Fast","Lite Slow","Narrow Fast","Narrow Slow","Long Slow (legacy)","Very Long Slow (legacy)"},
  {8,6,5,4,3,9,0,7,10,11,12,13,1,2},config.lora.modem_preset,[](int v){config.lora.use_preset=true;config.lora.modem_preset=(meshtastic_Config_LoRaConfig_ModemPreset)v;config.lora.coding_rate=0;config.lora.channel_num=0;config.lora.override_frequency=0;},cydLoraMenu,true);
}
void hops(){choose("Hop limit",{"Default (3)","1","2","3","4","5","6","7"},{0,1,2,3,4,5,6,7},config.lora.hop_limit,
 [](int v){config.lora.hop_limit=v;},cydLoraMenu,true);}
void gain(){choose("RX boosted gain",{"Off","On"},{0,1},config.lora.sx126x_rx_boosted_gain,
 [](int v){config.lora.sx126x_rx_boosted_gain=v;},cydLoraMenu,true);}
void powerPicker(){
 std::vector<std::string> names;std::vector<int> values;
 int ceiling=myRegion&&myRegion->powerLimit?std::min<int>(22,myRegion->powerLimit):22;
 for(int v: {-9,-3,1,5,10,14,17,20,22})if(v<=ceiling){names.push_back(std::to_string(v)+" dBm");values.push_back(v);}
 if(std::find(values.begin(),values.end(),ceiling)==values.end()){values.push_back(ceiling);names.push_back(std::to_string(ceiling)+" dBm");}
 choose("Transmit power",names,values,config.lora.tx_power,[](int v){config.lora.tx_power=v;},cydLoraMenu,true);
}
CydCivilTime clockDraft{};
void clockEditor();
void clockField(int field){
 const char *names[]={"Year","Month","Day","Hour","Minute"};
 int values[]={clockDraft.year,clockDraft.month,clockDraft.day,clockDraft.hour,clockDraft.minute};
 screen->showNumberPicker(names[field],0,field==0?4:2,[field](uint32_t v){defer([field,v]{
  const uint32_t low[]={2020,1,1,0,0},high[]={2099,12,31,23,59};
  if(v<low[field]||v>high[field]){note("Value out of range",clockEditor);return;}
  int *fields[]={&clockDraft.year,&clockDraft.month,&clockDraft.day,&clockDraft.hour,&clockDraft.minute};
  *fields[field]=v;clockEditor();
 });});
 graphics::NotificationRenderer::currentNumber=values[field];
}
void clockEditor(){
 std::vector<std::string> options{"Cancel","Year: "+std::to_string(clockDraft.year),"Month: "+std::to_string(clockDraft.month),
  "Day: "+std::to_string(clockDraft.day),"Hour: "+std::to_string(clockDraft.hour),"Minute: "+std::to_string(clockDraft.minute),"Save date / time"};
 menu("Set local date / time",options,[](int n){
  if(!n){cydClockMenu();return;}
  if(n<=5){clockField(n-1);return;}
  char formatted[24];snprintf(formatted,sizeof(formatted),"%04d-%02d-%02d %02d:%02d",clockDraft.year,clockDraft.month,clockDraft.day,clockDraft.hour,clockDraft.minute);
  CydCivilTime civil{};if(!cydParseDateTime(formatted,civil)){note("Invalid calendar date",clockEditor);return;}
  struct tm t{};t.tm_year=civil.year-1900;t.tm_mon=civil.month-1;t.tm_mday=civil.day;t.tm_hour=civil.hour;t.tm_min=civil.minute;t.tm_isdst=-1;
  time_t epoch=mktime(&t);struct tm check{};
  if(epoch<0||!localtime_r(&epoch,&check)||check.tm_hour!=civil.hour||check.tm_mday!=civil.day){note("Time invalid in this timezone",clockEditor);return;}
  confirm(std::string("Set local time?\n")+formatted,[epoch]{struct timeval tv{epoch,0};auto result=perhapsSetRTC(RTCQualityDevice,&tv,true);
   manualTime=result==RTCSetResultSuccess;
   if(!manualTime)note("Time rejected\nCheck date against firmware build",cydClockMenu);else cydClockMenu();
  },clockEditor);
 });
}
void setTime(){
 time_t now=getValidTime(RTCQualityDevice);if(!now)now=BUILD_EPOCH;
 struct tm local{};localtime_r(&now,&local);
 clockDraft={local.tm_year+1900,local.tm_mon+1,local.tm_mday,local.tm_hour,local.tm_min};clockEditor();
}
void timezonePicker(){
 const std::vector<std::string> names{"UTC","EU Western","EU Central","EU Eastern (Tallinn)"};
 const std::vector<std::string> zones{"UTC0","GMT0BST,M3.5.0/1,M10.5.0","CET-1CEST,M3.5.0,M10.5.0/3","EET-2EEST,M3.5.0/3,M10.5.0/4"};
 std::vector<std::string> options{"Back"};for(unsigned i=0;i<names.size();++i)options.push_back((zones[i]==config.device.tzdef?"* ":"")+names[i]);
 menu("Timezone",options,[names,zones](int n){if(!n){cydClockMenu();return;}
  auto zone=zones[n-1];confirm("Timezone\n"+names[n-1],[zone]{snprintf(config.device.tzdef,sizeof(config.device.tzdef),"%s",zone.c_str());setenv("TZ",zone.c_str(),1);tzset();saveConfig();cydClockMenu();},cydClockMenu);
 });
}
} // namespace

bool cydComposerActive(){return cannedMessageModule&&static_cast<MeshModule*>(cannedMessageModule)->interceptingKeyboardInput();}
bool cydLeaveComposer(){
 if(!cydComposerActive())return false;
 if(inputBroker){InputEvent e{};e.source="cyd-back";e.inputEvent=INPUT_BROKER_CANCEL;inputBroker->injectInputEvent(&e);}
 if(!cydComposerActive())defer([]{openConversation(selected);});return true;
}
bool cydMenuActive(){return screen&&(screen->isOverlayBannerShowing()||cydComposerActive());}
void cydUiTick(){
 if(!screen)return;
 if(infoIntervalKnown&&observedInfoInterval!=config.device.node_info_broadcast_secs&&nodeInfoModule)
  nodeInfoModule->triggerImmediateNodeInfoCheck();
 observedInfoInterval=config.device.node_info_broadcast_secs;infoIntervalKnown=true;
 if(!lastInteraction)lastInteraction=millis();
 bool on=screen->isScreenOn();if(on&&!screenWasOn)lastInteraction=millis();screenWasOn=on;
 if(pending){auto action=std::move(pending);pending=nullptr;action();lastInteraction=millis();}
 uint32_t timeout=config.display.screen_on_secs;
 if(timeout&&screen->isScreenOn()&&(uint32_t)(millis()-lastInteraction)>=timeout*1000ULL){LOG_INFO("CYD display timeout; radio/BLE remain active");screen->setOn(false);screenWasOn=false;}
 if(pinned&&screen->isScreenOn()&&screen->cydIsMessageFrame()&&!cydMenuActive())unread.markRead(selected);
}
bool cydAcceptTouch(bool touched){
 bool on=screen&&screen->isScreenOn();
 bool accepted=wakeGate.accept(touched,on);
 if(touched){lastInteraction=millis();if(!on&&screen){LOG_INFO("CYD wake touch swallowed");screen->setOn(true);powerFSM.trigger(EVENT_INPUT);}}
 return accepted;
}
void cydChatMenu(){menu("Chats",{"Back","Recent conversations","Channels","Direct messages / New DM"},[](int n){
 if(n==1)recentMenu();else if(n==2)channelsMenu();else if(n==3)nodesMenu();
});}
void cydConversationMenu(){
 auto mode=graphics::MessageRenderer::getThreadMode();
 if(mode==graphics::MessageRenderer::ThreadMode::ALL){cydChatMenu();return;}
 selected=mode==graphics::MessageRenderer::ThreadMode::DIRECT?CydConversation{graphics::MessageRenderer::getThreadPeer(),0}:CydConversation{0,(uint8_t)graphics::MessageRenderer::getThreadChannel()};
 pinned=true;
 menu(conversationName(selected),{"Back to conversation","Compose","Choose another chat"},[](int n){if(n==1)compose();else if(n==2)cydChatMenu();else openConversation(selected);});
}
bool cydKeepConversation(){return screen&&(screen->isOverlayBannerShowing()||cydComposerActive()||(pinned&&screen->cydIsMessageFrame()));}
void cydOnMessage(const StoredMessage &m,const meshtastic_MeshPacket &){
 auto key=keyFor(m);if(m.sender!=nodeDB->getNodeNum()&&!(pinned&&selected==key&&screen&&screen->isScreenOn()&&screen->cydIsMessageFrame()&&!cydMenuActive())){unread.received(key);graphics::hasUnreadMessage=true;}
}
void cydSystemMenu(){
 menu("System settings",{"Back","Notifications","Display options","Screen on for","Bluetooth","Rebroadcast mode","Node info interval","LoRa settings","Reboot / Shutdown"},[](int n){
  switch(n){case 1:screen->requestMenu(menuHandler::BuzzerModeMenuPicker);break;
   case 2:screen->requestMenu(menuHandler::ScreenOptionsMenu);break;
   case 3:screenTimeout();break;case 4:screen->requestMenu(menuHandler::BluetoothToggleMenu);break;
   case 5:rebroadcast();break;case 6:interval();break;case 7:cydLoraMenu();break;
   case 8:screen->requestMenu(menuHandler::PowerMenu);break;default:break;}
 });
}
void cydLoraMenu(){menu("LoRa settings",{"Back","Region","Modem preset","Hop limit","RX boosted gain","Transmit power"},[](int n){
 switch(n){case 0:cydSystemMenu();break;case 1:region();break;case 2:preset();break;case 3:hops();break;case 4:gain();break;case 5:powerPicker();break;}
});}
void cydClockMenu(){
 auto quality=getRTCQuality();const char *source=quality==RTCQualityNone?"Not set":quality==RTCQualityDevice&&manualTime?"Manual":"Synchronized";
 menu(std::string("Clock: ")+source,{"Back","Set date and time","Timezone","12 / 24 hour","Clock face"},[](int n){
  if(n==1)setTime();else if(n==2)timezonePicker();else if(n==3)choose("Clock format",{"24 hour","12 hour"},{0,1},config.display.use_12h_clock,[](int v){config.display.use_12h_clock=v;},cydClockMenu);
  else if(n==4)screen->requestMenu(menuHandler::ClockFacePicker);
 });
}
#endif
