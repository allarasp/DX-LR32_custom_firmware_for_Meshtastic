#include "CYDUiModel.h"
#include <cassert>
#include <iostream>
int main(){
 CydCivilTime t{};
 assert(cydParseDateTime("2028-02-29 23:59",t)&&t.year==2028&&t.month==2&&t.day==29);
 for(const char*s:{"2027-02-29 12:00","2028-04-31 12:00","2028-13-01 12:00","2028-00-01 12:00","2028-01-00 12:00","2028-01-01 24:00","2028-01-01 23:60","2028-01-01 01:01x","2028-01-01 0a:01","2028/01/01 01:01","2019-01-01 01:01"})assert(!cydParseDateTime(s,t));
 uint32_t n=0;assert(cydParseUnsigned("10080",1,10080,n)&&n==10080);
 assert(!cydParseUnsigned("99999999999999999",1,10080,n)&&!cydParseUnsigned("0",1,10080,n)&&!cydParseUnsigned("-1",1,10080,n));
 CydUnread unread;CydConversation publicChat{0,0},privateChat{42,0},secondary{0,1};
 unread.received(publicChat);unread.received(privateChat);unread.received(publicChat);
 assert(unread.count(publicChat)==2&&unread.count(privateChat)==1&&unread.count(secondary)==0);
 unread.markRead(publicChat);assert(unread.count(publicChat)==0&&unread.count(privateChat)==1);
 for(int i=0;i<120;++i)unread.received(privateChat);
 assert(unread.count(privateChat)==99);
 for(unsigned i=100;i<200;++i)unread.received({i,0});
 assert(unread.count({199,0})==1&&unread.count({100,0})==0);
 assert(cydMenuLabel(std::string(100,'x')).size()==34);
 assert(cydMenuLabel("a\nb")=="a b");
 auto unicode=cydMenuLabel(std::string(30,'x')+"\xc3\xa4"+std::string(20,'y'));
 assert(unicode==std::string(30,'x')+"...");
 CydWakeGate gate;assert(!gate.accept(true,false));assert(!gate.accept(true,true));assert(!gate.accept(false,true));
 assert(gate.accept(true,true));assert(!gate.accept(false,true));
 std::cout<<"CYD GUI model tests passed: date/calendar bounds, numeric bounds, per-conversation unread isolation/eviction and wake-gesture suppression\n";
}
