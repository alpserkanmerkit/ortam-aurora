/*
 * Ortam Aurora - ESP32-S3 smart room monitor + sunrise wake-up light
 * Author: Alp Serkan Merkit (built with AI coding agents) - MIT License, see LICENSE
 *
 * Public build v2.0 = v1 field (presentation) firmware + security hardening:
 *  - Wi-Fi setup AP is WPA2-protected with a per-device random password (NVS)
 *  - HTTP API needs "Authorization: Bearer <token>"; state changes are POST-only
 *  - WebSocket: first message (within 5 s) must be the device token, else the socket is closed
 *  - BLE: bonding + MITM + Secure Connections; all characteristic reads/writes need an
 *    encrypted, authenticated link; a fresh random 6-digit PIN is shown on the OLED
 *    for every pairing; unauthenticated links are dropped after 30 s
 * AP password + API token are generated on first boot, stored in NVS ("guvenlik"),
 * printed ONCE on Serial and shown on the OLED (15 s) on first boot. The AP password is
 * also shown on the OLED every time the setup portal opens. Lost them? Erase flash and re-flash.
 * Location / UTC offset: "LOCATION" config block below (default: İzmir city centre, UTC+3).
 * Identifiers and UI strings are in English.
 */

/*
 * ESP32-S3 Smart Room Monitor v2.0 (base: v1 field / presentation release)
 *
 * This release is the extended presentation version of the previous v7 logic.
 * Added:
 * - SGP30 TVOC / eCO2 air quality sensor
 * - MAX4466 analog microphone module
 * - Single I2C bus: OLED + BH1750 + SGP30 on the same bus
 * - New pin plan: mostly uses the other side of the ESP32-S3
 *
 * Target board:
 * - Board: ESP32-S3 Dev Module
 * - ESP32 Arduino Core: 2.0.17
 *
 * Recommended library versions:
 * 1. ESP Async WebServer by ESP32Async: 3.10.3
 * 2. Async TCP by ESP32Async: 3.4.10
 * 3. FastLED: 3.6.0, use 3.7.0 if problems occur
 * 4. NimBLE-Arduino by h2zero: 1.4.3
 * 5. WiFiManager by tzapu: 2.0.x
 * 6. Adafruit GFX Library
 * 7. Adafruit SSD1306
 * 8. DHT sensor library + Adafruit Unified Sensor
 * 9. BH1750 by Christopher Laws
 * 10. Adafruit SGP30
 *
 * Final pin plan:
 * - I2C SDA: GPIO40
 * - I2C SCL: GPIO41
 * - DHT22 DATA: GPIO42
 * - MAX4466 OUT: GPIO1
 * - WS2812B DATA: GPIO47, via logic shifter
 *
 * Hardware notes:
 * - All GND lines must be common.
 * - OLED, BH1750, SGP30, DHT22 and MAX4466 must be powered at 3.3V.
 * - The WS2812B LED strip must be powered by an external 5V supply.
 * - WS2812B data line must be wired ESP32 GPIO47 -> logic shifter LV -> HV -> 330R resistor -> DIN.
 * - A 1000uF capacitor is recommended across the LED strip 5V-GND, close to the strip input.
 */

#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <BH1750.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SGP30.h>
#include <FastLED.h>
#include "time.h"
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <NimBLEDevice.h>

// ============================================================
// LOCATION - CONFIG: city-level default (İzmir city centre) — set your own city.
// These coordinates are sent to api.sunrise-sunset.org to get the sunrise time.
// Fixed UTC offset, no DST handling (Turkey: UTC+3 all year).
// ============================================================
const float LATITUDE  = 38.4237;  // latitude,  decimal degrees (İzmir city centre)
const float LONGITUDE = 27.1428;  // longitude, decimal degrees (İzmir city centre)
const int   UTC_OFFSET_HOURS = 3;  // local time = UTC + this many hours (Turkey = 3)

// ============================================================
// PIN DEFINITIONS - presentation release
// ============================================================
#define I2C_SDA      40
#define I2C_SCL      41
#define DHT_PIN      42
#define MIC_PIN       1
#define LED_PIN      47

// ============================================================
// OLED DISPLAY
// ============================================================
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDRESS  0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledActive = false;

// ============================================================
// SENSORS
// ============================================================
#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);

BH1750 lightSensor;
bool bh1750Active = false;

Adafruit_SGP30 sgp30;
bool sgp30Active = false;

// ============================================================
// MAX4466 MICROPHONE
// ============================================================
int micRaw = 0;
int micLevel = 0;           // normalized sound level, 0-100
int micMinCalibration = 4095;
int micMaxCalibration = 0;
unsigned long micCalibrationStart = 0;
const unsigned long MIC_CALIBRATION_MS = 3000;
bool micCalibrationDone = false;

// ============================================================
// LED
// ============================================================
#define NUM_LEDS     60
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
CRGB leds[NUM_LEDS];
float TOTAL_DURATION_SEC = 1800.0;  // 30 minute sunrise simulation
const uint16_t LED_POWER_LIMIT_MA = 4000;
const uint8_t LED_SAFE_BRIGHTNESS = 160;
const uint8_t LED_DEMO_BOOST_BRIGHTNESS = 180;
const unsigned long LED_DEMO_BOOST_MS = 8000;
bool luxDemoActive = false;
unsigned long luxDemoEnd = 0;

// ============================================================
// WEB SERVER
// ============================================================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ============================================================
// NVS
// ============================================================
Preferences preferences;

// ============================================================
// WATCHDOG
// ============================================================
#define WDT_TIMEOUT_SEC 30

// ============================================================
// BLE DEFINITIONS
// ============================================================
#define BLE_DEVICE_NAME "OrtamMonitor"

#define SERVICE_UUID              "e3710001-0000-1000-8000-00805f9b34fb"
#define SENSOR_CHAR_UUID          "e3710002-0000-1000-8000-00805f9b34fb"
#define STATUS_CHAR_UUID          "e3710003-0000-1000-8000-00805f9b34fb"
#define LED_CONTROL_CHAR_UUID     "e3710004-0000-1000-8000-00805f9b34fb"
#define ALARM_CONTROL_CHAR_UUID   "e3710005-0000-1000-8000-00805f9b34fb"

NimBLEServer*         pServer           = nullptr;
NimBLECharacteristic* pSensorChar       = nullptr;
NimBLECharacteristic* pStatusChar       = nullptr;
NimBLECharacteristic* pLedControlChar   = nullptr;
NimBLECharacteristic* pAlarmControlChar = nullptr;

bool bleDeviceConnected = false;
int  bleConnectionCount = 0;
unsigned long lastBleNotify = 0;
const unsigned long BLE_NOTIFY_INTERVAL = 2000;

// ============================================================
// TIMERS
// ============================================================
unsigned long lastReadTime = 0;
unsigned long ledStartTime = 0;
unsigned long lastLedUpdate = 0;
unsigned long lastWsBroadcast = 0;
unsigned long lastSunriseCheck = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastOledUpdate = 0;
unsigned long lastSgp30BaselineSave = 0;

// Sensor cache
float cacheTemperature = 0;
float cacheHumidity = 0;
float cacheLux = 0;
uint16_t cacheEco2 = 0;
uint16_t cacheTvoc = 0;
bool dhtError = true;
int dhtConsecutiveErrors = 0;
const int DHT_ERROR_THRESHOLD = 5;

// WiFi and time
bool wifiConnected = false;
bool timeSynced = false;
int wifiDropCount = 0;
bool webServerStarted = false;
unsigned long lastWifiReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 30000;

// Sunrise alarm system
int sunriseHour = -1;
int sunriseMinute = -1;
int alarmHour = -1;
int alarmMinute = -1;
bool alarmFiredToday = false;
int lastAlarmDay = -1;
bool alarmEnabled = true;
bool manualLedControl = false;

enum LedState { SUNRISE, STANDBY, DARK, MANUAL_COLOR };
LedState activeLedState = DARK;

uint8_t manualLedR = 255;
uint8_t manualLedG = 180;
uint8_t manualLedB = 80;
uint8_t manualLedBrightness = 128;

// Error / status messages
String lastErrorMessage = "";
unsigned long lastErrorTime = 0;

// Uptime
unsigned long bootTime = 0;

// ============================================================
// SECURITY - device-specific secrets (generated on first boot, stored in NVS "guvenlik")
// ============================================================
#define AP_PASSWORD_LENGTH   12   // WPA2 setup AP password
#define API_TOKEN_LENGTH   20   // HTTP / WebSocket bearer token (100 bit)
String apPassword = "";
String apiToken = "";
bool secretsGenerated = false;
bool securityNvsError = false;  // true if the secrets could not be written to NVS

// BLE pairing: a new random 6-digit PIN for every pairing (no fixed PIN)
volatile uint32_t blePairingPin = 0;           // 0 = no PIN to show
volatile unsigned long blePairingStart = 0;
const unsigned long BLE_PIN_DISPLAY_MS = 30000;
const unsigned long BLE_VERIFY_TIMEOUT_MS = 30000; // connections not verified within this time are dropped
#define BLE_CONN_MAX 4
uint16_t bleConnHandles[BLE_CONN_MAX] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
unsigned long bleConnTimes[BLE_CONN_MAX] = {0};
volatile bool bleAllSecure = false;  // notify only while true
unsigned long lastBleSecurityCheck = 0;

// WebSocket: client IDs that sent the token (0 = empty slot)
#define WS_AUTH_MAX 8
uint32_t wsAuthIds[WS_AUTH_MAX] = {0};
// WebSocket: clients that have not sent the token yet + connect time
#define WS_PENDING_MAX 8
const unsigned long WS_AUTH_TIMEOUT_MS = 5000;
uint32_t wsPendingIds[WS_PENDING_MAX] = {0};
unsigned long wsPendingTimes[WS_PENDING_MAX] = {0};
portMUX_TYPE wsLock = portMUX_INITIALIZER_UNLOCKED;

// ============================================================
// FUNCTION PROTOTYPES
// ============================================================
void readSensors();
void readMicrophone();
void updateDisplay();
void manageLedAnimation();
void wsBroadcastVerisi();
void handleWsEvent(AsyncWebSocket *svr, AsyncWebSocketClient *client,
                    AwsEventType type, void *arg, uint8_t *data, size_t len);
String ledStatusText();
String buildHtmlPage();
void setupClock();
void fetchSunriseTime();
void checkAlarm();
String timeText();
void nvsSave();
void nvsLoad();
void wifiReconnect();
void startWebServer();
void reportError(String message);
String uptimeText();
void bleStart();
void bleSendNotify();
void bleHandleCommand(String command);
String buildSensorJson();
String ledModeText();
void applyManualLed();
uint8_t clampToByte(int value);
void sgp30BaselineSave();
void sgp30BaselineLoad();
uint32_t getAbsoluteHumidity(float temperature, float humidity);
void prepareSecrets();
void showSecrets();
bool constantTimeEquals(const String &a, const String &b);
bool httpAuthorized(AsyncWebServerRequest *request);
bool wsIsAuthorized(uint32_t id);
bool wsAddAuthorized(uint32_t id);
void wsRemoveAuthorized(uint32_t id);
bool wsAddPending(uint32_t id);
void wsRemovePending(uint32_t id);
void wsCloseUnauthorized();
void bleSecurityCheck();
bool bleShowPinScreen();
int requestParamInt(AsyncWebServerRequest *request, const char *ad, int varsayilan);

// ============================================================
// BLE CALLBACK CLASSES - compatible with NimBLE 1.4.3
// ============================================================
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer) {
    bleDeviceConnected = true;
    bleConnectionCount++;
    Serial.printf("BLE: device connected, total: %d\n", bleConnectionCount);
    NimBLEDevice::startAdvertising();
  }

  // Security: pairing/encryption starts immediately on every connection;
  // connection time is recorded; loop() drops it if not verified in time.
  void onConnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
    bleAllSecure = false;
    bool registered = false;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
      if (bleConnHandles[i] == 0xFFFF) {
        bleConnHandles[i] = desc->conn_handle;
        bleConnTimes[i] = millis();
        registered = true;
        break;
      }
    }
    if (!registered) {  // untracked connections are rejected
      pServer->disconnect(desc->conn_handle);
      return;
    }
    NimBLEDevice::startSecurity(desc->conn_handle);
  }

  void onDisconnect(NimBLEServer* pServer) {
    bleDeviceConnected = (pServer->getConnectedCount() > 0);
    Serial.println("BLE: device disconnected");
    NimBLEDevice::startAdvertising();
  }

  void onDisconnect(NimBLEServer* pServer, ble_gap_conn_desc* desc) {
    for (int i = 0; i < BLE_CONN_MAX; i++) {
      if (bleConnHandles[i] == desc->conn_handle) bleConnHandles[i] = 0xFFFF;
    }
  }

  // Device shows the PIN (DISPLAY_ONLY): a new random PIN for every pairing.
  // NimBLE 1.4.3 calls this callback only while the static passkey is left at default (123456);
  // that is why setSecurityPasskey() is not used.
  uint32_t onPassKeyRequest() {
    uint32_t pin = 100000 + (esp_random() % 900000);
    blePairingStart = millis();
    blePairingPin = pin;
    Serial.printf("BLE pairing PIN: %06u\n", (unsigned)pin);
    return pin;
  }

  void onAuthenticationComplete(ble_gap_conn_desc* desc) {
    blePairingPin = 0;
    if (!desc->sec_state.encrypted || !desc->sec_state.authenticated) {
      Serial.println("BLE: verification failed, disconnecting");
      NimBLEDevice::getServer()->disconnect(desc->conn_handle);
      return;
    }
    Serial.println("BLE: encrypted + verified connection");
  }
};

class LedControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      String command = String(value.c_str());
      command.trim();
      Serial.print("BLE LED command: ");
      Serial.println(command);
      bleHandleCommand(command);
    }
  }
};

class AlarmControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      String command = String(value.c_str());
      command.trim();
      Serial.print("BLE Alarm command: ");
      Serial.println(command);
      bleHandleCommand(command);
    }
  }
};

// ============================================================
// BLE COMMAND HANDLING
// ============================================================
uint8_t clampToByte(int value) {
  return (uint8_t)constrain(value, 0, 255);
}

uint8_t safeBrightness(int value) {
  return (uint8_t)constrain(value, 0, LED_SAFE_BRIGHTNESS);
}

void applyManualLed() {
  luxDemoActive = false;
  manualLedControl = true;
  activeLedState = MANUAL_COLOR;
  fill_solid(leds, NUM_LEDS, CRGB(manualLedR, manualLedG, manualLedB));
  FastLED.setBrightness(manualLedBrightness);
  FastLED.show();
}

void bleHandleCommand(String command) {
  if (command == "led_on") {
    manualLedControl = true;
    activeLedState = SUNRISE;
    ledStartTime = millis();
  } else if (command == "led_off") {
    luxDemoActive = false;
    manualLedControl = false;
    FastLED.clear();
    FastLED.show();
    activeLedState = DARK;
  } else if (command == "lux_demo") {
    luxDemoActive = true;
    luxDemoEnd = millis() + LED_DEMO_BOOST_MS;
    manualLedControl = true;
    activeLedState = MANUAL_COLOR;
    manualLedR = 255;
    manualLedG = 255;
    manualLedB = 220;
    manualLedBrightness = LED_DEMO_BOOST_BRIGHTNESS;
    fill_solid(leds, NUM_LEDS, CRGB(manualLedR, manualLedG, manualLedB));
    FastLED.setBrightness(manualLedBrightness);
    FastLED.show();
  } else if (command == "alarm_toggle") {
    alarmEnabled = !alarmEnabled;
    nvsSave();
  } else if (command.startsWith("led_set:")) {
    String payload = command.substring(8);
    int p1 = payload.indexOf(',');
    int p2 = payload.indexOf(',', p1 + 1);
    int p3 = payload.indexOf(',', p2 + 1);

    if (p1 > 0 && p2 > p1 && p3 > p2) {
      manualLedR = clampToByte(payload.substring(0, p1).toInt());
      manualLedG = clampToByte(payload.substring(p1 + 1, p2).toInt());
      manualLedB = clampToByte(payload.substring(p2 + 1, p3).toInt());
      manualLedBrightness = safeBrightness(payload.substring(p3 + 1).toInt());
      applyManualLed();
    }
  } else if (command.startsWith("led_color:")) {
    String payload = command.substring(10);
    int p1 = payload.indexOf(',');
    int p2 = payload.indexOf(',', p1 + 1);

    if (p1 > 0 && p2 > p1) {
      manualLedR = clampToByte(payload.substring(0, p1).toInt());
      manualLedG = clampToByte(payload.substring(p1 + 1, p2).toInt());
      manualLedB = clampToByte(payload.substring(p2 + 1).toInt());
      applyManualLed();
    }
  } else if (command.startsWith("led_brightness:")) {
    manualLedBrightness = safeBrightness(command.substring(15).toInt());
    applyManualLed();
  }
}

// ============================================================
// BLE INIT
// ============================================================
void bleStart() {
  Serial.println("Starting BLE...");

  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);

  // Device-specific secrets are generated/read here: since the BT radio is on,
  // esp_random() hardware RNG returns real random values.
  prepareSecrets();

  // Security: bonding + MITM + Secure Connections. The device shows a new
  // PIN on the OLED for every pairing (onPassKeyRequest); the phone enters the code.
  // Static passkey is intentionally not set (default 123456 = callback path).
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  pSensorChar = pService->createCharacteristic(
    SENSOR_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY |
    NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN
  );
  pSensorChar->setValue("{}");

  pStatusChar = pService->createCharacteristic(
    STATUS_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY |
    NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN
  );
  pStatusChar->setValue("{}");

  pLedControlChar = pService->createCharacteristic(
    LED_CONTROL_CHAR_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
    NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN
  );
  pLedControlChar->setCallbacks(new LedControlCallbacks());

  pAlarmControlChar = pService->createCharacteristic(
    ALARM_CONTROL_CHAR_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
    NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN
  );
  pAlarmControlChar->setCallbacks(new AlarmControlCallbacks());

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(BLE_DEVICE_NAME);
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  // Advertising starts at the end of setup(): if setup screens/portal block setup()
  // and pairing starts earlier, the PIN cannot be shown on the OLED.

  Serial.println("BLE GATT Server ready: " + String(BLE_DEVICE_NAME));
}

// ============================================================
// BLE NOTIFY SEND
// ============================================================
void bleSendNotify() {
  if (!bleDeviceConnected) return;
  if (pServer == nullptr || pServer->getConnectedCount() == 0) return;
  // Since CCCD subscription can be made without encryption: no notify is sent
  // while any unverified connection exists.
  if (!bleAllSecure) return;
  // The flag may be up to 1 s old: also verify the live connection list.
  for (uint16_t h : pServer->getPeerDevices()) {
    NimBLEConnInfo connInfo = pServer->getPeerIDInfo(h);
    if (!connInfo.isEncrypted() || !connInfo.isAuthenticated()) return;
  }

  char sensorJson[192];
  snprintf(sensorJson, sizeof(sensorJson),
    "{\"t\":%.1f,\"h\":%.1f,\"l\":%.0f,\"eco2\":%u,\"tvoc\":%u,\"mic\":%d,\"de\":%d,\"sgp\":%d}",
    cacheTemperature,
    cacheHumidity,
    cacheLux,
    cacheEco2,
    cacheTvoc,
    micLevel,
    dhtError ? 1 : 0,
    sgp30Active ? 1 : 0
  );
  pSensorChar->setValue(sensorJson);
  pSensorChar->notify();

  char statusJson[256];
  char timeBuf[9] = "--:--:--";
  char sunriseBuf[6] = "--:--";
  char alarmBuf[6] = "--:--";

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }

  if (sunriseHour >= 0) {
    snprintf(sunriseBuf, sizeof(sunriseBuf), "%02d:%02d", sunriseHour, sunriseMinute);
    snprintf(alarmBuf, sizeof(alarmBuf), "%02d:%02d", alarmHour, alarmMinute);
  }

  snprintf(statusJson, sizeof(statusJson),
    "{\"s\":\"%s\",\"aa\":%d,\"ld\":\"%s\",\"lr\":%u,\"lg\":%u,\"lb\":%u,\"br\":%u,\"gd\":\"%s\",\"al\":\"%s\",\"up\":\"%s\",\"w\":%d}",
    timeBuf,
    alarmEnabled ? 1 : 0,
    ledModeText().c_str(),
    manualLedR,
    manualLedG,
    manualLedB,
    manualLedBrightness,
    sunriseBuf,
    alarmBuf,
    uptimeText().c_str(),
    wifiConnected ? 1 : 0
  );
  pStatusChar->setValue(statusJson);
  pStatusChar->notify();
}

String ledModeText() {
  if (activeLedState == SUNRISE) return "anim";
  if (activeLedState == STANDBY) return "tam";
  if (activeLedState == MANUAL_COLOR) return "manuel";
  return "off";
}

String buildSensorJson() {
  String json = "{";
  json += "\"sicaklik\":" + String(cacheTemperature, 1) + ",";
  json += "\"nem\":" + String(cacheHumidity, 1) + ",";
  json += "\"luks\":" + String(cacheLux, 1) + ",";
  json += "\"eco2\":" + String(cacheEco2) + ",";
  json += "\"tvoc\":" + String(cacheTvoc) + ",";
  json += "\"mic\":" + String(micLevel) + ",";
  json += "\"led_durum\":\"" + ledStatusText() + "\",";
  json += "\"led_modu\":\"" + ledModeText() + "\",";
  json += "\"led_r\":" + String(manualLedR) + ",";
  json += "\"led_g\":" + String(manualLedG) + ",";
  json += "\"led_b\":" + String(manualLedB) + ",";
  json += "\"led_brightness\":" + String(manualLedBrightness) + ",";
  json += "\"dht_hata\":" + String(dhtError ? "true" : "false") + ",";
  json += "\"sgp30_hazir\":" + String(sgp30Active ? "true" : "false") + ",";
  json += "\"saat\":\"" + timeText() + "\",";
  json += "\"alarm_aktif\":" + String(alarmEnabled ? "true" : "false") + ",";
  json += "\"uptime\":\"" + uptimeText() + "\",";
  json += "\"wifi_kopma\":" + String(wifiDropCount) + ",";
  json += "\"ble_bagli\":" + String(bleDeviceConnected ? "true" : "false") + ",";
  json += "\"ble_sayisi\":" + String(bleConnectionCount) + ",";

  if (sunriseHour >= 0) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", sunriseHour, sunriseMinute);
    json += "\"gunes_dogumu\":\"" + String(buf) + "\",";
    snprintf(buf, sizeof(buf), "%02d:%02d", alarmHour, alarmMinute);
    json += "\"alarm_saati\":\"" + String(buf) + "\"";
  } else {
    json += "\"gunes_dogumu\":\"--:--\",";
    json += "\"alarm_saati\":\"--:--\"";
  }

  json += "}";
  return json;
}

// ============================================================
// SECURITY HELPERS
// ============================================================
// Crockford base32 alphabet (no i, l, o, u): 32 chars -> 5 bits, unbiased.
static const char SECRET_ALPHABET[] = "0123456789abcdefghjkmnpqrstvwxyz";

static String randomSecret(size_t length) {
  String s;
  s.reserve(length + 1);
  for (size_t i = 0; i < length; i++) {
    s += SECRET_ALPHABET[esp_random() & 31];
  }
  return s;
}

// Reads the secret from NVS; generates and writes it if missing. Returns empty on write failure
// (a non-persistent secret is never used or shown -> the related feature stays off).
static String prepareSecret(const char *key, size_t length) {
  String secret = preferences.getString(key, "");
  if (secret.length() == length) return secret;
  secret = randomSecret(length);
  if (preferences.putString(key, secret) != secret.length()) {
    securityNvsError = true;
    return "";
  }
  secretsGenerated = true;
  return secret;
}

void prepareSecrets() {
  if (!preferences.begin("guvenlik", false)) {
    securityNvsError = true;
    apPassword = "";
    apiToken = "";
  } else {
    apPassword = prepareSecret("apPass", AP_PASSWORD_LENGTH);
    apiToken = prepareSecret("apiTok", API_TOKEN_LENGTH);
    preferences.end();
  }

  if (securityNvsError) {
    // Fail closed: with an empty token no HTTP/WS request is authorized,
    // without a password the setup portal is not opened (setup()).
    Serial.println("SECURITY ERROR: secrets could not be written to NVS; API and setup portal are disabled.");
    reportError("NVS SECURITY ERROR");
    secretsGenerated = false;
    return;
  }

  // Secrets are printed to Serial once, only on the first boot when they are generated.
  if (secretsGenerated) {
    Serial.println("=== FIRST SETUP - device-specific secrets (will not be printed again) ===");
    Serial.println("Setup AP password : " + apPassword);
    Serial.println("API token         : " + apiToken);
    Serial.println("BLE PIN           : a new one is generated on the OLED for every pairing");
    Serial.println("================================================================");
  } else {
    Serial.println("Security: device secrets loaded from NVS.");
  }
}

// Shows the secrets on the OLED on first boot (15 s); shows an error on NVS failure.
void showSecrets() {
  if (!oledActive) return;
  if (securityNvsError) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("SECURITY ERROR");
    display.println("NVS write failed");
    display.println("API + portal off");
    display.display();
    delay(5000);
    return;
  }
  if (!secretsGenerated) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("FIRST SETUP - note these");
  display.println("AP password:");
  display.println(apPassword);
  display.println("API token:");
  display.println(apiToken);
  display.display();
  delay(15000);
}

// Shows the PIN on the OLED during pairing; returns true if shown.
bool bleShowPinScreen() {
  uint32_t pin = blePairingPin;
  if (pin == 0) return false;
  if (millis() - blePairingStart >= BLE_PIN_DISPLAY_MS) {
    blePairingPin = 0;
    return false;
  }
  if (!oledActive) return false;
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("BLE pairing");
  display.println("Enter on phone:");
  display.setTextSize(2);
  display.setCursor(0, 28);
  display.printf("%06u", (unsigned)pin);
  display.setTextSize(1);
  display.display();
  return true;
}

// Drops BLE connections not verified in time; updates the notify flag.
void bleSecurityCheck() {
  if (pServer == nullptr) return;
  bool allSecure = true;
  std::vector<uint16_t> peers = pServer->getPeerDevices();
  for (uint16_t h : peers) {
    NimBLEConnInfo connInfo = pServer->getPeerIDInfo(h);
    if (connInfo.isEncrypted() && connInfo.isAuthenticated()) continue;
    allSecure = false;
    for (int i = 0; i < BLE_CONN_MAX; i++) {
      if (bleConnHandles[i] == h &&
          millis() - bleConnTimes[i] >= BLE_VERIFY_TIMEOUT_MS) {
        Serial.println("BLE: unverified connection timed out, disconnecting");
        pServer->disconnect(h);
      }
    }
  }
  bleAllSecure = allSecure && !peers.empty();
}

// Comparison that leaks no timing information except the length.
bool constantTimeEquals(const String &a, const String &b) {
  if (b.length() == 0 || a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); i++) {
    diff |= (uint8_t)(a[i] ^ b[i]);
  }
  return diff == 0;
}

// HTTP: "Authorization: Bearer <token>" header is required.
bool httpAuthorized(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Authorization")) return false;
  String value = request->header("Authorization");
  if (!value.startsWith("Bearer ")) return false;
  String token = value.substring(7);
  token.trim();
  return constantTimeEquals(token, apiToken);
}

static void sendUnauthorized(AsyncWebServerRequest *request) {
  AsyncWebServerResponse *response =
    request->beginResponse(401, "application/json", "{\"hata\":\"yetkisiz\"}");
  response->addHeader("WWW-Authenticate", "Bearer");
  request->send(response);
}

// WebSocket slot tables are shared between the async_tcp task and loop().
bool wsIsAuthorized(uint32_t id) {
  bool found = false;
  portENTER_CRITICAL(&wsLock);
  for (int i = 0; i < WS_AUTH_MAX; i++) {
    if (wsAuthIds[i] == id) { found = true; break; }
  }
  portEXIT_CRITICAL(&wsLock);
  return found;
}

bool wsAddAuthorized(uint32_t id) {
  bool added = false;
  portENTER_CRITICAL(&wsLock);
  for (int i = 0; i < WS_AUTH_MAX; i++) {
    if (wsAuthIds[i] == 0) {
      wsAuthIds[i] = id;
      added = true;
      break;
    }
  }
  portEXIT_CRITICAL(&wsLock);
  return added;
}

void wsRemoveAuthorized(uint32_t id) {
  portENTER_CRITICAL(&wsLock);
  for (int i = 0; i < WS_AUTH_MAX; i++) {
    if (wsAuthIds[i] == id) wsAuthIds[i] = 0;
  }
  portEXIT_CRITICAL(&wsLock);
}

bool wsAddPending(uint32_t id) {
  bool added = false;
  portENTER_CRITICAL(&wsLock);
  for (int i = 0; i < WS_PENDING_MAX; i++) {
    if (wsPendingIds[i] == 0) {
      wsPendingIds[i] = id;
      wsPendingTimes[i] = millis();
      added = true;
      break;
    }
  }
  portEXIT_CRITICAL(&wsLock);
  return added;
}

void wsRemovePending(uint32_t id) {
  portENTER_CRITICAL(&wsLock);
  for (int i = 0; i < WS_PENDING_MAX; i++) {
    if (wsPendingIds[i] == id) wsPendingIds[i] = 0;
  }
  portEXIT_CRITICAL(&wsLock);
}

// Closes sockets that do not send the token within WS_AUTH_TIMEOUT_MS (prevents slot exhaustion).
void wsCloseUnauthorized() {
  uint32_t toClose[WS_PENDING_MAX];
  int count = 0;
  unsigned long now = millis();
  portENTER_CRITICAL(&wsLock);
  for (int i = 0; i < WS_PENDING_MAX; i++) {
    if (wsPendingIds[i] != 0 && now - wsPendingTimes[i] >= WS_AUTH_TIMEOUT_MS) {
      toClose[count++] = wsPendingIds[i];
      wsPendingIds[i] = 0;
    }
  }
  portEXIT_CRITICAL(&wsLock);
  for (int i = 0; i < count; i++) {
    Serial.printf("WS #%u did not send token, closing\n", toClose[i]);
    ws.close(toClose[i]);
  }
}

// POST body (form) first, then query string.
int requestParamInt(AsyncWebServerRequest *request, const char *ad, int varsayilan) {
  if (request->hasParam(ad, true)) return request->getParam(ad, true)->value().toInt();
  if (request->hasParam(ad)) return request->getParam(ad)->value().toInt();
  return varsayilan;
}

void startWebServer() {
  if (webServerStarted) return;

  ws.onEvent(handleWsEvent);
  server.addHandler(&ws);

  // The UI page contains no data; the browser asks the user for the token.
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildHtmlPage());
  });

  server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!httpAuthorized(request)) { sendUnauthorized(request); return; }
    request->send(200, "application/json", buildSensorJson());
  });

  // State-changing endpoints are POST only.
  server.on("/api/led/on", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!httpAuthorized(request)) { sendUnauthorized(request); return; }
    bleHandleCommand("led_on");
    request->send(200, "application/json", "{\"durum\":\"baslatildi\"}");
  });

  server.on("/api/led/off", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!httpAuthorized(request)) { sendUnauthorized(request); return; }
    bleHandleCommand("led_off");
    request->send(200, "application/json", "{\"durum\":\"kapatildi\"}");
  });

  server.on("/api/led/set", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!httpAuthorized(request)) { sendUnauthorized(request); return; }
    int r = requestParamInt(request, "r", manualLedR);
    int g = requestParamInt(request, "g", manualLedG);
    int b = requestParamInt(request, "b", manualLedB);
    int brightness = requestParamInt(request, "brightness", manualLedBrightness);

    String command = "led_set:" + String(r) + "," + String(g) + "," + String(b) + "," + String(brightness);
    bleHandleCommand(command);
    request->send(200, "application/json", buildSensorJson());
  });

  server.on("/api/alarm/toggle", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!httpAuthorized(request)) { sendUnauthorized(request); return; }
    bleHandleCommand("alarm_toggle");
    String json = "{\"alarm_aktif\":" + String(alarmEnabled ? "true" : "false") + "}";
    request->send(200, "application/json", json);
  });

  server.begin();
  webServerStarted = true;
  Serial.println("Web server started!");
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-S3 Smart Room Monitor v2.0 ===");
  Serial.println("SGP30 + MAX4466 + single I2C bus, final release");
  Serial.print("Reset reason: ");
  Serial.println((int)esp_reset_reason());
  bootTime = millis();
  micCalibrationStart = millis();

  // 1. Read NVS settings
  nvsLoad();

  // 2. Basic sensor init
  pinMode(DHT_PIN, INPUT);
  pinMode(MIC_PIN, INPUT);
  dht.begin();

  // 3. Single I2C bus
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  // 4. OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    oledActive = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Starting v2.0");
    display.println("I2C: GPIO40/41");
    display.println("BLE: e371 OK");
    display.display();
  } else {
    oledActive = false;
    Serial.println(F("OLED display not found!"));
  }

  // 5. BH1750
  if (lightSensor.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    bh1750Active = true;
    Serial.println("BH1750 ready.");
  } else {
    bh1750Active = false;
    Serial.println(F("BH1750 not found!"));
    reportError("BH1750 ERROR");
  }

  // 6. SGP30
  if (sgp30.begin()) {
    sgp30Active = true;
    Serial.print("SGP30 ready. Serial no: ");
    Serial.print(sgp30.serialnumber[0], HEX);
    Serial.print(sgp30.serialnumber[1], HEX);
    Serial.println(sgp30.serialnumber[2], HEX);
    sgp30BaselineLoad();
  } else {
    sgp30Active = false;
    Serial.println(F("SGP30 not found!"));
    reportError("SGP30 ERROR");
  }

  // 7. FastLED
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  // First limit to 3000 mA on a 5V 5A adapter for presentation safety.
  FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_POWER_LIMIT_MA);
  FastLED.setBrightness(60);
  FastLED.clear();
  FastLED.show();

  // 8. Start BLE first, so it is active before WiFi
  bleStart();
  showSecrets();  // first boot only

  // 9. WiFiManager
  Serial.println("Starting WiFiManager...");
  if (oledActive) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi connecting...");
    display.println("AP: OrtamMonitor-Setup");
    display.println("BLE: Active");
    display.display();
  }

  WiFiManager wm;
  wm.setConnectTimeout(15);
  wm.setConfigPortalTimeout(120);
  // If the setup portal opens, show the WPA2 password on the OLED.
  wm.setAPCallback([](WiFiManager *) {
    if (!oledActive) return;
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Setup AP open");
    display.println("OrtamMonitor-Setup");
    display.println("Password:");
    display.println(apPassword);
    display.display();
  });

  // If the password could not be written to NVS, the portal never opens (stay closed instead of an open AP).
  if (apPassword.length() != AP_PASSWORD_LENGTH) {
    wm.setEnableConfigPortal(false);
  }

  // The setup AP opens with the device-specific password (WPA2).
  bool wmConnected = wm.autoConnect("OrtamMonitor-Setup", apPassword.c_str());

  if (wmConnected) {
    wifiConnected = true;
    Serial.println("WiFi connected! IP: " + WiFi.localIP().toString());
  } else {
    wifiConnected = false;
    Serial.println("WiFi connection failed. BLE fallback active.");
    reportError("WiFi ERROR");
  }

  // 10. NTP and sunrise
  if (wifiConnected) {
    setupClock();

    if (sunriseHour < 0) {
      fetchSunriseTime();
    } else {
      Serial.printf("Loaded from NVS -> Sunrise: %02d:%02d, Alarm: %02d:%02d\n",
                    sunriseHour, sunriseMinute, alarmHour, alarmMinute);
      fetchSunriseTime();
    }

    if (oledActive) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("WiFi connected!");
      display.print("IP: ");
      display.println(WiFi.localIP());
      display.println("BLE: Active");
      if (sunriseHour >= 0) {
        display.printf("Sunrise: %02d:%02d\n", sunriseHour, sunriseMinute);
        display.printf("Alarm: %02d:%02d %s", alarmHour, alarmMinute,
                       alarmEnabled ? "ON" : "OFF");
      }
      display.display();
      delay(2500);
    }
  }

  // 11. Web server
  if (wifiConnected) {
    startWebServer();
  }

  // 12. Watchdog
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
  esp_task_wdt_add(NULL);
  Serial.println("Watchdog active: " + String(WDT_TIMEOUT_SEC) + " s");

  // 13. BLE advertising last: loop() can now show the PIN screen.
  NimBLEDevice::startAdvertising();

  Serial.println("=== System ready: v2.0 ===\n");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  esp_task_wdt_reset();

  // Sensors: every 2 seconds
  if (millis() - lastReadTime >= 2000) {
    lastReadTime = millis();
    readSensors();
  }

  // LED animation: 20 ms
  if (millis() - lastLedUpdate >= 20) {
    lastLedUpdate = millis();
    manageLedAnimation();
  }

  // WebSocket: 500 ms
  if (wifiConnected && millis() - lastWsBroadcast >= 500) {
    lastWsBroadcast = millis();
    wsBroadcastVerisi();
    wsCloseUnauthorized();
    ws.cleanupClients();
  }

  // BLE security check: 1 second
  if (millis() - lastBleSecurityCheck >= 1000) {
    lastBleSecurityCheck = millis();
    bleSecurityCheck();
  }

  // BLE Notify: 2 seconds
  if (millis() - lastBleNotify >= BLE_NOTIFY_INTERVAL) {
    lastBleNotify = millis();
    bleSendNotify();
  }

  // Alarm: 30 seconds
  if (millis() - lastSunriseCheck >= 30000) {
    lastSunriseCheck = millis();
    checkAlarm();
  }

  // WiFi check: 10 seconds
  if (millis() - lastWifiCheck >= 10000) {
    lastWifiCheck = millis();
    wifiReconnect();
  }

  // OLED: 1 second
  if (millis() - lastOledUpdate >= 1000) {
    lastOledUpdate = millis();
    if (!bleShowPinScreen()) updateDisplay();  // PIN screen during pairing
  }

  // SGP30 baseline: save once per hour
  if (sgp30Active && millis() - lastSgp30BaselineSave >= 3600000UL) {
    lastSgp30BaselineSave = millis();
    sgp30BaselineSave();
  }
}

// ============================================================
// SENSOR READING
// ============================================================
void readSensors() {
  // DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    cacheTemperature = t;
    cacheHumidity = h;
    dhtError = false;
    dhtConsecutiveErrors = 0;
  } else {
    dhtError = true;
    dhtConsecutiveErrors++;
    if (dhtConsecutiveErrors == DHT_ERROR_THRESHOLD) {
      reportError("DHT22 NO RESPONSE");
    }
  }

  // BH1750
  if (bh1750Active) {
    float l = lightSensor.readLightLevel();
    if (l >= 0) {
      cacheLux = l;
    } else {
      reportError("BH1750 READ ERROR");
    }
  }

  // SGP30
  if (sgp30Active) {
    if (!dhtError) {
      sgp30.setHumidity(getAbsoluteHumidity(cacheTemperature, cacheHumidity));
    }

    if (sgp30.IAQmeasure()) {
      cacheTvoc = sgp30.TVOC;
      cacheEco2 = sgp30.eCO2;
    } else {
      reportError("SGP30 READ ERROR");
    }
  }

  // MAX4466
  readMicrophone();
}

// ============================================================
// MAX4466 MICROPHONE READING
// ============================================================
void readMicrophone() {
  const int SAMPLE_COUNT = 80;
  int minValue = 4095;
  int maxValue = 0;

  for (int i = 0; i < SAMPLE_COUNT; i++) {
    int reading = analogRead(MIC_PIN);
    if (reading < minValue) minValue = reading;
    if (reading > maxValue) maxValue = reading;
    delayMicroseconds(120);
  }

  int amplitude = maxValue - minValue;
  micRaw = amplitude;

  if (!micCalibrationDone) {
    if (amplitude < micMinCalibration) micMinCalibration = amplitude;
    if (amplitude > micMaxCalibration) micMaxCalibration = amplitude;

    if (millis() - micCalibrationStart >= MIC_CALIBRATION_MS) {
      micCalibrationDone = true;
      if (micMaxCalibration < micMinCalibration + 20) {
        micMaxCalibration = micMinCalibration + 300;
      }
      Serial.printf("MIC calibration done. Min:%d Max:%d\n", micMinCalibration, micMaxCalibration);
    }
  }

  int low = micMinCalibration + 5;
  int high = micMaxCalibration + 500;
  micLevel = map(amplitude, low, high, 0, 100);
  micLevel = constrain(micLevel, 0, 100);
}

// ============================================================
// SGP30 ABSOLUTE HUMIDITY CALCULATION
// Adafruit SGP30 setHumidity() expects absolute humidity in mg/m^3.
// ============================================================
uint32_t getAbsoluteHumidity(float temperature, float humidity) {
  const float absoluteHumidity = 216.7f *
    ((humidity / 100.0f) * 6.112f * exp((17.62f * temperature) / (243.12f + temperature)) /
    (273.15f + temperature));

  const uint32_t absoluteHumidityScaled = (uint32_t)(1000.0f * absoluteHumidity);
  return absoluteHumidityScaled;
}

// ============================================================
// SGP30 BASELINE LOAD / SAVE
// ============================================================
void sgp30BaselineLoad() {
  preferences.begin("monitor", true);
  uint16_t eco2Base = preferences.getUShort("eco2Base", 0);
  uint16_t tvocBase = preferences.getUShort("tvocBase", 0);
  preferences.end();

  if (eco2Base != 0 && tvocBase != 0) {
    sgp30.setIAQBaseline(eco2Base, tvocBase);
    Serial.printf("SGP30 baseline loaded. eCO2:0x%04X TVOC:0x%04X\n", eco2Base, tvocBase);
  } else {
    Serial.println("No SGP30 baseline. On first run the sensor may take time to settle.");
  }
}

void sgp30BaselineSave() {
  uint16_t eco2Base = 0;
  uint16_t tvocBase = 0;

  if (sgp30.getIAQBaseline(&eco2Base, &tvocBase)) {
    preferences.begin("monitor", false);
    preferences.putUShort("eco2Base", eco2Base);
    preferences.putUShort("tvocBase", tvocBase);
    preferences.end();
    Serial.printf("SGP30 baseline saved. eCO2:0x%04X TVOC:0x%04X\n", eco2Base, tvocBase);
  }
}

// ============================================================
// NVS LOAD
// ============================================================
void nvsLoad() {
  preferences.begin("monitor", true);
  alarmEnabled = preferences.getBool("alarmAktif", true);
  sunriseHour = preferences.getInt("sunriseH", -1);
  sunriseMinute = preferences.getInt("sunriseM", -1);
  alarmHour = preferences.getInt("alarmH", -1);
  alarmMinute = preferences.getInt("alarmM", -1);
  preferences.end();

  Serial.printf("NVS Load -> alarm:%s, sunrise:%02d:%02d, alarm:%02d:%02d\n",
    alarmEnabled ? "ON" : "OFF", sunriseHour, sunriseMinute, alarmHour, alarmMinute);
}

// ============================================================
// NVS SAVE
// ============================================================
void nvsSave() {
  preferences.begin("monitor", false);
  preferences.putBool("alarmAktif", alarmEnabled);
  if (sunriseHour >= 0) {
    preferences.putInt("sunriseH", sunriseHour);
    preferences.putInt("sunriseM", sunriseMinute);
    preferences.putInt("alarmH", alarmHour);
    preferences.putInt("alarmM", alarmMinute);
  }
  preferences.end();
  Serial.println("NVS saved.");
}

// ============================================================
// ERROR REPORTING
// ============================================================
void reportError(String message) {
  lastErrorMessage = message;
  lastErrorTime = millis();
  if (message.length() > 0) {
    Serial.println("ERROR: " + message);
  }
}

// ============================================================
// UPTIME TEXT
// ============================================================
String uptimeText() {
  unsigned long totalSec = (millis() - bootTime) / 1000;
  int hours = totalSec / 3600;
  int minutes = (totalSec % 3600) / 60;
  int seconds = totalSec % 60;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours, minutes, seconds);
  return String(buf);
}

// ============================================================
// NTP CLOCK SETUP
// ============================================================
void setupClock() {
  configTime(UTC_OFFSET_HOURS * 3600, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Waiting for NTP sync");
  struct tm timeinfo;
  int attempt = 0;
  while (!getLocalTime(&timeinfo) && attempt < 10) {
    Serial.print(".");
    delay(500);
    attempt++;
  }

  if (getLocalTime(&timeinfo)) {
    timeSynced = true;
    Serial.println("\nTime synced:");
    Serial.println(&timeinfo, "%d/%m/%Y %H:%M:%S");
  } else {
    timeSynced = false;
    Serial.println("\nNTP FAILED!");
    reportError("NTP ERROR");
  }
}

// ============================================================
// TIME TEXT
// ============================================================
String timeText() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--:--:--";
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(buf);
}

// ============================================================
// FETCH SUNRISE TIME
// ============================================================
void fetchSunriseTime() {
  if (WiFi.status() != WL_CONNECTED) return;

  String url = "https://api.sunrise-sunset.org/json?lat="
               + String(LATITUDE, 4) + "&lng=" + String(LONGITUDE, 4)
               + "&formatted=0";

  Serial.println("Sunrise API query: " + url);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("API response: " + response.substring(0, 200));

    int idx = response.indexOf("\"sunrise\":\"");
    if (idx > 0) {
      idx += 11;
      int tIdx = response.indexOf('T', idx);
      if (tIdx > 0) {
        int utcHour = response.substring(tIdx + 1, tIdx + 3).toInt();
        int utcMin = response.substring(tIdx + 4, tIdx + 6).toInt();

        sunriseHour = utcHour + UTC_OFFSET_HOURS;
        sunriseMinute = utcMin;
        if (sunriseHour >= 24) sunriseHour -= 24;
        if (sunriseHour < 0) sunriseHour += 24;

        alarmMinute = sunriseMinute - 30;
        alarmHour = sunriseHour;
        if (alarmMinute < 0) {
          alarmMinute += 60;
          alarmHour -= 1;
          if (alarmHour < 0) alarmHour += 24;
        }

        Serial.printf("Sunrise: %02d:%02d UTC%+d\n", sunriseHour, sunriseMinute, UTC_OFFSET_HOURS);
        Serial.printf("LED start: %02d:%02d\n", alarmHour, alarmMinute);

        nvsSave();
      }
    }
  } else {
    Serial.printf("Sunrise API error: %d\n", httpCode);
    reportError("Sunrise API:" + String(httpCode));
  }

  http.end();
}

// ============================================================
// WIFI RECONNECT CHECK
// ============================================================
void wifiReconnect() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.println("WiFi reconnected! IP: " + WiFi.localIP().toString());
      reportError("");

      if (!timeSynced) {
        setupClock();
      }

      if (sunriseHour < 0) {
        fetchSunriseTime();
      }
    }

    startWebServer();
    return;
  }

  if (wifiConnected) {
    wifiConnected = false;
    wifiDropCount++;
    Serial.printf("WiFi DROPPED! Total: %d, BLE fallback active\n", wifiDropCount);
    reportError("WiFi DROPPED #" + String(wifiDropCount));
  }

  if (millis() - lastWifiReconnectAttempt >= WIFI_RECONNECT_INTERVAL) {
    lastWifiReconnectAttempt = millis();
    Serial.println("Trying WiFi reconnect...");
    WiFi.mode(WIFI_STA);
    WiFi.reconnect();
  }
}

// ============================================================
// ALARM CHECK
// ============================================================
void checkAlarm() {
  if (!alarmEnabled || !timeSynced || sunriseHour < 0) return;
  if (manualLedControl) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int now = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int alarmMinutes = alarmHour * 60 + alarmMinute;

  if (timeinfo.tm_mday != lastAlarmDay) {
    alarmFiredToday = false;
  }

  if (timeinfo.tm_hour == 0 && timeinfo.tm_min >= 5) {
    fetchSunriseTime();
    lastAlarmDay = timeinfo.tm_mday;
  }

  if (!alarmFiredToday && now >= alarmMinutes && now < alarmMinutes + 35) {
    if (cacheLux < 50.0) {
      Serial.println(">>> ALARM: sunrise simulation starting! <<<");
      activeLedState = SUNRISE;
      ledStartTime = millis();
      alarmFiredToday = true;
      lastAlarmDay = timeinfo.tm_mday;
      updateDisplay();
    } else {
      Serial.println("Room is already bright, skipping alarm.");
      alarmFiredToday = true;
      lastAlarmDay = timeinfo.tm_mday;
    }
  }
}

// ============================================================
// LED STATUS TEXT
// ============================================================
String ledStatusText() {
  if (activeLedState == SUNRISE) return "Running (Anim)";
  if (activeLedState == STANDBY) return "Running (Full)";
  if (activeLedState == MANUAL_COLOR) return "Manual Color";
  return "Waiting (Off)";
}

// ============================================================
// LED ANIMATION
// ============================================================
void manageLedAnimation() {
  if (luxDemoActive && millis() >= luxDemoEnd) {
    luxDemoActive = false;
    manualLedBrightness = 100;
    fill_solid(leds, NUM_LEDS, CRGB(255, 197, 143));
    FastLED.setBrightness(manualLedBrightness);
    FastLED.show();
  }

  unsigned long elapsedMs = millis() - ledStartTime;

  if (activeLedState == SUNRISE) {
    float elapsedSec = elapsedMs / 1000.0;
    float minuteRatio = (elapsedSec / TOTAL_DURATION_SEC) * 30.0;

    if (minuteRatio >= 30.0) {
      fill_solid(leds, NUM_LEDS, CRGB(255, 197, 143));
      FastLED.setBrightness(LED_SAFE_BRIGHTNESS);
      FastLED.show();
      activeLedState = STANDBY;
      ledStartTime = millis();
    } else {
      CRGB currentColor;
      uint8_t currentBrightness;

      if (minuteRatio < 10.0) {
        float p = minuteRatio / 10.0;
        currentColor = blend(CRGB(255, 0, 0), CRGB(255, 60, 0), p * 255);
        currentBrightness = 60 + (140 - 60) * p;
      } else if (minuteRatio < 18.0) {
        float p = (minuteRatio - 10.0) / 8.0;
        currentColor = blend(CRGB(255, 60, 0), CRGB(255, 200, 50), p * 255);
        currentBrightness = 140 + (LED_SAFE_BRIGHTNESS - 140) * p;
      } else if (minuteRatio < 24.0) {
        float p = (minuteRatio - 18.0) / 6.0;
        currentColor = blend(CRGB(255, 200, 50), CRGB(255, 214, 170), p * 255);
        currentBrightness = LED_SAFE_BRIGHTNESS;
      } else {
        float p = (minuteRatio - 24.0) / 6.0;
        currentColor = blend(CRGB(255, 214, 170), CRGB(255, 197, 143), p * 255);
        currentBrightness = LED_SAFE_BRIGHTNESS;
      }

      fill_solid(leds, NUM_LEDS, currentColor);
      FastLED.setBrightness(currentBrightness);
      FastLED.show();
    }
  } else if (activeLedState == STANDBY) {
    if (elapsedMs >= 5000) {
      FastLED.clear();
      FastLED.show();
      activeLedState = DARK;
      manualLedControl = false;
    }
  }
}

// ============================================================
// OLED DISPLAY
// ============================================================
void updateDisplay() {
  if (!oledActive) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Row 1: Time + status icons
  display.setCursor(0, 0);
  if (timeSynced) display.print(timeText().substring(0, 5));
  else display.print("--:--");

  display.setCursor(64, 0);
  display.print(wifiConnected ? "W+" : "W-");

  display.setCursor(80, 0);
  display.print(bleDeviceConnected ? "B+" : "Bx");

  display.setCursor(96, 0);
  if (dhtConsecutiveErrors >= DHT_ERROR_THRESHOLD) display.print("D!");
  else if (dhtError) display.print("D?");
  else display.print("D+");

  display.setCursor(114, 0);
  display.print(alarmEnabled ? "A+" : "A-");

  // Row 2: Temperature / humidity
  display.setCursor(0, 12);
  if (!dhtError) {
    display.print(cacheTemperature, 1);
    display.print("C ");
    display.print(cacheHumidity, 0);
    display.print("% ");
  } else {
    display.print("DHT:");
    display.print(dhtConsecutiveErrors);
    display.print("x error");
  }

  // Row 3: Light / sound
  display.setCursor(0, 24);
  display.print(cacheLux, 0);
  display.print("lx ");
  display.print("Mic:");
  display.print(micLevel);
  display.print("%");

  // Row 4: Air quality
  display.setCursor(0, 36);
  if (sgp30Active) {
    display.print("CO2:");
    display.print(cacheEco2);
    display.print(" TVOC:");
    display.print(cacheTvoc);
  } else {
    display.print("no SGP30");
  }

  // Divider
  display.drawFastHLine(0, 48, SCREEN_WIDTH, SSD1306_WHITE);

  // Row 5: LED / error / uptime
  display.setCursor(0, 52);
  if (lastErrorMessage.length() > 0 && (millis() - lastErrorTime < 10000)) {
    display.print("!");
    display.print(lastErrorMessage.substring(0, 19));
  } else {
    if (activeLedState == SUNRISE) {
      unsigned long elapsed = (millis() - ledStartTime) / 1000;
      int remaining = max(0, (int)(TOTAL_DURATION_SEC - elapsed));
      display.printf("LED:%dm%ds ", remaining / 60, remaining % 60);
    } else if (activeLedState == STANDBY) {
      display.print("LED:FUL ");
    } else if (activeLedState == MANUAL_COLOR) {
      display.print("LED:MAN ");
    } else {
      display.print("LED:- ");
    }

    if (sunriseHour >= 0) {
      display.printf("SR:%02d:%02d", sunriseHour, sunriseMinute);
    } else {
      display.print(uptimeText());
    }
  }

  display.display();
}

// ============================================================
// WEBSOCKET EVENTS
// ============================================================
void handleWsEvent(AsyncWebSocket *svr, AsyncWebSocketClient *client,
                    AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    wsRemoveAuthorized(client->id());
    Serial.printf("WS #%u connected\n", client->id());
    // 5 s allowed for the token; closed immediately if no pending slot is free.
    if (!wsAddPending(client->id())) {
      client->close();
    }
  } else if (type == WS_EVT_DISCONNECT) {
    wsRemoveAuthorized(client->id());
    wsRemovePending(client->id());
    Serial.printf("WS #%u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (!wsIsAuthorized(client->id()) &&
        !(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) {
      client->close();  // fragmented/binary frames from unauthorized clients are not accepted
      return;
    }
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String command;
      command.reserve(len + 1);
      for (size_t i = 0; i < len; i++) {
        command += (char)data[i];
      }
      command.trim();

      // The first message must be the token; otherwise the socket is closed. The token is not logged.
      if (!wsIsAuthorized(client->id())) {
        wsRemovePending(client->id());
        if (constantTimeEquals(command, apiToken) && wsAddAuthorized(client->id())) {
          Serial.printf("WS #%u authorized\n", client->id());
          client->text("auth_ok");
        } else {
          Serial.printf("WS #%u unauthorized, closing\n", client->id());
          client->text("auth_fail");
          client->close();
        }
        return;
      }

      Serial.print("WS command: ");
      Serial.println(command);
      bleHandleCommand(command);
    }
  }
}

// ============================================================
// WEBSOCKET BROADCAST
// ============================================================
void wsBroadcastVerisi() {
  if (ws.count() == 0) return;
  // Sensor data is sent only to clients that sent the token.
  String json = buildSensorJson();
  for (int i = 0; i < WS_AUTH_MAX; i++) {
    if (wsAuthIds[i] != 0) ws.text(wsAuthIds[i], json);
  }
}

// ============================================================
// WEB UI
// ============================================================
String buildHtmlPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Room Monitor v2</title>
  <style>
    *{margin:0;padding:0;box-sizing:border-box}
    body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#0f0f0f;color:#e0e0e0;min-height:100vh;padding:16px}
    h1{text-align:center;font-size:1.3em;color:#ff9f43;margin-bottom:4px;font-weight:600}
    .clock{text-align:center;font-size:2em;font-weight:700;color:#fff;margin-bottom:12px;font-variant-numeric:tabular-nums}
    .grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;max-width:460px;margin:0 auto}
    .card{background:#1a1a2e;border-radius:14px;padding:14px;text-align:center}
    .card.wide{grid-column:1/-1}
    .card-label{font-size:.7em;color:#888;text-transform:uppercase;letter-spacing:1px;margin-bottom:6px}
    .card-value{font-size:1.65em;font-weight:700}
    .card-unit{font-size:.45em;color:#888;font-weight:400}
    .temp{color:#ff6b6b}.hum{color:#48dbfb}.lux{color:#feca57}.air{color:#55efc4}.mic{color:#a29bfe}
    .led-status{color:#1dd1a1;font-size:1em;font-weight:600}
    .btn-group{display:flex;gap:8px;margin-top:10px;justify-content:center;flex-wrap:wrap}
    .btn{padding:10px 22px;border:none;border-radius:10px;font-size:.9em;font-weight:600;cursor:pointer;transition:transform .1s}
    .btn:active{transform:scale(.95)}
    .btn-on{background:#ff9f43;color:#000}
    .btn-off{background:#333;color:#fff}
    .btn-alarm{background:#6c5ce7;color:#fff}
    .btn-alarm.off{background:#2d3436;color:#888}
    .led-controls{margin-top:12px;display:grid;gap:10px;justify-items:center}
    .color-input{width:64px;height:38px;border:0;border-radius:8px;background:#333;cursor:pointer}
    .range-row{width:100%;max-width:320px;display:grid;grid-template-columns:70px 1fr 38px;gap:8px;align-items:center;font-size:.8em;color:#aaa}
    .range-row input{width:100%;accent-color:#ff9f43}
    .range-row span:last-child{color:#fff;font-variant-numeric:tabular-nums;text-align:right}
    .sunrise-info{display:flex;justify-content:center;gap:20px;margin-top:8px;font-size:.8em;color:#aaa}
    .sunrise-info span{color:#feca57;font-weight:600}
    .status-bar{text-align:center;margin-top:12px;font-size:.7em;color:#555}
    .dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:4px;background:#e74c3c}
    .dot.ok{background:#2ecc71}
    .dot.ble{background:#0984e3}
    .error-text{color:#e74c3c;font-size:1.1em}
    .sys-info{max-width:460px;margin:12px auto 0;display:flex;justify-content:center;gap:16px;font-size:.7em;color:#555;flex-wrap:wrap}
    .sys-info span{color:#888}
  </style>
</head>
<body>
  <h1>Smart Room Monitor v2</h1>
  <div class="clock" id="clock">--:--:--</div>

  <div class="grid">
    <div class="card">
      <div class="card-label">Temperature</div>
      <div class="card-value temp" id="temperature">--<span class="card-unit"> &deg;C</span></div>
    </div>
    <div class="card">
      <div class="card-label">Nem</div>
      <div class="card-value hum" id="humidity">--<span class="card-unit"> %</span></div>
    </div>
    <div class="card">
      <div class="card-label">Ambient Light</div>
      <div class="card-value lux" id="lux">--<span class="card-unit"> lx</span></div>
    </div>
    <div class="card">
      <div class="card-label">Sound Level</div>
      <div class="card-value mic" id="mic">--<span class="card-unit"> %</span></div>
    </div>
    <div class="card">
      <div class="card-label">eCO2</div>
      <div class="card-value air" id="eco2">--<span class="card-unit"> ppm</span></div>
    </div>
    <div class="card">
      <div class="card-label">TVOC</div>
      <div class="card-value air" id="tvoc">--<span class="card-unit"> ppb</span></div>
    </div>
    <div class="card wide">
      <div class="card-label">LED & Alarm</div>
      <div class="led-status" id="ledStatus">--</div>
      <div class="sunrise-info">
        <div>Sunrise: <span id="sunriseTime">--:--</span></div>
        <div>Alarm: <span id="alarmTime">--:--</span></div>
      </div>
      <div class="led-controls">
        <input class="color-input" type="color" id="ledColor" value="#ffb450" oninput="colorChanged()">
        <div class="range-row">
          <span>Brightness</span>
          <input type="range" id="ledBrightness" min="0" max="160" value="128" oninput="brightnessChanged()">
          <span id="brightnessValue">128</span>
        </div>
      </div>
      <div class="btn-group">
        <button class="btn btn-on" onclick="send('led_on')">Sunrise</button>
        <button class="btn btn-on" onclick="send('lux_demo')">Lux Demo</button>
        <button class="btn btn-off" onclick="send('led_off')">Off</button>
        <button class="btn btn-alarm" id="alarmBtn" onclick="send('alarm_toggle')">Alarm ON</button>
      </div>
    </div>
  </div>

  <div class="sys-info">
    <div>Uptime: <span id="uptime">--:--:--</span></div>
    <div>WiFi drops: <span id="wifiDrops">0</span></div>
    <div>BLE: <span id="bleState">--</span></div>
  </div>

  <div class="status-bar">
    <span class="dot" id="wsDot"></span>
    <span id="wsStatus">Connecting...</span>
    &nbsp;&nbsp;
    <span class="dot ble" id="bleDot"></span>
    <span id="bleStatus">--</span>
  </div>

  <script>
    let ws, reconnectTimer;
    let ledR = 255, ledG = 180, ledB = 80, ledBrightness = 128;
    let ledSendTimer = null;
    let apiToken = null;
    try { apiToken = localStorage.getItem('ortamToken'); } catch(e) {}

    // Device API token: shown on Serial/OLED on first boot; asked for once here.
    function getToken() {
      if (!apiToken) {
        apiToken = (prompt('Device API token') || '').trim();
        try { localStorage.setItem('ortamToken', apiToken); } catch(e) {}
      }
      return apiToken;
    }

    function connect() {
      ws = new WebSocket('ws://' + location.host + '/ws');
      ws.onopen = function() {
        ws.send(getToken());
        document.getElementById('wsDot').className = 'dot ok';
        document.getElementById('wsStatus').textContent = 'WiFi connected';
        clearTimeout(reconnectTimer);
      };
      ws.onclose = function() {
        document.getElementById('wsDot').className = 'dot';
        document.getElementById('wsStatus').textContent = 'Retrying...';
        reconnectTimer = setTimeout(connect, 3000);
      };
      ws.onerror = function() { ws.close(); };
      ws.onmessage = function(e) {
        if (e.data === 'auth_ok') return;
        if (e.data === 'auth_fail') {
          apiToken = null;
          try { localStorage.removeItem('ortamToken'); } catch(err) {}
          return;
        }
        try {
          let d = JSON.parse(e.data);
          document.getElementById('clock').textContent = d.saat || '--:--:--';

          if (d.dht_hata) {
            document.getElementById('temperature').innerHTML = '<span class="error-text">Error</span>';
            document.getElementById('humidity').innerHTML = '<span class="error-text">Error</span>';
          } else {
            document.getElementById('temperature').innerHTML = Number(d.sicaklik).toFixed(1) + '<span class="card-unit"> &deg;C</span>';
            document.getElementById('humidity').innerHTML = Number(d.nem).toFixed(1) + '<span class="card-unit"> %</span>';
          }

          document.getElementById('lux').innerHTML = Number(d.luks).toFixed(0) + '<span class="card-unit"> lx</span>';
          document.getElementById('mic').innerHTML = Number(d.mic).toFixed(0) + '<span class="card-unit"> %</span>';
          document.getElementById('eco2').innerHTML = Number(d.eco2).toFixed(0) + '<span class="card-unit"> ppm</span>';
          document.getElementById('tvoc').innerHTML = Number(d.tvoc).toFixed(0) + '<span class="card-unit"> ppb</span>';

          document.getElementById('ledStatus').textContent = d.led_durum;
          document.getElementById('sunriseTime').textContent = d.gunes_dogumu || '--:--';
          document.getElementById('alarmTime').textContent = d.alarm_saati || '--:--';
          document.getElementById('uptime').textContent = d.uptime || '--:--:--';
          document.getElementById('wifiDrops').textContent = d.wifi_kopma || 0;
          ledR = Number(d.led_r ?? ledR);
          ledG = Number(d.led_g ?? ledG);
          ledB = Number(d.led_b ?? ledB);
          ledBrightness = Math.max(0, Math.min(160, Number(d.led_brightness ?? ledBrightness)));
          document.getElementById('ledColor').value = rgbToHex(ledR, ledG, ledB);
          document.getElementById('ledBrightness').value = ledBrightness;
          document.getElementById('brightnessValue').textContent = ledBrightness;

          let ab = document.getElementById('alarmBtn');
          if (d.alarm_aktif) { ab.textContent = 'Alarm ON'; ab.className = 'btn btn-alarm'; }
          else { ab.textContent = 'Alarm OFF'; ab.className = 'btn btn-alarm off'; }

          let bleStateEl = document.getElementById('bleState');
          let bleDot = document.getElementById('bleDot');
          let bleStatusEl = document.getElementById('bleStatus');
          if (d.ble_bagli) {
            bleStateEl.textContent = 'Connected (' + d.ble_sayisi + ')';
            bleDot.className = 'dot ble';
            bleStatusEl.textContent = 'BLE connected';
          } else {
            bleStateEl.textContent = 'Waiting';
            bleDot.className = 'dot';
            bleStatusEl.textContent = 'BLE Advertise';
          }
        } catch(err) {}
      };
    }

    function send(k) {
      if (ws && ws.readyState === WebSocket.OPEN) ws.send(k);
    }

    function rgbToHex(r, g, b) {
      return '#' + [r, g, b].map(function(v) {
        v = Math.max(0, Math.min(255, Number(v) || 0));
        return v.toString(16).padStart(2, '0');
      }).join('');
    }

    function hexToRgb(hex) {
      let v = parseInt(hex.slice(1), 16);
      return { r: (v >> 16) & 255, g: (v >> 8) & 255, b: v & 255 };
    }

    function ledSetSend() {
      ledBrightness = Math.max(0, Math.min(160, Number(ledBrightness) || 0));
      send('led_set:' + ledR + ',' + ledG + ',' + ledB + ',' + ledBrightness);
    }

    function ledSetSchedule() {
      clearTimeout(ledSendTimer);
      ledSendTimer = setTimeout(ledSetSend, 150);
    }

    function colorChanged() {
      let rgb = hexToRgb(document.getElementById('ledColor').value);
      ledR = rgb.r; ledG = rgb.g; ledB = rgb.b;
      ledSetSchedule();
    }

    function brightnessChanged() {
      ledBrightness = Math.max(0, Math.min(160, Number(document.getElementById('ledBrightness').value) || 0));
      document.getElementById('brightnessValue').textContent = ledBrightness;
      ledSetSchedule();
    }

    connect();
  </script>
</body>
</html>
)rawliteral";
  return html;
}
