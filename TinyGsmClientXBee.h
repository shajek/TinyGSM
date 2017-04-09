/**
 * @file       TinyWiFiClientESP8266.h
 * @author     Volodymyr Shymanskyy
 * @license    LGPL-3.0
 * @copyright  Copyright (c) 2016 Volodymyr Shymanskyy
 * @date       Nov 2016
 */

#ifndef TinyWiFiClientXBee_h
#define TinyWiFiClientXBee_h

// #define TINY_GSM_DEBUG Serial

#if !defined(TINY_GSM_RX_BUFFER)
  #define TINY_GSM_RX_BUFFER 256
#endif

#include <TinyGsmCommon.h>

#define GSM_NL "\r"
static const char GSM_OK[] TINY_GSM_PROGMEM = "OK" GSM_NL;
static const char GSM_ERROR[] TINY_GSM_PROGMEM = "ERROR" GSM_NL;

enum RegStatus {
  REG_UNREGISTERED = 0,
  REG_SEARCHING    = 2,
  REG_DENIED       = 3,
  REG_OK_HOME      = 1,
  REG_OK_ROAMING   = 5,
  REG_UNKNOWN      = 4,
};

class TinyGsm
{

public:
  TinyGsm(Stream& stream)
    : stream(stream)
  {}

public:

class GsmClient : public Client
{
  friend class TinyGsm;
  typedef TinyGsmFifo<uint8_t, TINY_GSM_RX_BUFFER> RxFifo;

public:
  GsmClient() {}

  GsmClient(TinyGsm& modem, uint8_t mux = 1) {
    init(&modem, mux);
  }

  bool init(TinyGsm* modem, uint8_t mux = 1) {
    this->at = modem;
    this->mux = mux;
    sock_connected = false;

    at->sockets[mux] = this;

    return true;
  }

public:
  virtual int connect(const char *host, uint16_t port) {
    TINY_GSM_YIELD();
    rx.clear();
    at->commandMode();
    sock_connected = at->modemConnect(host, port, mux);
    at->writeChanges();
    at->exitCommand();
    return sock_connected;
  }

  virtual int connect(IPAddress ip, uint16_t port) {
    TINY_GSM_YIELD();
    rx.clear();
    at->commandMode();
    sock_connected = at->modemConnect(ip, port, mux);
    at->writeChanges();
    at->exitCommand();
    return sock_connected;
  }

  virtual void stop() {
    sock_connected = false;
  }

  virtual size_t write(const uint8_t *buf, size_t size) {
    TINY_GSM_YIELD();
    //at->maintain();
    return at->modemSend(buf, size, mux);
  }

  virtual size_t write(uint8_t c) {
    return write(&c, 1);
  }

  virtual int available() {
    TINY_GSM_YIELD();
    if (!rx.size()) {
      at->maintain();
    }
    return rx.size();
  }

  virtual int read(uint8_t *buf, size_t size) {
    TINY_GSM_YIELD();
    size_t cnt = 0;
    while (cnt < size) {
      size_t chunk = TinyGsmMin(size-cnt, rx.size());
      if (chunk > 0) {
        rx.get(buf, chunk);
        buf += chunk;
        cnt += chunk;
        continue;
      }
      // TODO: Read directly into user buffer?
      if (!rx.size()) {
        at->maintain();
        //break;
      }
    }
    return cnt;
  }

  virtual int read() {
    uint8_t c;
    if (read(&c, 1) == 1) {
      return c;
    }
    return -1;
  }

  virtual int peek() { return at->stream.peek(); }
  virtual void flush() { at->stream.flush(); }

  virtual uint8_t connected() {
    if (available()) {
      return true;
    }
    return sock_connected;
  }
  virtual operator bool() { return connected(); }
private:
  TinyGsm*      at;
  uint8_t       mux;
  bool          sock_connected;
  RxFifo        rx;
};

public:

  /*
   * Basic functions
   */
  bool begin() {
    return init();
  }

  bool init() {
    if (!autoBaud()) {
      return false;
    }
    return true;
  }

  bool autoBaud(unsigned long timeout = 10000L) {  // not supported
    return false;
  }

  void maintain() {
    //while (stream.available()) {
      waitResponse(10, NULL, NULL);
    //}
  }

  bool factoryDefault() {
    commandMode();
    sendAT(GF("RE"));
    bool ret_val = waitResponse() == 1;
    writeChanges();
    exitCommand();
    return ret_val;
  }

  /*
   * Power functions
   */

  bool restart() {
    commandMode();
    sendAT(GF("FR"));
    if (waitResponse() != 1) {
      return false;
    }
    delay (2000);  // Actually resets about 2 seconds later
    for (unsigned long start = millis(); millis() - start < 60000L; ) {
      if (commandMode()) {
        exitCommand();
        return true;
      }
    }
    exitCommand();
    return false;;
  }

  /*
   * SIM card & Networ Operator functions
   */

  bool simUnlock(const char *pin) {  // Not supported
    return false;
  }

  String getSimCCID() {
    commandMode();
    sendAT(GF("S#"));
    String res = streamReadUntil('\r');
    exitCommand();
    return res;
  }

  String getIMEI() {
    commandMode();
    sendAT(GF("IM"));
    String res = streamReadUntil('\r');
    exitCommand();
    return res;
  }

  int getSignalQuality() {
    commandMode();
    sendAT(GF("DB"));
    char buf[4] = { 0, };
    buf[0] = streamRead();
    buf[1] = streamRead();
    buf[2] = streamRead();
    buf[3] = streamRead();
    exitCommand();
    int intr = strtol(buf, 0, 16);
    return intr;
  }

  RegStatus getRegistrationStatus() {
    commandMode();
    sendAT(GF("AI"));
    String res = streamReadUntil('\r');
    exitCommand();

    if(res == GF("0x00"))
      return REG_OK_HOME;

    else if(res == GF("0x13") || res == GF("0x2A"))
      return REG_UNREGISTERED;

    else if(res == GF("0xFF") || res == GF("0x22") || res == GF("0x23") ||
            res == GF("0x40") || res == GF("0x41") || res == GF("0x42"))
      return REG_SEARCHING;

    else if(res == GF("0x24"))
      return REG_DENIED;

    else return REG_UNKNOWN;
  }

  String getOperator() {
    commandMode();
    sendAT(GF("MN"));
    String res = streamReadUntil('\r');
    exitCommand();
    return res;
  }


  bool waitForNetwork(unsigned long timeout = 60000L) {
    for (unsigned long start = millis(); millis() - start < timeout; ) {
      if (modemGetConnected()) {
        return true;
      }
      delay(1000);
    }
    return false;
  }

  /*
   * WiFi functions
   */
  bool networkConnect(const char* ssid, const char* pwd) {

    commandMode();

    sendAT(GF("AP"), 0);  // Put in transparent mode
    waitResponse();
    sendAT(GF("IP"), 1);  // Put in TCP mode
    waitResponse();

    sendAT(GF("ID"), ssid);
    if (waitResponse() != 1) {
      goto fail;
    }

    sendAT(GF("PK"), pwd);
    if (waitResponse() != 1) {
      goto fail;
    }

    writeChanges();
    exitCommand();

    return true;

    fail:
      exitCommand();
      return false;
  }

  bool networkDisconnect() {
    return false;
  }

  /*
   * GPRS functions
   */
  bool gprsConnect(const char* apn) {

    commandMode();

    sendAT(GF("AP"), 0);  // Put in transparent mode
    waitResponse();
    sendAT(GF("IP"), 0);  // Put in UDP mode
    waitResponse();

    sendAT(GF("AN"), apn);
    waitResponse();

    return true;
  }

  bool gprsDisconnect() {  // TODO
    return false;
  }

  /*
   * Messaging functions
   */

  void sendUSSD() {
  }

  void sendSMS() {
  }

  bool sendSMS(const String& number, const String& text) {
    commandMode();
    sendAT(GF("AP"), 0);
    waitResponse();
    sendAT(GF("IP"), 2);
    waitResponse();
    sendAT(GF("PH"), number);
    waitResponse();
    sendAT(GF("TD D"));
    waitResponse();
    sendAT(GF("TD D"));
    waitResponse();
    writeChanges();
    exitCommand();
    stream.print(text);
    stream.write((char)0x0D);
    return true;
  }

  /* Public Utilities */
  template<typename... Args>
  void sendAT(Args... cmd) {
    streamWrite("AT", cmd..., GSM_NL);
    stream.flush();
    TINY_GSM_YIELD();
    DBG(GSM_NL, ">>> AT:", cmd...);
  }

  bool commandMode(void){
    delay(1000);  // cannot send anything for 1 second before entering command mode
    streamWrite("+++");  // enter command mode
    waitResponse(1100);
    return 1 == waitResponse(1100);  // wait another second for an "OK\r"
  }

  void writeChanges(void){
    streamWrite("ATWR", GSM_NL);  // Write changes to flash
    waitResponse();
    streamWrite("ATAC", GSM_NL);  // Apply changes
    waitResponse();
  }

  void exitCommand(void){
    streamWrite("ATCN", GSM_NL);  // Exit command mode
    waitResponse();
  }

  // TODO: Optimize this!
  uint8_t waitResponse(uint32_t timeout, String& data,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    /*String r1s(r1); r1s.trim();
    String r2s(r2); r2s.trim();
    String r3s(r3); r3s.trim();
    String r4s(r4); r4s.trim();
    String r5s(r5); r5s.trim();
    DBG("### ..:", r1s, ",", r2s, ",", r3s, ",", r4s, ",", r5s);*/
    data.reserve(64);
    int index = 0;
    unsigned long startMillis = millis();
    do {
      TINY_GSM_YIELD();
      while (stream.available() > 0) {
        int a = streamRead();
        if (a <= 0) continue; // Skip 0x00 bytes, just in case
        data += (char)a;
        if (r1 && data.endsWith(r1)) {
          index = 1;
          goto finish;
        } else if (r2 && data.endsWith(r2)) {
          index = 2;
          goto finish;
        } else if (r3 && data.endsWith(r3)) {
          index = 3;
          goto finish;
        } else if (r4 && data.endsWith(r4)) {
          index = 4;
          goto finish;
        } else if (r5 && data.endsWith(r5)) {
          index = 5;
          goto finish;
        } else if (data.endsWith(GF(GSM_NL "+IPD,"))) {
          int mux = stream.readStringUntil(',').toInt();
          int len = stream.readStringUntil(':').toInt();
          if (len > sockets[mux]->rx.free()) {
            DBG("### Buffer overflow: ", len, "->", sockets[mux]->rx.free());
          } else {
            DBG("### Got: ", len, "->", sockets[mux]->rx.free());
          }
          while (len--) {
            while (!stream.available()) {}
            sockets[mux]->rx.put(stream.read());
          }
          data = "";
          return index;
        } else if (data.endsWith(GF(GSM_NL "1,CLOSED" GSM_NL))) { //TODO: use mux
          sockets[1]->sock_connected = false;
          data = "";
        }
      }
    } while (millis() - startMillis < timeout);
  finish:
    if (!index) {
      data.trim();
      if (data.length()) {
        DBG("### Unhandled:", data);
      }
      data = "";
    }
    return index;
  }

  uint8_t waitResponse(uint32_t timeout,
                       GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    String data;
    return waitResponse(timeout, data, r1, r2, r3, r4, r5);
  }

  uint8_t waitResponse(GsmConstStr r1=GFP(GSM_OK), GsmConstStr r2=GFP(GSM_ERROR),
                       GsmConstStr r3=NULL, GsmConstStr r4=NULL, GsmConstStr r5=NULL)
  {
    return waitResponse(1000, r1, r2, r3, r4, r5);
  }

private:
  int modemConnect(const char* host, uint16_t port, uint8_t mux = 1) {
    sendAT(GF("LA"), host);
    String ipadd; ipadd.reserve(16);
    ipadd = streamReadUntil('\r');
    IPAddress ip;
    ip.fromString(ipadd);
    return modemConnect(ip, port);
  }

  int modemConnect(IPAddress ip, uint16_t port, uint8_t mux = 1) {
    String host; host.reserve(16);
    host += ip[0];
    host += ".";
    host += ip[1];
    host += ".";
    host += ip[2];
    host += ".";
    host += ip[3];
    sendAT(GF("DL"), host);
    waitResponse();
    sendAT(GF("DE"), String(port, HEX));
    int rsp = waitResponse();
    return rsp;
  }

  int modemSend(const void* buff, size_t len, uint8_t mux = 1) {
    stream.write((uint8_t*)buff, len);
    return len;
  }

  bool modemGetConnected(uint8_t mux = 1) {
    commandMode();
    sendAT(GF("AI"));
    int res = waitResponse(GF("0"));
    exitCommand();
    return 1 == res;
  }

  /* Private Utilities */
  template<typename T>
  void streamWrite(T last) {
    stream.print(last);
  }

  template<typename T, typename... Args>
  void streamWrite(T head, Args... tail) {
    stream.print(head);
    streamWrite(tail...);
  }

  int streamRead() { return stream.read(); }

  String streamReadUntil(char c) {
    String return_string = stream.readStringUntil(c);
    return_string.trim();
    if (String(c) == GSM_NL || String(c) == "\n"){
      DBG(return_string, c, "    ");
    } else DBG(return_string, c);
    return return_string;
  }

private:
  Stream&       stream;
  GsmClient*    sockets[1];
};

typedef TinyGsm::GsmClient TinyGsmClient;

#endif
