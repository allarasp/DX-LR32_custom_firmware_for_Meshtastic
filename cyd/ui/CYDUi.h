#pragma once
#include <cstdint>
struct StoredMessage;
struct _meshtastic_MeshPacket;
void cydUiTick();
bool cydAcceptTouch(bool touched);
void cydSystemMenu();
void cydClockMenu();
void cydLoraMenu();
void cydChatMenu();
void cydConversationMenu();
bool cydKeepConversation();
void cydOnMessage(const StoredMessage &,const _meshtastic_MeshPacket &);
bool cydMenuActive();
bool cydComposerActive();
bool cydLeaveComposer();
