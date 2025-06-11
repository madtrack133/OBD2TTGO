#include <ctype.h>           // ❹ for isxdigit()
#include <NimBLEDevice.h>
#include <TFT_eSPI.h>        // LilyGo T-Display S3 ships with this lib
#include <SPI.h>
#include "nimconfig.h"

volatile uint16_t latestRPM = 0;   // ❷ global cache for the RPM value
TFT_eSPI tft = TFT_eSPI();  
void scanEndedCB(NimBLEScanResults results);


// Handle to the BLE characteristic we’ll use for I/O
static NimBLERemoteCharacteristic* elmChr = nullptr;
static NimBLEAdvertisedDevice* advDevice = nullptr;
static NimBLERemoteCharacteristic* elmTX = nullptr;  // 0xFFF1 – notifications
static NimBLERemoteCharacteristic* elmRX = nullptr;  // 0xFFF2 – writes


// Simple FPS limiter for display updates
static uint32_t lastScreenUpdate = 0;
const uint32_t screenPeriodMs    = 200;   // 5 fps is plenty

static bool doConnect = false;
static uint32_t scanTime = 0; /** 0 = scan forever */

/**  None of these are required as they will be handled by the library with defaults. **
 **                       Remove as you see fit for your needs                        */
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        Serial.println("Connected");
        pClient->updateConnParams(120,120,0,60);
    }

    void onDisconnect(NimBLEClient* pClient) override {
        Serial.print(pClient->getPeerAddress().toString().c_str());
        Serial.println(" Disconnected - Starting scan");
        NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
    }

    bool onConnParamsUpdateRequest(NimBLEClient* pClient, const ble_gap_upd_params* params) override {
        if(params->itvl_min < 24 || params->itvl_max > 40 || params->latency > 2 || params->supervision_timeout > 100) {
            return false;
        }
        return true;
    }

    uint32_t onPassKeyRequest() override {
        Serial.println("Client Passkey Request");
        return 1234;
    }

    bool onConfirmPIN(uint32_t pass_key) override {
        Serial.print("The passkey YES/NO number: ");
        Serial.println(pass_key);
        return true;
    }

    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        if(!desc->sec_state.encrypted) {
            Serial.println("Encrypt connection failed - disconnecting");
            NimBLEDevice::getClientByID(desc->conn_handle)->disconnect();
        }
    }
};

class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
        Serial.print("Advertised Device found: ");
        Serial.println(advertisedDevice->toString().c_str());
        if (advertisedDevice->getName() == "OBDII" ||
            advertisedDevice->isAdvertisingService(NimBLEUUID((uint16_t)0xFFF0))) {
            Serial.println("Found OBD-II dongle – stopping scan");
            NimBLEDevice::getScan()->stop();
            advDevice = advertisedDevice;
            doConnect = true;
        }
    }
};

/** Notification callback – parse replies */
void notifyCB(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool) {
    // 1) Dump raw bytes in hex…
  Serial.print("Raw notification (len=");
  Serial.print(len);
  Serial.print("): ");
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();

  // 2) Dump as ASCII so you can see CR/LF or echoes…
  Serial.print("   As ASCII: \"");
  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (isPrintable(c)) Serial.print(c);
    else if (c=='\r') Serial.print("\\r");
    else if (c=='\n') Serial.print("\\n");
    else Serial.print('?');
  }
  Serial.println('"');
    char buf[32]; uint8_t j = 0;
    for(size_t i = 0; i < len && j < sizeof(buf)-1; ++i)
        if(isxdigit(data[i]) || data[i] == ' ') buf[j++] = data[i];
    buf[j] = '\0';

    uint8_t A, B;
    if(sscanf(buf, "41 0C %hhx %hhx", &A, &B) == 2) {
        latestRPM = (((uint16_t)A << 8) | B) / 4;
    }
}

/** Scan-ended callback */
void scanEndedCB(NimBLEScanResults results) {
    Serial.println("Scan Ended");
}

static ClientCallbacks clientCB;

bool connectToServer() {
    NimBLEClient* pClient = nullptr;

    if(NimBLEDevice::getClientListSize()) {
        pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
        if(pClient) {
            if(!pClient->connect(advDevice, false)) {
                Serial.println("Reconnect failed");
                return false;
            }
            Serial.println("Reconnected client");
        } else {
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }

    if(!pClient) {
        if(NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
            Serial.println("Max clients reached - no more connections available");
            return false;
        }
        pClient = NimBLEDevice::createClient();
        Serial.println("New client created");
        pClient->setClientCallbacks(&clientCB, false);
        pClient->setConnectionParams(24,40,0,200);
        pClient->setConnectTimeout(15);
        if(!pClient->connect(advDevice)) {
            Serial.println("Failed to connect, deleted client");
            return false;
        }
    }

    if(!pClient->isConnected()) {
        if(!pClient->connect(advDevice)) {
            Serial.println("Failed to connect");
            return false;
        }
    }

    Serial.print("Connected to: ");
    Serial.println(pClient->getPeerAddress().toString().c_str());
    Serial.print("RSSI: ");
    Serial.println(pClient->getRssi());

    NimBLERemoteService* pSvc = pClient->getService("FFF0");
    if(!pSvc) pSvc = pClient->getService("FFE0");
    if(!pSvc) pSvc = pClient->getService("FFE5");
    if(!pSvc) { Serial.println("OBD service not found"); return false; }

    elmChr = pSvc->getCharacteristic("FFF1");
    if(!elmChr) elmChr = pSvc->getCharacteristic("FFE1");
    if(!elmChr) { Serial.println("OBD char not found"); return false; }

    if(!elmChr->subscribe(true, notifyCB)) {
        Serial.println("Subscribing failed");
        pClient->disconnect();
        return false;
    }

    Serial.println("OBD-II characteristic ready");
      // ——— ELM327 startup sequence ———
  static const char* initCmds[] = {
    "ATZ\r",    // reset
    "ATE0\r",   // echo off
    "ATS0\r",   // spaces off
    "ATH0\r",   // headers off
    "ATSP0\r"   // auto protocol
  };
  for (auto &cmd : initCmds) {
    Serial.printf("ELM init: %s", cmd);
    elmChr->writeValue((uint8_t*)cmd, strlen(cmd), false);
    delay(500);        // give the dongle time to respond and settle
  }
  Serial.println("ELM327 init done, now polling RPM");

    return true;
}

void requestRPM() {
    if(elmChr && elmChr->canWrite()) {
        elmChr->writeValue("010C\r", false);
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting NimBLE Client");
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLUE);
    tft.drawString("RPM mmmm guage", 20, 40);
    NimBLEDevice::init("");
    NimBLEDevice::setSecurityPasskey(1234);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(true, false, false);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    pScan->setInterval(45);
    pScan->setWindow(15);
    pScan->setActiveScan(true);
    pScan->start(scanTime, scanEndedCB);
}

void loop() {
    if(doConnect) {
        doConnect = false;
        if(!connectToServer()) {
            Serial.println("Connection attempt failed – restarting scan");
            delay(1000);
            NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
            return;
        }
    }

    static uint32_t lastPoll = 0;
    uint32_t now = millis();
    if(elmChr && now - lastPoll >= 200) {
        lastPoll = now;
        requestRPM();
    }

    static uint16_t prevRPM = 10;
    if(latestRPM != prevRPM && now - lastScreenUpdate >= screenPeriodMs) {
        prevRPM = latestRPM;
        lastScreenUpdate = now;
        //tft.fillRect(60, 100, 200, 80, TFT_BLACK);
        tft.println(prevRPM);
    }
}
