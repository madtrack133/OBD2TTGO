#define NIMBLE_DEBUG

#include <NimBLEDevice.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// --- CONFIGURATION ---
static const char* SERVICE_UUID        = "0000FFF0-0000-1000-8000-00805F9B34FB";
static const char* CHAR_UUID_TX_NOTIFY = "0000FFF1-0000-1000-8000-00805F9B34FB";
static const char* CHAR_UUID_RX_WRITE  = "0000FFF2-0000-1000-8000-00805F9B34FB";
static const char* OBD_RPM_CMD         = "010C\r";
static const uint32_t REQUEST_INTERVAL = 1000;

// --- GLOBALS ---
TFT_eSPI            tft;
NimBLEClient*        pClient    = nullptr;
NimBLERemoteService* pService   = nullptr;
NimBLERemoteCharacteristic* pRXChar = nullptr;
bool                 deviceOK   = false;
bool                 subOK      = false;
uint32_t             lastReq    = 0;

// forward declarations
void scanForOBD();
void connectToOBD(NimBLEAdvertisedDevice* dev);
int  parseRPM(const std::string&);
void displayRPM(int rpm);

// —————————————————————————————
// Client callbacks
class ClientCB : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient*) override {
    tft.println("GATT connected");
    deviceOK = true;
  }
  void onDisconnect(NimBLEClient*) override {
    tft.println("GATT disconnected");
    deviceOK = false;
    subOK    = false;
    pService = nullptr;
    pRXChar  = nullptr;
    scanForOBD();
  }
};

// —————————————————————————————
// Scan callbacks
// in ScanCB:
class ScanCB : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    bool hasFFF0 = dev->isAdvertisingService(NimBLEUUID(SERVICE_UUID));
    bool canConn  = dev->isConnectable();
    Serial.printf("Adv: %s  RSSI=%d  FFF0=%d  Conn=%d\n",
                  dev->getAddress().toString().c_str(),
                  dev->getRSSI(), hasFFF0, canConn);
    if(hasFFF0 && canConn) {
      tft.println("Found connectable OBD-II!");
      NimBLEDevice::getScan()->stop();
      connectToOBD(dev);
    }
  }
};

// —————————————————————————————
// Notify callback
static void onNotify(NimBLERemoteCharacteristic* rc, uint8_t* data, size_t len, bool) {
  std::string resp((char*)data, len);
  tft.print("Raw: "); Serial.println(resp.c_str());
  int rpm = parseRPM(resp);
  if(rpm>0) displayRPM(rpm);
}

// —————————————————————————————
// SETUP
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.setDebugOutput(true);
  NimBLEDevice::init("");

  // Just-Works, no MITM, no passkey
  NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10,10);
  tft.println("OBD-II RPM Reader");

  // start
  scanForOBD();
}

// —————————————————————————————
// LOOP
void loop() {
  if(deviceOK && subOK) {
    if(millis() - lastReq >= REQUEST_INTERVAL) {
      lastReq = millis();
      pRXChar->writeValue((uint8_t*)OBD_RPM_CMD, strlen(OBD_RPM_CMD), false);
      tft.println("→ Sent 010C");
    }
  }
  delay(20);
}

// —————————————————————————————
// SCAN
void scanForOBD() {
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10,10);
  tft.println("Scanning for BLE OBD-II…");

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCB());
  scan->setActiveScan(false);        // ← passive scan only
  scan->setInterval(160);
  scan->setWindow(160);
  scan->start(0, nullptr);
}

// —————————————————————————————
// CONNECT + DISCOVER
void connectToOBD(NimBLEAdvertisedDevice* dev) {
  tft.printf("Connecting to %s …\n", dev->getAddress().toString().c_str());

  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(new ClientCB(), false);

  // try to establish GATT connection
  if (!pClient->connect(dev)) {
    // no more getConnInfo()/status here
    tft.printf("GATT connect() failed\n");
    Serial.println("Connect() failed");
    delay(1000);
    scanForOBD();  
    return;
  }

  // service
  pService = pClient->getService(SERVICE_UUID);
  if(!pService) {
    tft.println("Service not found");
    pClient->disconnect();
    return;
  }

  // RX (write)
  pRXChar = pService->getCharacteristic(CHAR_UUID_RX_WRITE);
  if(!pRXChar) {
    tft.println("Write char missing");
    pClient->disconnect();
    return;
  }

  // TX (notify)
  auto* tx = pService->getCharacteristic(CHAR_UUID_TX_NOTIFY);
  if(!tx || !tx->canNotify()) {
    tft.println("Notify char missing");
    pClient->disconnect();
    return;
  }
  tx->subscribe(true, onNotify);
  subOK = true;
  tft.println("Subscribed");
}

// —————————————————————————————
// PARSE + DISPLAY
int parseRPM(const std::string& r) {
  auto p = r.find("410C");
  if(p!=std::string::npos && r.size()>=p+8) {
    int A = strtol(r.substr(p+4,2).c_str(), nullptr, 16);
    int B = strtol(r.substr(p+6,2).c_str(), nullptr, 16);
    return ((A<<8)|B)/4;
  }
  return -1;
}

void displayRPM(int rpm) {
  tft.fillRect(0,40, tft.width(), tft.height()-40, TFT_BLACK);
  tft.setCursor(10,50);
  tft.setTextSize(4);
  tft.printf("RPM: %d", rpm);
}
