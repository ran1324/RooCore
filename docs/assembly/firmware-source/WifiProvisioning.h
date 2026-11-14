#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_system.h>
#include "WifiSetup.h"
#include "WifiSetupPage.h"
// Owned exclusively by Arduino setup()/loop(); no callbacks mutate robot state.
class RooWifiProvisioning {
  WebServer &web;
  roonav::WifiCredentials stored, candidate;
  roonav::WifiSetupWindow window;
  String nonce, apName;
  const char *state="ready";
  bool scanned=false;
  uint32_t lastScan=0;
  char rawBody[512]{};
  size_t rawUsed=0;
  bool rawComplete=false, rawRejected=false;
  void rejectRaw(int code){rawRejected=true;memset(rawBody,0,sizeof rawBody);rawUsed=0;web.send(code);web.client().stop();}
  void receiveBody(){
    if(!web.hasRaw()){rejectRaw(415);return;}
    auto &raw=web.raw();
    if(raw.status==RAW_START){
      rawUsed=0;rawComplete=false;rawRejected=false;
      if(!access()){rawRejected=true;web.client().stop();return;}
      const String length=web.header("Content-Length"),type=web.header("Content-Type");
      if(web.header("Origin")!="http://192.168.4.1"){rejectRaw(403);return;}
      if(web.header("Transfer-Encoding").length() ||
         (type!="application/x-www-form-urlencoded" && type!="application/x-www-form-urlencoded;charset=UTF-8")){
        rejectRaw(415);return;
      }
      if(!length.length() || length.length()>3){rejectRaw(413);return;}
      for(size_t i=0;i<length.length();++i)if(length[i]<'0'||length[i]>'9'){rejectRaw(400);return;}
      if(length.toInt()<1 || length.toInt()>512){rejectRaw(413);return;}
    }else if(raw.status==RAW_WRITE && !rawRejected){
      if(raw.currentSize>sizeof rawBody-rawUsed){rejectRaw(413);return;}
      memcpy(rawBody+rawUsed,raw.buf,raw.currentSize);rawUsed+=raw.currentSize;
    }else if(raw.status==RAW_END && !rawRejected)rawComplete=true;
    else if(raw.status==RAW_ABORTED)rawRejected=true;
  }
  void headers() {
    web.sendHeader("Cache-Control","no-store");
    web.sendHeader("X-Content-Type-Options","nosniff");
    web.sendHeader("Content-Security-Policy","default-src 'none'; script-src 'self'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; form-action 'self'; base-uri 'none'");
    web.sendHeader("Referrer-Policy","no-referrer");
  }
  bool peer() {
    const IPAddress ip=web.client().remoteIP();
    return window.active && web.client().localIP()==IPAddress(192,168,4,1) && ip[0]==192 && ip[1]==168 && ip[2]==4 && ip[3]>=2 && ip[3]<255;
  }
  bool access() {
    headers();
    if(!peer() || (web.hostHeader()!="192.168.4.1" && web.hostHeader()!="192.168.4.1:80")) {
      web.send(404);return false;
    }
    return true;
  }
  static String quote(const String &s) {
    String out="\"";
    for(size_t i=0;i<s.length();++i){const uint8_t c=s[i];if(c=='"'||c=='\\'){out+='\\';out+=char(c);}else if(c<32){char b[7];snprintf(b,sizeof b,"\\u%04x",c);out+=b;}else out+=char(c);}
    return out+'"';
  }
  bool save() {
    Preferences prefs;
    if(!prefs.begin("roocore-wifi",false))return false;
    bool ok=prefs.putBytes("network",&candidate,sizeof candidate)==sizeof candidate;
    roonav::WifiCredentials readback;
    ok=ok && prefs.getBytes("network",&readback,sizeof readback)==sizeof readback &&
       readback.valid() && !memcmp(&readback,&candidate,sizeof candidate);
    prefs.end();if(ok)stored=candidate;return ok;
  }
public:
  explicit RooWifiProvisioning(WebServer &server):web(server){}
  bool active()const{return window.active;}
  // Missing/corrupt record falls back only to an explicitly compiled network.
  bool begin(const char *ssid,const char *password) {
    WiFi.persistent(false);WiFi.setAutoReconnect(true);WiFi.mode(WIFI_STA);
    Preferences prefs;
    if(prefs.begin("roocore-wifi",true)){
      if(prefs.getBytesLength("network")==sizeof stored)prefs.getBytes("network",&stored,sizeof stored);
      prefs.end();
    }
    if(!stored.valid())stored.assign(ssid,strlen(ssid),password,strlen(password));
    if(stored.valid()){WiFi.begin(stored.ssid,stored.password);return true;}
    return false;
  }
  bool open(const char *password,uint32_t now) {
    // Empty public defaults cannot accidentally create an open/default-password AP.
    const size_t n=strlen(password);
    if(n<12 || n>63)return false;
    for(size_t i=0;i<n;++i)if(password[i]<33 || password[i]>126)return false;
    uint8_t random[16];esp_fill_random(random,sizeof random);nonce="";
    for(auto byte:random){char b[3];snprintf(b,sizeof b,"%02x",byte);nonce+=b;}
    const uint64_t mac=ESP.getEfuseMac();char name[32];
    snprintf(name,sizeof name,"RooCore-Setup-%06lx",static_cast<unsigned long>(mac&0xffffff));apName=name;
    WiFi.setAutoReconnect(false);WiFi.disconnect(false,false);WiFi.mode(WIFI_AP_STA);
    const IPAddress ip(192,168,4,1);
    if(!WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0)) ||
       !WiFi.softAP(apName.c_str(),password,1,0,1,false,WIFI_AUTH_WPA2_PSK,WIFI_CIPHER_TYPE_CCMP)){
      WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);return false;
    }
    window.start(now);state="ready";scanned=false;lastScan=0;
    Serial.print("Wi-Fi setup: ");Serial.println(apName); // never log any password/key
    return true;
  }
  void close() {
    WiFi.scanDelete();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);window.active=false;nonce="";candidate=roonav::WifiCredentials();
    if(stored.valid() && WiFi.status()!=WL_CONNECTED)WiFi.begin(stored.ssid,stored.password);
  }
  // Return true when the control-owned hold can be released. Never start a mission.
  bool tick(uint32_t now,bool permitted) {
    if(!window.active)return false;
    if(!permitted || window.expired(now) || (window.saved && uint32_t(now-window.connected)>=8000)){
      close();return true;
    }
    if(window.joining){
      if(WiFi.status()==WL_CONNECTED && WiFi.SSID()==candidate.ssid && WiFi.localIP()!=IPAddress()){
        if(save()){window.saved=true;window.connected=now;state="saved";}else{state="storage_failed";}
        window.joining=false;candidate=roonav::WifiCredentials();
      }else if(window.joinExpired(now)){
        WiFi.disconnect(false,false);window.joining=false;state="failed";candidate=roonav::WifiCredentials();
      }
    }
    return false;
  }
  void routes() {
    web.on("/setup",HTTP_GET,[this]{if(access())web.send_P(200,"text/html; charset=utf-8",RooWifiSetupPage);});
    web.on("/setup/app.js",HTTP_GET,[this]{if(access())web.send_P(200,"application/javascript; charset=utf-8",RooWifiSetupJs);});
    web.on("/setup/state",HTTP_GET,[this]{if(access())web.send(200,"application/json",String("{\"state\":\"")+state+"\",\"nonce\":\""+nonce+"\"}");});
    web.on("/setup/scan",HTTP_GET,[this]{
      if(!access())return;
      if(window.joining || window.saved){web.send(409);return;}
      int count=WiFi.scanComplete();
      if(count==WIFI_SCAN_RUNNING){web.send(200,"application/json","{\"scanning\":true}");return;}
      if(!scanned || ((web.arg("refresh")=="1" || count<0) && uint32_t(millis()-lastScan)>10000)){
        WiFi.scanDelete();scanned=true;lastScan=millis();WiFi.scanNetworks(true,false);
        web.send(200,"application/json","{\"scanning\":true}");return;
      }
      String body="{\"scanning\":false,\"networks\":[";
      for(int i=0;i<count && i<30;++i){if(i)body+=',';body+="{\"ssid\":"+quote(WiFi.SSID(i))+",\"rssi\":"+String(WiFi.RSSI(i))+",\"secure\":"+(WiFi.encryptionType(i)==WIFI_AUTH_OPEN?"false":"true")+"}";}
      web.send(200,"application/json",body+"]}");
      // Retain result briefly so UI polling does not immediately start another scan.
    });
    web.on("/setup/connect",HTTP_POST,[this]{
      if(!access())return;
      if(rawRejected || !rawComplete){web.send(400);return;}
      roonav::WifiSetupForm form;
      const bool valid=form.parse(rawBody,rawUsed);memset(rawBody,0,sizeof rawBody);rawUsed=0;rawComplete=false;
      if(!valid){web.send(400);return;}
      if(web.header("Origin")!="http://192.168.4.1" || String(form.nonce)!=nonce || nonce.length()!=32){web.send(403);return;}
      if(window.joining || window.saved){web.send(409);return;}
      if(!candidate.assign(form.ssid,strlen(form.ssid),form.password,strlen(form.password))){web.send(400);return;}
      WiFi.scanDelete();scanned=false;WiFi.disconnect(false,false);
      WiFi.begin(candidate.ssid,candidate.password);window.joining=true;window.attempting=millis();state="connecting";
      web.send(202,"application/json","{\"accepted\":true}");
    },[this]{receiveBody();});
    web.onNotFound([this]{
      if(peer() && web.method()==HTTP_GET){headers();web.sendHeader("Location","http://192.168.4.1/setup");web.send(302);}else web.send(404);
    });
  }
};
