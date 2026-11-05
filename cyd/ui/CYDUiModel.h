#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
struct CydCivilTime {int year,month,day,hour,minute;};
inline bool cydParseDateTime(const char *s,CydCivilTime &out) {
 if(!s||strlen(s)!=16||s[4]!='-'||s[7]!='-'||s[10]!=' '||s[13]!=':')return false;
 for(unsigned i=0;i<16;++i)if(i!=4&&i!=7&&i!=10&&i!=13&&(s[i]<'0'||s[i]>'9'))return false;
 auto two=[s](int i){return (s[i]-'0')*10+s[i+1]-'0';};
 CydCivilTime t{two(0)*100+two(2),two(5),two(8),two(11),two(14)};
 if(t.year<2020||t.year>2099||t.month<1||t.month>12||t.hour>23||t.minute>59)return false;
 static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
 int maxDay=days[t.month-1]+(t.month==2&&t.year%4==0);
 if(t.day<1||t.day>maxDay)return false;
 out=t;return true;
}
inline bool cydParseUnsigned(const char *s,uint32_t lo,uint32_t hi,uint32_t &value) {
 if(!s||!*s)return false;
 uint32_t n=0;
 for(;*s;++s) {if(*s<'0'||*s>'9')return false;unsigned d=*s-'0';if(n>hi/10||(n==hi/10&&d>hi%10))return false;n=n*10+d;}
 if(n<lo)return false;
 value=n;return true;
}
struct CydConversation {
 uint32_t peer=0;uint8_t channel=0;
 bool operator==(const CydConversation &b)const{return peer==b.peer&&channel==b.channel;}
};
// Bounded UI-only unread state; text/history/delivery state stays in upstream MessageStore.
class CydUnread {
 struct Entry {CydConversation key;uint8_t unread=0;bool used=false;};
 Entry entries[24]{};unsigned next=0;
 public:
 uint8_t count(CydConversation key)const{for(const auto&e:entries)if(e.used&&e.key==key)return e.unread;return 0;}
 void markRead(CydConversation key){for(auto&e:entries)if(e.used&&e.key==key)e.unread=0;}
 void received(CydConversation key){
  for(auto&e:entries)if(e.used&&e.key==key){if(e.unread<99)++e.unread;return;}
  entries[next]={key,1,true};next=(next+1)%24;
 }
};
// The full wake gesture is swallowed through release so it cannot activate a control.
class CydWakeGate {
 bool swallow=false;
 public:
 bool accept(bool touched,bool screenOn){
  if(!touched){swallow=false;return false;}
  if(!screenOn)swallow=true;
  return !swallow;
 }
};

inline std::string cydMenuLabel(std::string text,size_t limit=34) {
 for(auto &c:text)if((unsigned char)c<32)c=' ';
 // Native selection renderer uses a 40-byte highlighted-line buffer (+ > and <).
 if(limit>34)limit=34;
 if(text.size()>limit){if(limit<=3)return std::string(limit,'.');size_t end=limit-3;while(end&&((unsigned char)text[end]&0xc0)==0x80)--end;text.resize(end);text+="...";}
 return text;
}
