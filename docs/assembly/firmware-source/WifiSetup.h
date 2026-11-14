#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace roonav {
// One NVS blob: never commit a password separately from its SSID.
struct WifiCredentials {
  uint32_t magic = 0x31574652; // RFW1, local storage format
  char ssid[33]{};
  char password[64]{};
  uint8_t reserved[3]{};
  uint32_t checksum = 0;
  uint32_t digest() const {
    uint32_t h = 2166136261u;
    const auto *p = reinterpret_cast<const uint8_t *>(this);
    for (size_t i=0;i<offsetof(WifiCredentials,checksum);++i) h=(h^p[i])*16777619u;
    return h;
  }
  static bool input(const char *s, size_t sn, const char *p, size_t pn) {
    if (!s || !p || !sn || sn>32 || pn>63 || (pn && pn<8)) return false;
    for(size_t i=0;i<sn;++i) if(uint8_t(s[i])<32 || uint8_t(s[i])==127)return false;
    for(size_t i=0;i<pn;++i) if(uint8_t(p[i])<32 || uint8_t(p[i])>126)return false;
    return true;
  }
  bool valid() const {
    return magic==0x31574652 && !reserved[0] && !reserved[1] && !reserved[2] &&
      memchr(ssid,0,sizeof ssid) && memchr(password,0,sizeof password) &&
      input(ssid,strlen(ssid),password,strlen(password)) && checksum==digest();
  }
  bool assign(const char *s, size_t sn, const char *p, size_t pn) {
    if(!input(s,sn,p,pn))return false;
    *this=WifiCredentials();memcpy(ssid,s,sn);memcpy(password,p,pn);checksum=digest();return true;
  }
};
// All durations are software UX budgets, not claims about physical standstill.
struct WifiSetupWindow {
  static constexpr uint32_t LifetimeMs=300000, ConnectMs=25000;
  uint32_t opened=0, attempting=0, connected=0;
  bool active=false, joining=false, saved=false;
  void start(uint32_t now){opened=now;attempting=connected=0;active=true;joining=saved=false;}
  bool expired(uint32_t now)const{return active && uint32_t(now-opened)>=LifetimeMs;}
  bool joinExpired(uint32_t now)const{return joining && uint32_t(now-attempting)>=ConnectMs;}
};
struct WifiSetupForm {
  char ssid[33]{},password[64]{},nonce[33]{};
  static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
  static bool decode(const char *p,size_t n,char *out,size_t cap){
    size_t used=0;
    for(size_t i=0;i<n;++i){
      uint8_t c=p[i];
      if(c=='%'){if(i+2>=n || hex(p[i+1])<0 || hex(p[i+2])<0)return false;c=uint8_t(hex(p[i+1])*16+hex(p[i+2]));i+=2;}
      else if(c=='+')c=' ';
      if(!c || used+1>=cap)return false;
      out[used++]=char(c);
    }
    out[used]=0;return true;
  }
  bool parse(const char *body,size_t n){
    *this=WifiSetupForm();if(!body || !n || n>512)return false;
    unsigned seen=0;size_t start=0;
    while(start<n){
      size_t end=start;while(end<n && body[end]!='&')++end;
      size_t equal=start;while(equal<end && body[equal]!='=')++equal;
      if(equal==end)return false;
      char key[10];if(!decode(body+start,equal-start,key,sizeof key))return false;
      unsigned bit;char *out;size_t cap;
      if(!strcmp(key,"ssid")){bit=1;out=ssid;cap=sizeof ssid;}
      else if(!strcmp(key,"password")){bit=2;out=password;cap=sizeof password;}
      else if(!strcmp(key,"nonce")){bit=4;out=nonce;cap=sizeof nonce;}
      else return false;
      if((seen&bit) || !decode(body+equal+1,end-equal-1,out,cap))return false;
      seen|=bit;start=end+1;if(start==n)return false;
    }
    if(seen!=7 || strlen(nonce)!=32 || !WifiCredentials::input(ssid,strlen(ssid),password,strlen(password)))return false;
    for(char c:nonce)if(c && hex(c)<0)return false;
    return true;
  }
};

}
