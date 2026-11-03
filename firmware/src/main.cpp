#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "recorder_core.h"
#if __has_include("network_config.h")
#include "network_config.h"
#else
#include "network_config.example.h"
#endif

using namespace roocore;
WiFiClient link;
BrcClock brc;
IPAddress serverIp;
bool configured=false, ready=false, recording=false, pending=false, wheels=true;
bool helloSent=false;
uint32_t bootId, capture=0, sequence=0, overruns=0, dropped=0;
uint32_t lastSerialDrain=0;
uint32_t lastAttempt=0, lastLease=0, lastStatus=0, startAt=0, connectedAt=0;
char commandLine[48]; size_t commandSize=0; bool commandOverflow=false;
char deviceId[28];

// Never wait for TCP space. Count discarded bytes so the server marks a gap.
bool sendLine(const char *line) {
  size_t n=strlen(line);
  if (!link.connected() || size_t(link.availableForWrite())<n) return false;
  if (link.write(reinterpret_cast<const uint8_t *>(line),n)!=n) {
    link.stop(); // A partial JSON record cannot be resumed on this connection.
    return false;
  }
  return true;
}
void pauseTelemetry() {
  if (recording || pending) {
    const uint8_t pause[]={150,0}; Serial.write(pause,sizeof(pause));
  }
  recording=false; pending=false;
}
void status() {
  char line[256];
  snprintf(line,sizeof(line),"{\"type\":\"status\",\"boot\":%lu,\"capture\":%lu,\"ms\":%lu,\"recording\":%s,\"overruns\":%lu,\"dropped\":%lu}\n",
    (unsigned long)bootId,(unsigned long)capture,(unsigned long)millis(),recording?"true":"false",
    (unsigned long)overruns,(unsigned long)dropped);
  sendLine(line);
}
void consumeCommand(const char *line) {
  Command cmd=parseCommand(line);
  if (cmd==Command::Ready && helloSent) { ready=true; lastLease=millis(); return; }
  if (!ready) return;
  if (cmd==Command::Ping) { lastLease=millis(); return; }
  if (cmd==Command::Stop) { pauseTelemetry(); status(); return; }
  if (cmd!=Command::StartBasic && cmd!=Command::StartWheels) return;
  bool requestedWheels=cmd==Command::StartWheels;
  lastLease=millis();
  if ((recording || pending) && wheels==requestedWheels) return;
  pauseTelemetry();
  wheels=requestedWheels;
  ++capture; sequence=0; overruns=0; dropped=0;
  // Start/Passive only; let the robot run its own cleaning program.
  Serial.write(uint8_t(128));
  pending=true; startAt=millis();
}
void readCommands() {
  // Bound each pass; a busy server cannot starve the serial reader.
  for (size_t n=0;n<256 && link.available();++n) {
    char ch=link.read();
    if (ch=='\n') {
      commandLine[commandSize]=0;
      if (!commandOverflow) consumeCommand(commandLine);
      commandSize=0; commandOverflow=false;
    } else if (ch!='\r') {
      if (commandSize+1<sizeof(commandLine)) commandLine[commandSize++]=ch;
      else commandOverflow=true;
    }
  }
}
void pumpSerial() {
  if (Serial.hasOverrun()) ++overruns;
  if (recording && Serial.available()<256 && !elapsed(millis(),lastSerialDrain,5)) return;
  lastSerialDrain=millis();
  uint8_t bytes[256]; size_t n=0;
  while (Serial.available() && n<sizeof(bytes)) bytes[n++]=Serial.read();
  if (!recording || !n) return;
  char line[768];
  int prefix=snprintf(line,sizeof(line),"{\"type\":\"serial\",\"boot\":%lu,\"capture\":%lu,\"seq\":%lu,\"ms\":%lu,\"overruns\":%lu,\"dropped\":%lu,\"hex\":\"",
    (unsigned long)bootId,(unsigned long)capture,(unsigned long)sequence++,
    (unsigned long)millis(),(unsigned long)overruns,(unsigned long)dropped);
  size_t at=size_t(prefix);
  const char *hex="0123456789abcdef";
  for (size_t i=0;i<n;++i) { line[at++]=hex[bytes[i]>>4]; line[at++]=hex[bytes[i]&15]; }
  memcpy(line+at,"\"}\n",4);
  if (!sendLine(line)) dropped+=n;
}
void serviceNetwork() {
  if (WiFi.status()!=WL_CONNECTED || !link.connected()) {
    if (ready || helloSent) {
      pauseTelemetry(); ready=false; helloSent=false;
      commandSize=0; commandOverflow=false; link.stop();
    }
    if (configured && WiFi.status()==WL_CONNECTED && elapsed(millis(),lastAttempt,3000)) {
      lastAttempt=millis();
      // ESP8266 connect timeout is bounded, unlike an unbounded reconnect loop.
      if (link.connect(serverIp,SERVER_PORT)) {
        link.setNoDelay(true); connectedAt=millis();
      }
    }
    return;
  }
  if (!helloSent) {
    char hello[320];
    snprintf(hello,sizeof(hello),"{\"type\":\"hello\",\"protocol\":2,\"device\":\"%s\",\"boot\":%lu,\"token\":\"%s\"}\n",
      deviceId,(unsigned long)bootId,DEVICE_TOKEN);
    helloSent=sendLine(hello);
  }
  readCommands();
  if (!ready && elapsed(millis(),connectedAt,5000)) link.stop();
  if (ready && elapsed(millis(),lastStatus,1000)) { lastStatus=millis(); status(); }
}
void setup() {
  // Upstream 600-series pinout: UART0 GPIO1/GPIO3, GPIO2 for BRC.
  digitalWrite(BRC_PIN,HIGH); pinMode(BRC_PIN,OUTPUT);
  Serial.setRxBufferSize(2048);
  Serial.begin(115200, SERIAL_8N1, SERIAL_FULL, TX_PIN);
  Serial.setDebugOutput(false);
  // Enable the internal RX pull-up while keeping the UART pin function.
  pinMode(RX_PIN,SPECIAL);
  bootId=os_random();
  snprintf(deviceId,sizeof(deviceId),"roocore-%06x",ESP.getChipId());
  configured=strcmp(WIFI_SSID,"CONFIGURE_WIFI") && strcmp(DEVICE_TOKEN,"CONFIGURE_TOKEN") && serverIp.fromString(SERVER_IP);
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  WiFi.hostname(deviceId);
  link.setTimeout(100);
  if (configured) WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
}
void loop() {
  if (brc.update(millis())) digitalWrite(BRC_PIN,brc.low?LOW:HIGH);
  pumpSerial();
  serviceNetwork();
  if (ready && elapsed(millis(),lastLease,LEASE_MS)) {
    pauseTelemetry(); link.stop(); ready=false; helloSent=false;
  }
  if (pending && elapsed(millis(),startAt,50)) {
    // Drain old bytes before requesting this capture's profile.
    while (Serial.available()) Serial.read();
    uint8_t command[32]; size_t n=streamCommand(wheels,command);
    Serial.write(command,n); pending=false; recording=true;
  }
  pumpSerial();
  yield();
}
