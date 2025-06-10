#define NIMBLE_DEBUG

#include <NimBLEDevice.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// Define UUIDs for the OBD-II adapter
#define SERVICE_UUID            "0000FFF0-0000-1000-8000-00805F9B34FB"
#define CHARACTERISTIC_TX_UUID  "0000FFF1-0000-1000-8000-00805F9B34FB" // Notify
#define CHARACTERISTIC_RX_UUID  "0000FFF2-0000-1000-8000-00805F9B34FB" // Write

// OBD-II Command
const char* OBD_RPM_CMD = "010C\r";

// TFT Display
TFT_eSPI tft = TFT_eSPI(); // Invoke library

// BLE Client
NimBLEClient* pClient = nullptr;
NimBLERemoteService* pService = nullptr;
NimBLERemoteCharacteristic* pTXCharacteristic = nullptr; // For notifications
NimBLERemoteCharacteristic* pRXCharacteristic = nullptr; // For writing commands

// Flags
bool deviceConnected = false;
bool isSubscribed = false;
unsigned long lastRequestTime = 0;
const unsigned long requestInterval = 1000; // Request RPM every 1 second

// Device details
const char* OBDII_DEVICE_MAC = "00:10:CC:4F:36:03"; // Replace with your device's actual MAC address

// Function prototypes
int parseRPM(const char* response);
void displayRPM(int rpm);
void connectToServer(NimBLEAddress pAddress);
void scanForDevice();
void sendOBDCommand(const char* cmd);

// ----------------------
// Callback Classes
// ----------------------

// 1. MyClientCallback: Handles connection and disconnection events
class MyClientCallback : public NimBLEClientCallbacks {
public:
    void onConnect(NimBLEClient* pClient) override {
        Serial.println("Connected to OBD-II adapter");
        deviceConnected = true;
    }

    void onDisconnect(NimBLEClient* pClient) override {
        Serial.println("Disconnected from OBD-II adapter");
        deviceConnected = false;
        isSubscribed = false;
        pService = nullptr;
        pTXCharacteristic = nullptr;
        pRXCharacteristic = nullptr;

        // Restart scanning
        scanForDevice();
    }
};

// 2. MyAdvertisedDeviceCallbacks: Handles BLE scan results
class MyAdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
public:
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
        String deviceAddress = advertisedDevice->getAddress().toString().c_str();
        Serial.print("Device Address: ");
        Serial.println(deviceAddress);

        if (deviceAddress.equalsIgnoreCase(String(OBDII_DEVICE_MAC))) {
            Serial.println("OBD-II adapter found by MAC!");
            tft.println("OBD-II adapter found by MAC!");

            // Stop scanning
            NimBLEDevice::getScan()->stop();
            Serial.println("Scanning stopped.");

            // Proceed with connection
            connectToServer(advertisedDevice->getAddress());
        }
    }
};

// ----------------------
// Setup Function
// ----------------------
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting OBD-II RPM Reader");

    // Initialize TFT Display
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 10);
    tft.println("OBD-II RPM Reader");

    // Initialize BLE
    NimBLEDevice::init("");

    // Set security and PIN code
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_MITM);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(1234); // Set PIN to 1234


    // Start scanning
    scanForDevice();
}

// ----------------------
// Loop Function
// ----------------------
void loop() {
    if (deviceConnected && isSubscribed) {
        unsigned long currentTime = millis();
        if (currentTime - lastRequestTime > requestInterval) {
            lastRequestTime = currentTime;
            sendOBDCommand(OBD_RPM_CMD);
        }
    }

    delay(100);
}

// ----------------------
// Helper Functions
// ----------------------

// Function to scan for the device
void scanForDevice() {
    NimBLEScan* pBLEScan = NimBLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    tft.println("Scanning...");
    Serial.println("BLE Scan started.");
    pBLEScan->start(0, nullptr);
}

// Function to send OBD-II command
void sendOBDCommand(const char* cmd) {
    if (pRXCharacteristic != nullptr) {
        pRXCharacteristic->writeValue((uint8_t*)cmd, strlen(cmd), false);
        Serial.printf("Sent OBD-II command: %s\n", cmd);
        tft.printf("Sent: %s\n", cmd);
    } else {
        Serial.println("RX Characteristic is null");
        tft.println("RX Characteristic is null");
    }
}

// Function to parse RPM from response
int parseRPM(const char* response) {
    const char* ptr = strstr(response, "410C");
    if (ptr != nullptr && strlen(ptr) >= 8) {
        char hexA[3] = {ptr[4], ptr[5], '\0'};
        char hexB[3] = {ptr[6], ptr[7], '\0'};
        int A = (int) strtol(hexA, nullptr, 16);
        int B = (int) strtol(hexB, nullptr, 16);
        int rpm = ((A * 256) + B) / 4;
        return rpm;
    }
    return 0;
}

// Function to display RPM on TFT
void displayRPM(int rpm) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(10, 50);
    tft.printf("RPM: %d", rpm);
}

// Notification callback
void notifyCallback(NimBLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    if (length > 0) {
        std::string value((char*)pData, length);
        Serial.print("Received RPM data: ");
        Serial.println(value.c_str());

        int rpm = parseRPM(value.c_str());
        if (rpm > 0) {
            displayRPM(rpm);
        } else {
            Serial.println("Invalid RPM data");
        }
    }
}

void connectToServer(NimBLEAddress pAddress) {
    pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());

    Serial.println("Attempting to connect to the OBD-II server...");
    tft.println("Attempting to connect to server...");

    bool connectionSuccess = false;
    for (int attempts = 0; attempts < 3; attempts++) {
        Serial.printf("Connection attempt %d...\n", attempts + 1);
        bool result = pClient->connect(pAddress);
        Serial.printf("Connection attempt %d result: %s\n", attempts + 1, result ? "Success" : "Failed");
        if (result) {
            connectionSuccess = true;
            break;
        }
        tft.println("Connection attempt failed, retrying...\n");
        delay(1000);
    }

    if (!connectionSuccess) {
        Serial.println("Failed to connect after 3 attempts.");
        tft.println("Failed to connect after 3 attempts.");
        scanForDevice();
        return;
    }

    Serial.println("Connected to OBD-II adapter!");
    tft.println("Connected to server");

    pService = pClient->getService(SERVICE_UUID);
    if (pService == nullptr) {
        Serial.println("Failed to find OBD-II service");
        tft.println("Failed to find OBD-II service");
        pClient->disconnect();
        return;
    } else {
        Serial.println("Service discovered");
        tft.println("Service discovered");
    }

    pTXCharacteristic = pService->getCharacteristic(CHARACTERISTIC_TX_UUID);
    pRXCharacteristic = pService->getCharacteristic(CHARACTERISTIC_RX_UUID);

    if (pTXCharacteristic == nullptr || pRXCharacteristic == nullptr) {
        Serial.println("Failed to find TX/RX characteristics");
        tft.println("Failed to find TX/RX characteristics");
        pClient->disconnect();
        return;
    } else {
        Serial.println("Characteristics discovered");
        tft.println("Characteristics discovered");
    }

    if (pTXCharacteristic->canNotify()) {
        pTXCharacteristic->subscribe(true, notifyCallback);
        Serial.println("Notifications registered");
        tft.println("Subscribed to TX characteristic for notifications");
        isSubscribed = true;
    } else {
        Serial.println("TX characteristic does not support notifications");
        tft.println("TX characteristic does not support notifications");
    }
}

Will this work on my Lilygo s3 t display 