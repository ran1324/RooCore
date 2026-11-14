#include <WifiSetup.h>
#include <cassert>
#include <cstring>
#include <iostream>
int main(){
  using namespace roonav;
  WifiCredentials c;assert(!c.valid());
  assert(c.assign("home",4,"password123",11));assert(c.valid());
  assert(!c.assign("",0,"",0));assert(c.valid()); // invalid input retains known-good record
  assert(!c.assign("home",4,"short",5));
  assert(!c.assign("bad\0ssid",8,"password123",11));
  assert(!c.assign("home",4,"bad\npassword",12));
  char s[34],p[65];memset(s,'s',33);s[33]=0;memset(p,'p',64);p[64]=0;
  assert(!c.assign(s,33,p,63));assert(!c.assign(s,32,p,64));
  assert(c.assign(s,32,p,63));assert(c.valid());
  assert(c.assign("open",4,"",0));assert(c.valid());
  assert(c.assign("בית",6,"password123",11));assert(c.valid());
  const auto original=c;
  for(size_t i=0;i<sizeof c;++i){c=original;reinterpret_cast<unsigned char*>(&c)[i]^=1;assert(!c.valid());}
  c=original;memset(c.ssid,'x',sizeof c.ssid);c.checksum=c.digest();assert(!c.valid());
  c=original;memset(c.password,'x',sizeof c.password);c.checksum=c.digest();assert(!c.valid());
  WifiSetupForm form;
  const char good[]="ssid=%D7%91%D7%99%D7%AA&password=hello+world&nonce=0123456789abcdef0123456789abcdef";
  assert(form.parse(good,strlen(good)));assert(!strcmp(form.ssid,"בית"));assert(!strcmp(form.password,"hello world"));
  const char *bad[]={"ssid=home&ssid=other&password=password&nonce=0123456789abcdef0123456789abcdef",
    "ssid=home%00&password=password&nonce=0123456789abcdef0123456789abcdef",
    "ssid=home%XZ&password=password&nonce=0123456789abcdef0123456789abcdef",
    "ssid=home&password=password&nonce=0123456789abcdef0123456789abcdeg",
    "ssid=home&password=password&nonce=0123456789abcdef0123456789abcdef&",
    "ssid=home&password=password", "ssid=home&password=password&unknown=0123456789abcdef0123456789abcdef"};
  for(const char *body:bad)assert(!form.parse(body,strlen(body)));
  uint32_t seed=1;char fuzz[514];
  for(unsigned iteration=0;iteration<10000;++iteration){
    const size_t size=iteration%sizeof fuzz;
    for(size_t i=0;i<size;++i){seed=seed*1664525u+1013904223u;fuzz[i]=char(seed>>24);}
    if(form.parse(fuzz,size))assert(WifiCredentials::input(form.ssid,strlen(form.ssid),form.password,strlen(form.password)));
  }
  WifiSetupWindow w;assert(!w.expired(100));
  w.start(0xfffffff0);assert(!w.expired(20));assert(w.expired(uint32_t(0xfffffff0+WifiSetupWindow::LifetimeMs)));
  w.joining=true;w.attempting=0xfffffff0;assert(!w.joinExpired(20));assert(w.joinExpired(uint32_t(0xfffffff0+WifiSetupWindow::ConnectMs)));
  std::cout<<"Wi-Fi record validation/corruption, UTF-8 names, open networks and wrap-safe setup budgets passed\n";
}
