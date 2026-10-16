/*
 * ESP32-S3 Smart Environment Monitor v8.0 - Presentation Version
 *
 * This version is the expanded presentation form of the previous v7 logic.
 * Additions:
 * - SGP30 TVOC / eCO2 air quality sensor
 * - MAX4466 analog microphone module
 * - Single I2C bus: OLED + BH1750 + SGP30 on the same bus
 * - New pin plan: mainly uses the other side of the ESP32-S3
 *
 * Target board:
 * - Board: ESP32-S3 Dev Module
 * - ESP32 Arduino Core: 2.0.17
 *
 * Recommended library versions:
 * 1. ESP Async WebServer by ESP32Async: 3.10.3
 * 2. Async TCP by ESP32Async: 3.4.10
 * 3. FastLED: 3.6.0, if problems occur, use 3.7.0
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
 * - All GND lines must be connected together.
 * - OLED, BH1750, SGP30, DHT22 and MAX4466 must be powered at 3.3V.
 * - The WS2812B LED strip must be powered by an external 5V supply.
 * - The WS2812B data line must be wired as ESP32 GPIO47 -> logic shifter LV -> HV -> 330R resistor -> DIN.
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
// KONUM - CONFIG: set to your own location before flashing.
// (Public copy: the field build's city-level coordinates (Istanbul city centre) were
//  replaced with a neutral placeholder; otherwise identical to the owner's last saved source file.)
// ============================================================
const float ENLEM  = 0.0;     // latitude,  decimal degrees
const float BOYLAM = 0.0;     // longitude, decimal degrees

// ============================================================
// PIN DEFINITIONS - Presentation version
// ============================================================
#define I2C_SDA      40
#define I2C_SCL      41
#define DHT_PIN      42
#define MIC_PIN       1
#define LED_PIN      47

// ============================================================
// OLED SCREEN
// ============================================================
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define OLED_ADDRESS  0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledCalisiyor = false;

// ============================================================
// SENSORS
// ============================================================
#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);

BH1750 isikSensoru;
bool bh1750Calisiyor = false;

Adafruit_SGP30 sgp30;
bool sgp30Calisiyor = false;

// ============================================================
// MAX4466 MICROPHONE
// ============================================================
int micHam = 0;
int micSeviye = 0;           // normalized sound level, 0-100
int micMinKalibrasyon = 4095;
int micMaxKalibrasyon = 0;
unsigned long micKalibrasyonBaslangic = 0;
const unsigned long MIC_KALIBRASYON_MS = 3000;
bool micKalibrasyonTamam = false;

// ============================================================
// LED
// ============================================================
#define NUM_LEDS     60
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
CRGB leds[NUM_LEDS];
float TOPLAM_SURE_SN = 1800.0;  // 30-minute sunrise simulation
const uint16_t LED_GUC_LIMIT_MA = 4000;
const uint8_t LED_GUVENLI_PARLAKLIK = 160;
const uint8_t LED_DEMO_BOOST_PARLAKLIK = 180;
const unsigned long LED_DEMO_BOOST_MS = 8000;
bool luxDemoAktif = false;
unsigned long luxDemoBitis = 0;

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

bool bleCihazBagli = false;
int  bleBaglantiSayisi = 0;
unsigned long sonBleNotify = 0;
const unsigned long BLE_NOTIFY_ARALIK = 2000;

// ============================================================
// TIMERS
// ============================================================
unsigned long sonOkumaZamani = 0;
unsigned long ledBaslangicZamani = 0;
unsigned long sonLedGuncelleme = 0;
unsigned long sonWsBroadcast = 0;
unsigned long sonSunriseKontrol = 0;
unsigned long sonWifiKontrol = 0;
unsigned long sonOledGuncelleme = 0;
unsigned long sonSgp30BaselineKayit = 0;

// Sensor cache
float cacheSicaklik = 0;
float cacheNem = 0;
float cacheLuks = 0;
uint16_t cacheEco2 = 0;
uint16_t cacheTvoc = 0;
bool dhtHatasi = true;
int dhtArdisikHata = 0;
const int DHT_HATA_ESIK = 5;

// WiFi and time
bool wifiBagli = false;
bool saatSenkron = false;
int wifiKopmaSayisi = 0;
bool webSunucusuBaslatildi = false;
unsigned long sonWifiReconnectDenemesi = 0;
const unsigned long WIFI_RECONNECT_ARALIK = 30000;

// Sunrise alarm system
int sunriseHour = -1;
int sunriseMinute = -1;
int alarmHour = -1;
int alarmMinute = -1;
bool bugunAlarmCalisti = false;
int sonAlarmGunu = -1;
bool alarmAktif = true;
bool manuelLedKontrol = false;

enum LedDurumu { GUN_DOGUMU, BEKLEME, KARANLIK, MANUEL_RENK };
LedDurumu aktifLedDurumu = KARANLIK;

uint8_t manuelLedR = 255;
uint8_t manuelLedG = 180;
uint8_t manuelLedB = 80;
uint8_t manuelLedParlaklik = 128;

// Error / status messages
String sonHataMesaji = "";
unsigned long sonHataZamani = 0;

// Uptime
unsigned long bootZamani = 0;

// ============================================================
// FUNCTION PROTOTYPES
// ============================================================
void sensorleriOku();
void mikrofonuOku();
void ekraniGuncelle();
void ledAnimasyonunuYonet();
void wsBroadcastVerisi();
void wsOlaylariIsle(AsyncWebSocket *svr, AsyncWebSocketClient *client,
                    AwsEventType type, void *arg, uint8_t *data, size_t len);
String ledDurumMetni();
String htmlSayfasiOlustur();
void saatKur();
void gunesDogusunuCek();
void alarmKontrol();
String saatMetni();
void nvsKaydet();
void nvsOku();
void wifiYenidenBaglan();
void webSunucusunuBaslat();
void hataBildir(String mesaj);
String uptimeMetni();
void bleBaslat();
void bleNotifyGonder();
void bleKomutIsle(String komut);
String sensorJsonOlustur();
String ledModuMetni();
void manuelLedUygula();
uint8_t byteDegerineSinirla(int deger);
void sgp30BaselineKaydet();
void sgp30BaselineOku();
uint32_t getAbsoluteHumidity(float temperature, float humidity);

// ============================================================
// BLE CALLBACK CLASSES - compatible with NimBLE 1.4.3
// ============================================================
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer) {
    bleCihazBagli = true;
    bleBaglantiSayisi++;
    Serial.printf("BLE: Cihaz baglandi, toplam: %d\n", bleBaglantiSayisi);
    NimBLEDevice::startAdvertising();
  }

  void onDisconnect(NimBLEServer* pServer) {
    bleCihazBagli = (pServer->getConnectedCount() > 0);
    Serial.println("BLE: Cihaz ayrildi");
    NimBLEDevice::startAdvertising();
  }
};

class LedControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      String komut = String(value.c_str());
      komut.trim();
      Serial.print("BLE LED komut: ");
      Serial.println(komut);
      bleKomutIsle(komut);
    }
  }
};

class AlarmControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() > 0) {
      String komut = String(value.c_str());
      komut.trim();
      Serial.print("BLE Alarm komut: ");
      Serial.println(komut);
      bleKomutIsle(komut);
    }
  }
};

// ============================================================
// BLE COMMAND PROCESSING
// ============================================================
uint8_t byteDegerineSinirla(int deger) {
  return (uint8_t)constrain(deger, 0, 255);
}

uint8_t guvenliParlaklik(int deger) {
  return (uint8_t)constrain(deger, 0, LED_GUVENLI_PARLAKLIK);
}

void manuelLedUygula() {
  luxDemoAktif = false;
  manuelLedKontrol = true;
  aktifLedDurumu = MANUEL_RENK;
  fill_solid(leds, NUM_LEDS, CRGB(manuelLedR, manuelLedG, manuelLedB));
  FastLED.setBrightness(manuelLedParlaklik);
  FastLED.show();
}

void bleKomutIsle(String komut) {
  if (komut == "led_on") {
    manuelLedKontrol = true;
    aktifLedDurumu = GUN_DOGUMU;
    ledBaslangicZamani = millis();
  } else if (komut == "led_off") {
    luxDemoAktif = false;
    manuelLedKontrol = false;
    FastLED.clear();
    FastLED.show();
    aktifLedDurumu = KARANLIK;
  } else if (komut == "lux_demo") {
    luxDemoAktif = true;
    luxDemoBitis = millis() + LED_DEMO_BOOST_MS;
    manuelLedKontrol = true;
    aktifLedDurumu = MANUEL_RENK;
    manuelLedR = 255;
    manuelLedG = 255;
    manuelLedB = 220;
    manuelLedParlaklik = LED_DEMO_BOOST_PARLAKLIK;
    fill_solid(leds, NUM_LEDS, CRGB(manuelLedR, manuelLedG, manuelLedB));
    FastLED.setBrightness(manuelLedParlaklik);
    FastLED.show();
  } else if (komut == "alarm_toggle") {
    alarmAktif = !alarmAktif;
    nvsKaydet();
  } else if (komut.startsWith("led_set:")) {
    String payload = komut.substring(8);
    int p1 = payload.indexOf(',');
    int p2 = payload.indexOf(',', p1 + 1);
    int p3 = payload.indexOf(',', p2 + 1);

    if (p1 > 0 && p2 > p1 && p3 > p2) {
      manuelLedR = byteDegerineSinirla(payload.substring(0, p1).toInt());
      manuelLedG = byteDegerineSinirla(payload.substring(p1 + 1, p2).toInt());
      manuelLedB = byteDegerineSinirla(payload.substring(p2 + 1, p3).toInt());
      manuelLedParlaklik = guvenliParlaklik(payload.substring(p3 + 1).toInt());
      manuelLedUygula();
    }
  } else if (komut.startsWith("led_color:")) {
    String payload = komut.substring(10);
    int p1 = payload.indexOf(',');
    int p2 = payload.indexOf(',', p1 + 1);

    if (p1 > 0 && p2 > p1) {
      manuelLedR = byteDegerineSinirla(payload.substring(0, p1).toInt());
      manuelLedG = byteDegerineSinirla(payload.substring(p1 + 1, p2).toInt());
      manuelLedB = byteDegerineSinirla(payload.substring(p2 + 1).toInt());
      manuelLedUygula();
    }
  } else if (komut.startsWith("led_brightness:")) {
    manuelLedParlaklik = guvenliParlaklik(komut.substring(15).toInt());
    manuelLedUygula();
  }
}

// ============================================================
// BLE INITIALIZATION
// ============================================================
void bleBaslat() {
  Serial.println("BLE baslatiliyor...");

  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  pSensorChar = pService->createCharacteristic(
    SENSOR_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  pSensorChar->setValue("{}");

  pStatusChar = pService->createCharacteristic(
    STATUS_CHAR_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  pStatusChar->setValue("{}");

  pLedControlChar = pService->createCharacteristic(
    LED_CONTROL_CHAR_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  pLedControlChar->setCallbacks(new LedControlCallbacks());

  pAlarmControlChar = pService->createCharacteristic(
    ALARM_CONTROL_CHAR_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  pAlarmControlChar->setCallbacks(new AlarmControlCallbacks());

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setName(BLE_DEVICE_NAME);
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  NimBLEDevice::startAdvertising();

  Serial.println("BLE GATT Server hazir: " + String(BLE_DEVICE_NAME));
}

// ============================================================
// BLE NOTIFY SEND
// ============================================================
void bleNotifyGonder() {
  if (!bleCihazBagli) return;
  if (pServer == nullptr || pServer->getConnectedCount() == 0) return;

  char sensorJson[192];
  snprintf(sensorJson, sizeof(sensorJson),
    "{\"t\":%.1f,\"h\":%.1f,\"l\":%.0f,\"eco2\":%u,\"tvoc\":%u,\"mic\":%d,\"de\":%d,\"sgp\":%d}",
    cacheSicaklik,
    cacheNem,
    cacheLuks,
    cacheEco2,
    cacheTvoc,
    micSeviye,
    dhtHatasi ? 1 : 0,
    sgp30Calisiyor ? 1 : 0
  );
  pSensorChar->setValue(sensorJson);
  pSensorChar->notify();

  char durumJson[256];
  char saatBuf[9] = "--:--:--";
  char gdBuf[6] = "--:--";
  char alBuf[6] = "--:--";

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    snprintf(saatBuf, sizeof(saatBuf), "%02d:%02d:%02d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }

  if (sunriseHour >= 0) {
    snprintf(gdBuf, sizeof(gdBuf), "%02d:%02d", sunriseHour, sunriseMinute);
    snprintf(alBuf, sizeof(alBuf), "%02d:%02d", alarmHour, alarmMinute);
  }

  snprintf(durumJson, sizeof(durumJson),
    "{\"s\":\"%s\",\"aa\":%d,\"ld\":\"%s\",\"lr\":%u,\"lg\":%u,\"lb\":%u,\"br\":%u,\"gd\":\"%s\",\"al\":\"%s\",\"up\":\"%s\",\"w\":%d}",
    saatBuf,
    alarmAktif ? 1 : 0,
    ledModuMetni().c_str(),
    manuelLedR,
    manuelLedG,
    manuelLedB,
    manuelLedParlaklik,
    gdBuf,
    alBuf,
    uptimeMetni().c_str(),
    wifiBagli ? 1 : 0
  );
  pStatusChar->setValue(durumJson);
  pStatusChar->notify();
}

String ledModuMetni() {
  if (aktifLedDurumu == GUN_DOGUMU) return "anim";
  if (aktifLedDurumu == BEKLEME) return "tam";
  if (aktifLedDurumu == MANUEL_RENK) return "manuel";
  return "off";
}

String sensorJsonOlustur() {
  String json = "{";
  json += "\"sicaklik\":" + String(cacheSicaklik, 1) + ",";
  json += "\"nem\":" + String(cacheNem, 1) + ",";
  json += "\"luks\":" + String(cacheLuks, 1) + ",";
  json += "\"eco2\":" + String(cacheEco2) + ",";
  json += "\"tvoc\":" + String(cacheTvoc) + ",";
  json += "\"mic\":" + String(micSeviye) + ",";
  json += "\"led_durum\":\"" + ledDurumMetni() + "\",";
  json += "\"led_modu\":\"" + ledModuMetni() + "\",";
  json += "\"led_r\":" + String(manuelLedR) + ",";
  json += "\"led_g\":" + String(manuelLedG) + ",";
  json += "\"led_b\":" + String(manuelLedB) + ",";
  json += "\"led_brightness\":" + String(manuelLedParlaklik) + ",";
  json += "\"dht_hata\":" + String(dhtHatasi ? "true" : "false") + ",";
  json += "\"sgp30_hazir\":" + String(sgp30Calisiyor ? "true" : "false") + ",";
  json += "\"saat\":\"" + saatMetni() + "\",";
  json += "\"alarm_aktif\":" + String(alarmAktif ? "true" : "false") + ",";
  json += "\"uptime\":\"" + uptimeMetni() + "\",";
  json += "\"wifi_kopma\":" + String(wifiKopmaSayisi) + ",";
  json += "\"ble_bagli\":" + String(bleCihazBagli ? "true" : "false") + ",";
  json += "\"ble_sayisi\":" + String(bleBaglantiSayisi) + ",";

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

void webSunucusunuBaslat() {
  if (webSunucusuBaslatildi) return;

  ws.onEvent(wsOlaylariIsle);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", htmlSayfasiOlustur());
  });

  server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", sensorJsonOlustur());
  });

  server.on("/api/led/on", HTTP_GET, [](AsyncWebServerRequest *request) {
    bleKomutIsle("led_on");
    request->send(200, "application/json", "{\"durum\":\"baslatildi\"}");
  });

  server.on("/api/led/off", HTTP_GET, [](AsyncWebServerRequest *request) {
    bleKomutIsle("led_off");
    request->send(200, "application/json", "{\"durum\":\"kapatildi\"}");
  });

  server.on("/api/led/set", HTTP_GET, [](AsyncWebServerRequest *request) {
    int r = manuelLedR;
    int g = manuelLedG;
    int b = manuelLedB;
    int brightness = manuelLedParlaklik;

    if (request->hasParam("r")) r = request->getParam("r")->value().toInt();
    if (request->hasParam("g")) g = request->getParam("g")->value().toInt();
    if (request->hasParam("b")) b = request->getParam("b")->value().toInt();
    if (request->hasParam("brightness")) brightness = request->getParam("brightness")->value().toInt();

    String komut = "led_set:" + String(r) + "," + String(g) + "," + String(b) + "," + String(brightness);
    bleKomutIsle(komut);
    request->send(200, "application/json", sensorJsonOlustur());
  });

  server.on("/api/alarm/toggle", HTTP_GET, [](AsyncWebServerRequest *request) {
    bleKomutIsle("alarm_toggle");
    String json = "{\"alarm_aktif\":" + String(alarmAktif ? "true" : "false") + "}";
    request->send(200, "application/json", json);
  });

  server.begin();
  webSunucusuBaslatildi = true;
  Serial.println("Web sunucusu baslatildi!");
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-S3 Akilli Ortam Monitoru v8.0 ===");
  Serial.println("SGP30 + MAX4466 + tek I2C hatli nihai surum");
  Serial.print("Reset nedeni: ");
  Serial.println((int)esp_reset_reason());
  bootZamani = millis();
  micKalibrasyonBaslangic = millis();

  // 1. Read NVS settings
  nvsOku();

  // 2. Basic sensor startup
  pinMode(DHT_PIN, INPUT);
  pinMode(MIC_PIN, INPUT);
  dht.begin();

  // 3. Single I2C bus
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  // 4. OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    oledCalisiyor = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Baslatiliyor v8.0");
    display.println("I2C: GPIO40/41");
    display.println("BLE: e371 OK");
    display.display();
  } else {
    oledCalisiyor = false;
    Serial.println(F("OLED Ekran Bulunamadi!"));
  }

  // 5. BH1750
  if (isikSensoru.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    bh1750Calisiyor = true;
    Serial.println("BH1750 hazir.");
  } else {
    bh1750Calisiyor = false;
    Serial.println(F("BH1750 Bulunamadi!"));
    hataBildir("BH1750 HATA");
  }

  // 6. SGP30
  if (sgp30.begin()) {
    sgp30Calisiyor = true;
    Serial.print("SGP30 hazir. Seri no: ");
    Serial.print(sgp30.serialnumber[0], HEX);
    Serial.print(sgp30.serialnumber[1], HEX);
    Serial.println(sgp30.serialnumber[2], HEX);
    sgp30BaselineOku();
  } else {
    sgp30Calisiyor = false;
    Serial.println(F("SGP30 Bulunamadi!"));
    hataBildir("SGP30 HATA");
  }

  // 7. FastLED
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  // 5V 5A adapter: for presentation safety, limit to 3000 mA first.
  FastLED.setMaxPowerInVoltsAndMilliamps(5, LED_GUC_LIMIT_MA);
  FastLED.setBrightness(60);
  FastLED.clear();
  FastLED.show();

  // 8. Start BLE first so it is active before WiFi
  bleBaslat();

  // 9. WiFiManager
  Serial.println("WiFiManager baslatiliyor...");
  if (oledCalisiyor) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi Baglaniyor...");
    display.println("AP: OrtamMonitor-Setup");
    display.println("BLE: Aktif");
    display.display();
  }

  WiFiManager wm;
  wm.setConnectTimeout(15);
  wm.setConfigPortalTimeout(120);

  bool wmBaglandi = wm.autoConnect("OrtamMonitor-Setup");

  if (wmBaglandi) {
    wifiBagli = true;
    Serial.println("WiFi Baglandi! IP: " + WiFi.localIP().toString());
  } else {
    wifiBagli = false;
    Serial.println("WiFi baglanamadi. BLE fallback aktif.");
    hataBildir("WiFi HATA");
  }

  // 10. NTP and sunrise
  if (wifiBagli) {
    saatKur();

    if (sunriseHour < 0) {
      gunesDogusunuCek();
    } else {
      Serial.printf("NVS'den yuklendi -> Gunes: %02d:%02d, Alarm: %02d:%02d\n",
                    sunriseHour, sunriseMinute, alarmHour, alarmMinute);
      gunesDogusunuCek();
    }

    if (oledCalisiyor) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("WiFi Bagli!");
      display.print("IP: ");
      display.println(WiFi.localIP());
      display.println("BLE: Aktif");
      if (sunriseHour >= 0) {
        display.printf("Gunes: %02d:%02d\n", sunriseHour, sunriseMinute);
        display.printf("Alarm: %02d:%02d %s", alarmHour, alarmMinute,
                       alarmAktif ? "ON" : "OFF");
      }
      display.display();
      delay(2500);
    }
  }

  // 11. Web server
  if (wifiBagli) {
    webSunucusunuBaslat();
  }

  // 12. Watchdog
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
  esp_task_wdt_add(NULL);
  Serial.println("Watchdog aktif: " + String(WDT_TIMEOUT_SEC) + " sn");

  Serial.println("=== Sistem Hazir: v8.0 ===\n");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  esp_task_wdt_reset();

  // Sensors: every 2 seconds
  if (millis() - sonOkumaZamani >= 2000) {
    sonOkumaZamani = millis();
    sensorleriOku();
  }

  // LED animation: 20 ms
  if (millis() - sonLedGuncelleme >= 20) {
    sonLedGuncelleme = millis();
    ledAnimasyonunuYonet();
  }

  // WebSocket: 500 ms
  if (wifiBagli && millis() - sonWsBroadcast >= 500) {
    sonWsBroadcast = millis();
    wsBroadcastVerisi();
    ws.cleanupClients();
  }

  // BLE Notify: 2 seconds
  if (millis() - sonBleNotify >= BLE_NOTIFY_ARALIK) {
    sonBleNotify = millis();
    bleNotifyGonder();
  }

  // Alarm: 30 seconds
  if (millis() - sonSunriseKontrol >= 30000) {
    sonSunriseKontrol = millis();
    alarmKontrol();
  }

  // WiFi check: 10 seconds
  if (millis() - sonWifiKontrol >= 10000) {
    sonWifiKontrol = millis();
    wifiYenidenBaglan();
  }

  // OLED: 1 second
  if (millis() - sonOledGuncelleme >= 1000) {
    sonOledGuncelleme = millis();
    ekraniGuncelle();
  }

  // SGP30 baseline: save once per hour
  if (sgp30Calisiyor && millis() - sonSgp30BaselineKayit >= 3600000UL) {
    sonSgp30BaselineKayit = millis();
    sgp30BaselineKaydet();
  }
}

// ============================================================
// SENSOR READING
// ============================================================
void sensorleriOku() {
  // DHT22
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    cacheSicaklik = t;
    cacheNem = h;
    dhtHatasi = false;
    dhtArdisikHata = 0;
  } else {
    dhtHatasi = true;
    dhtArdisikHata++;
    if (dhtArdisikHata == DHT_HATA_ESIK) {
      hataBildir("DHT22 YANIT YOK");
    }
  }

  // BH1750
  if (bh1750Calisiyor) {
    float l = isikSensoru.readLightLevel();
    if (l >= 0) {
      cacheLuks = l;
    } else {
      hataBildir("BH1750 OKUMA HATA");
    }
  }

  // SGP30
  if (sgp30Calisiyor) {
    if (!dhtHatasi) {
      sgp30.setHumidity(getAbsoluteHumidity(cacheSicaklik, cacheNem));
    }

    if (sgp30.IAQmeasure()) {
      cacheTvoc = sgp30.TVOC;
      cacheEco2 = sgp30.eCO2;
    } else {
      hataBildir("SGP30 OKUMA HATA");
    }
  }

  // MAX4466
  mikrofonuOku();
}

// ============================================================
// MAX4466 MICROPHONE READING
// ============================================================
void mikrofonuOku() {
  const int ORNEK_SAYISI = 80;
  int minDeger = 4095;
  int maxDeger = 0;

  for (int i = 0; i < ORNEK_SAYISI; i++) {
    int okuma = analogRead(MIC_PIN);
    if (okuma < minDeger) minDeger = okuma;
    if (okuma > maxDeger) maxDeger = okuma;
    delayMicroseconds(120);
  }

  int genlik = maxDeger - minDeger;
  micHam = genlik;

  if (!micKalibrasyonTamam) {
    if (genlik < micMinKalibrasyon) micMinKalibrasyon = genlik;
    if (genlik > micMaxKalibrasyon) micMaxKalibrasyon = genlik;

    if (millis() - micKalibrasyonBaslangic >= MIC_KALIBRASYON_MS) {
      micKalibrasyonTamam = true;
      if (micMaxKalibrasyon < micMinKalibrasyon + 20) {
        micMaxKalibrasyon = micMinKalibrasyon + 300;
      }
      Serial.printf("MIC kalibrasyon tamam. Min:%d Max:%d\n", micMinKalibrasyon, micMaxKalibrasyon);
    }
  }

  int alt = micMinKalibrasyon + 5;
  int ust = micMaxKalibrasyon + 500;
  micSeviye = map(genlik, alt, ust, 0, 100);
  micSeviye = constrain(micSeviye, 0, 100);
}

// ============================================================
// SGP30 ABSOLUTE HUMIDITY CALCULATION
// Adafruit SGP30 setHumidity() function expects absolute humidity in mg/m^3.
// ============================================================
uint32_t getAbsoluteHumidity(float temperature, float humidity) {
  const float absoluteHumidity = 216.7f *
    ((humidity / 100.0f) * 6.112f * exp((17.62f * temperature) / (243.12f + temperature)) /
    (273.15f + temperature));

  const uint32_t absoluteHumidityScaled = (uint32_t)(1000.0f * absoluteHumidity);
  return absoluteHumidityScaled;
}

// ============================================================
// SGP30 BASELINE READ / SAVE
// ============================================================
void sgp30BaselineOku() {
  preferences.begin("monitor", true);
  uint16_t eco2Base = preferences.getUShort("eco2Base", 0);
  uint16_t tvocBase = preferences.getUShort("tvocBase", 0);
  preferences.end();

  if (eco2Base != 0 && tvocBase != 0) {
    sgp30.setIAQBaseline(eco2Base, tvocBase);
    Serial.printf("SGP30 baseline yuklendi. eCO2:0x%04X TVOC:0x%04X\n", eco2Base, tvocBase);
  } else {
    Serial.println("SGP30 baseline yok. Ilk calismada sensorun oturmasi zaman alabilir.");
  }
}

void sgp30BaselineKaydet() {
  uint16_t eco2Base = 0;
  uint16_t tvocBase = 0;

  if (sgp30.getIAQBaseline(&eco2Base, &tvocBase)) {
    preferences.begin("monitor", false);
    preferences.putUShort("eco2Base", eco2Base);
    preferences.putUShort("tvocBase", tvocBase);
    preferences.end();
    Serial.printf("SGP30 baseline kaydedildi. eCO2:0x%04X TVOC:0x%04X\n", eco2Base, tvocBase);
  }
}

// ============================================================
// NVS READ
// ============================================================
void nvsOku() {
  preferences.begin("monitor", true);
  alarmAktif = preferences.getBool("alarmAktif", true);
  sunriseHour = preferences.getInt("sunriseH", -1);
  sunriseMinute = preferences.getInt("sunriseM", -1);
  alarmHour = preferences.getInt("alarmH", -1);
  alarmMinute = preferences.getInt("alarmM", -1);
  preferences.end();

  Serial.printf("NVS Oku -> alarm:%s, sunrise:%02d:%02d, alarm:%02d:%02d\n",
    alarmAktif ? "ON" : "OFF", sunriseHour, sunriseMinute, alarmHour, alarmMinute);
}

// ============================================================
// NVS SAVE
// ============================================================
void nvsKaydet() {
  preferences.begin("monitor", false);
  preferences.putBool("alarmAktif", alarmAktif);
  if (sunriseHour >= 0) {
    preferences.putInt("sunriseH", sunriseHour);
    preferences.putInt("sunriseM", sunriseMinute);
    preferences.putInt("alarmH", alarmHour);
    preferences.putInt("alarmM", alarmMinute);
  }
  preferences.end();
  Serial.println("NVS kaydedildi.");
}

// ============================================================
// ERROR REPORTING
// ============================================================
void hataBildir(String mesaj) {
  sonHataMesaji = mesaj;
  sonHataZamani = millis();
  if (mesaj.length() > 0) {
    Serial.println("HATA: " + mesaj);
  }
}

// ============================================================
// UPTIME TEXT
// ============================================================
String uptimeMetni() {
  unsigned long sn = (millis() - bootZamani) / 1000;
  int saat = sn / 3600;
  int dk = (sn % 3600) / 60;
  int s = sn % 60;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", saat, dk, s);
  return String(buf);
}

// ============================================================
// NTP TIME SETUP
// ============================================================
void saatKur() {
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("NTP senkronizasyonu bekleniyor");
  struct tm timeinfo;
  int deneme = 0;
  while (!getLocalTime(&timeinfo) && deneme < 10) {
    Serial.print(".");
    delay(500);
    deneme++;
  }

  if (getLocalTime(&timeinfo)) {
    saatSenkron = true;
    Serial.println("\nSaat senkron:");
    Serial.println(&timeinfo, "%d/%m/%Y %H:%M:%S");
  } else {
    saatSenkron = false;
    Serial.println("\nNTP BASARISIZ!");
    hataBildir("NTP HATA");
  }
}

// ============================================================
// TIME TEXT
// ============================================================
String saatMetni() {
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
void gunesDogusunuCek() {
  if (WiFi.status() != WL_CONNECTED) return;

  String url = "https://api.sunrise-sunset.org/json?lat="
               + String(ENLEM, 4) + "&lng=" + String(BOYLAM, 4)
               + "&formatted=0";

  Serial.println("Gunes dogusu API sorgusu: " + url);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(10000);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String yanit = http.getString();
    Serial.println("API Yanit: " + yanit.substring(0, 200));

    int idx = yanit.indexOf("\"sunrise\":\"");
    if (idx > 0) {
      idx += 11;
      int tIdx = yanit.indexOf('T', idx);
      if (tIdx > 0) {
        int utcHour = yanit.substring(tIdx + 1, tIdx + 3).toInt();
        int utcMin = yanit.substring(tIdx + 4, tIdx + 6).toInt();

        sunriseHour = utcHour + 3;
        sunriseMinute = utcMin;
        if (sunriseHour >= 24) sunriseHour -= 24;

        alarmMinute = sunriseMinute - 30;
        alarmHour = sunriseHour;
        if (alarmMinute < 0) {
          alarmMinute += 60;
          alarmHour -= 1;
          if (alarmHour < 0) alarmHour += 24;
        }

        Serial.printf("Gunes dogusu: %02d:%02d UTC+3\n", sunriseHour, sunriseMinute);
        Serial.printf("LED baslama: %02d:%02d\n", alarmHour, alarmMinute);

        nvsKaydet();
      }
    }
  } else {
    Serial.printf("Sunrise API hatasi: %d\n", httpCode);
    hataBildir("Sunrise API:" + String(httpCode));
  }

  http.end();
}

// ============================================================
// WIFI RECONNECTION CHECK
// ============================================================
void wifiYenidenBaglan() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiBagli) {
      wifiBagli = true;
      Serial.println("WiFi yeniden baglandi! IP: " + WiFi.localIP().toString());
      hataBildir("");

      if (!saatSenkron) {
        saatKur();
      }

      if (sunriseHour < 0) {
        gunesDogusunuCek();
      }
    }

    webSunucusunuBaslat();
    return;
  }

  if (wifiBagli) {
    wifiBagli = false;
    wifiKopmaSayisi++;
    Serial.printf("WiFi KOPTU! Toplam: %d, BLE fallback aktif\n", wifiKopmaSayisi);
    hataBildir("WiFi KOPTU #" + String(wifiKopmaSayisi));
  }

  if (millis() - sonWifiReconnectDenemesi >= WIFI_RECONNECT_ARALIK) {
    sonWifiReconnectDenemesi = millis();
    Serial.println("WiFi reconnect deneniyor...");
    WiFi.mode(WIFI_STA);
    WiFi.reconnect();
  }
}

// ============================================================
// ALARM CHECK
// ============================================================
void alarmKontrol() {
  if (!alarmAktif || !saatSenkron || sunriseHour < 0) return;
  if (manuelLedKontrol) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int simdi = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  int alarmDakika = alarmHour * 60 + alarmMinute;

  if (timeinfo.tm_mday != sonAlarmGunu) {
    bugunAlarmCalisti = false;
  }

  if (timeinfo.tm_hour == 0 && timeinfo.tm_min >= 5) {
    gunesDogusunuCek();
    sonAlarmGunu = timeinfo.tm_mday;
  }

  if (!bugunAlarmCalisti && simdi >= alarmDakika && simdi < alarmDakika + 35) {
    if (cacheLuks < 50.0) {
      Serial.println(">>> ALARM: Gun dogumu simulasyonu basliyor! <<<");
      aktifLedDurumu = GUN_DOGUMU;
      ledBaslangicZamani = millis();
      bugunAlarmCalisti = true;
      sonAlarmGunu = timeinfo.tm_mday;
      ekraniGuncelle();
    } else {
      Serial.println("Oda zaten aydinlik, alarm atlaniyor.");
      bugunAlarmCalisti = true;
      sonAlarmGunu = timeinfo.tm_mday;
    }
  }
}

// ============================================================
// LED STATUS TEXT
// ============================================================
String ledDurumMetni() {
  if (aktifLedDurumu == GUN_DOGUMU) return "Calisiyor (Anim)";
  if (aktifLedDurumu == BEKLEME) return "Calisiyor (Tam)";
  if (aktifLedDurumu == MANUEL_RENK) return "Manuel Renk";
  return "Bekliyor (Kapali)";
}

// ============================================================
// LED ANIMATION
// ============================================================
void ledAnimasyonunuYonet() {
  if (luxDemoAktif && millis() >= luxDemoBitis) {
    luxDemoAktif = false;
    manuelLedParlaklik = 100;
    fill_solid(leds, NUM_LEDS, CRGB(255, 197, 143));
    FastLED.setBrightness(manuelLedParlaklik);
    FastLED.show();
  }

  unsigned long gecenZamanMs = millis() - ledBaslangicZamani;

  if (aktifLedDurumu == GUN_DOGUMU) {
    float gecenSaniye = gecenZamanMs / 1000.0;
    float dakikaOrani = (gecenSaniye / TOPLAM_SURE_SN) * 30.0;

    if (dakikaOrani >= 30.0) {
      fill_solid(leds, NUM_LEDS, CRGB(255, 197, 143));
      FastLED.setBrightness(LED_GUVENLI_PARLAKLIK);
      FastLED.show();
      aktifLedDurumu = BEKLEME;
      ledBaslangicZamani = millis();
    } else {
      CRGB anlikRenk;
      uint8_t anlikParlaklik;

      if (dakikaOrani < 10.0) {
        float p = dakikaOrani / 10.0;
        anlikRenk = blend(CRGB(255, 0, 0), CRGB(255, 60, 0), p * 255);
        anlikParlaklik = 60 + (140 - 60) * p;
      } else if (dakikaOrani < 18.0) {
        float p = (dakikaOrani - 10.0) / 8.0;
        anlikRenk = blend(CRGB(255, 60, 0), CRGB(255, 200, 50), p * 255);
        anlikParlaklik = 140 + (LED_GUVENLI_PARLAKLIK - 140) * p;
      } else if (dakikaOrani < 24.0) {
        float p = (dakikaOrani - 18.0) / 6.0;
        anlikRenk = blend(CRGB(255, 200, 50), CRGB(255, 214, 170), p * 255);
        anlikParlaklik = LED_GUVENLI_PARLAKLIK;
      } else {
        float p = (dakikaOrani - 24.0) / 6.0;
        anlikRenk = blend(CRGB(255, 214, 170), CRGB(255, 197, 143), p * 255);
        anlikParlaklik = LED_GUVENLI_PARLAKLIK;
      }

      fill_solid(leds, NUM_LEDS, anlikRenk);
      FastLED.setBrightness(anlikParlaklik);
      FastLED.show();
    }
  } else if (aktifLedDurumu == BEKLEME) {
    if (gecenZamanMs >= 5000) {
      FastLED.clear();
      FastLED.show();
      aktifLedDurumu = KARANLIK;
      manuelLedKontrol = false;
    }
  }
}

// ============================================================
// OLED SCREEN
// ============================================================
void ekraniGuncelle() {
  if (!oledCalisiyor) return;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Row 1: Time + status icons
  display.setCursor(0, 0);
  if (saatSenkron) display.print(saatMetni().substring(0, 5));
  else display.print("--:--");

  display.setCursor(64, 0);
  display.print(wifiBagli ? "W+" : "W-");

  display.setCursor(80, 0);
  display.print(bleCihazBagli ? "B+" : "Bx");

  display.setCursor(96, 0);
  if (dhtArdisikHata >= DHT_HATA_ESIK) display.print("D!");
  else if (dhtHatasi) display.print("D?");
  else display.print("D+");

  display.setCursor(114, 0);
  display.print(alarmAktif ? "A+" : "A-");

  // Row 2: Temperature / humidity
  display.setCursor(0, 12);
  if (!dhtHatasi) {
    display.print(cacheSicaklik, 1);
    display.print("C ");
    display.print(cacheNem, 0);
    display.print("% ");
  } else {
    display.print("DHT:");
    display.print(dhtArdisikHata);
    display.print("x hata");
  }

  // Row 3: Light / sound
  display.setCursor(0, 24);
  display.print(cacheLuks, 0);
  display.print("lx ");
  display.print("Mic:");
  display.print(micSeviye);
  display.print("%");

  // Row 4: Air quality
  display.setCursor(0, 36);
  if (sgp30Calisiyor) {
    display.print("CO2:");
    display.print(cacheEco2);
    display.print(" TVOC:");
    display.print(cacheTvoc);
  } else {
    display.print("SGP30 yok");
  }

  // Divider line
  display.drawFastHLine(0, 48, SCREEN_WIDTH, SSD1306_WHITE);

  // Row 5: LED / error / uptime
  display.setCursor(0, 52);
  if (sonHataMesaji.length() > 0 && (millis() - sonHataZamani < 10000)) {
    display.print("!");
    display.print(sonHataMesaji.substring(0, 19));
  } else {
    if (aktifLedDurumu == GUN_DOGUMU) {
      unsigned long gecen = (millis() - ledBaslangicZamani) / 1000;
      int kalan = max(0, (int)(TOPLAM_SURE_SN - gecen));
      display.printf("LED:%dm%ds ", kalan / 60, kalan % 60);
    } else if (aktifLedDurumu == BEKLEME) {
      display.print("LED:TAM ");
    } else if (aktifLedDurumu == MANUEL_RENK) {
      display.print("LED:MAN ");
    } else {
      display.print("LED:- ");
    }

    if (sunriseHour >= 0) {
      display.printf("GD:%02d:%02d", sunriseHour, sunriseMinute);
    } else {
      display.print(uptimeMetni());
    }
  }

  display.display();
}

// ============================================================
// WEBSOCKET EVENTS
// ============================================================
void wsOlaylariIsle(AsyncWebSocket *svr, AsyncWebSocketClient *client,
                    AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WS #%u baglandi\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WS #%u ayrildi\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String komut;
      komut.reserve(len + 1);
      for (size_t i = 0; i < len; i++) {
        komut += (char)data[i];
      }
      komut.trim();
      Serial.print("WS Komut: ");
      Serial.println(komut);
      bleKomutIsle(komut);
    }
  }
}

// ============================================================
// WEBSOCKET BROADCAST
// ============================================================
void wsBroadcastVerisi() {
  if (ws.count() == 0) return;
  ws.textAll(sensorJsonOlustur());
}

// ============================================================
// WEB INTERFACE
// ============================================================
String htmlSayfasiOlustur() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="tr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Ortam Monitoru v8</title>
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
  <h1>Akilli Ortam Monitoru v8</h1>
  <div class="clock" id="saat">--:--:--</div>

  <div class="grid">
    <div class="card">
      <div class="card-label">Sicaklik</div>
      <div class="card-value temp" id="sicaklik">--<span class="card-unit"> &deg;C</span></div>
    </div>
    <div class="card">
      <div class="card-label">Nem</div>
      <div class="card-value hum" id="nem">--<span class="card-unit"> %</span></div>
    </div>
    <div class="card">
      <div class="card-label">Ortam Isigi</div>
      <div class="card-value lux" id="luks">--<span class="card-unit"> lx</span></div>
    </div>
    <div class="card">
      <div class="card-label">Ses Seviyesi</div>
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
      <div class="led-status" id="ledDurum">--</div>
      <div class="sunrise-info">
        <div>Gunes: <span id="gunesDogumu">--:--</span></div>
        <div>Alarm: <span id="alarmSaati">--:--</span></div>
      </div>
      <div class="led-controls">
        <input class="color-input" type="color" id="ledColor" value="#ffb450" oninput="renkDegisti()">
        <div class="range-row">
          <span>Parlaklik</span>
          <input type="range" id="ledBrightness" min="0" max="160" value="128" oninput="parlaklikDegisti()">
          <span id="brightnessValue">128</span>
        </div>
      </div>
      <div class="btn-group">
        <button class="btn btn-on" onclick="gonder('led_on')">Gun Dogumu</button>
        <button class="btn btn-on" onclick="gonder('lux_demo')">Lux Demo</button>
        <button class="btn btn-off" onclick="gonder('led_off')">Kapat</button>
        <button class="btn btn-alarm" id="alarmBtn" onclick="gonder('alarm_toggle')">Alarm ON</button>
      </div>
    </div>
  </div>

  <div class="sys-info">
    <div>Uptime: <span id="uptime">--:--:--</span></div>
    <div>WiFi kopma: <span id="wifiKopma">0</span></div>
    <div>BLE: <span id="bleDurum">--</span></div>
  </div>

  <div class="status-bar">
    <span class="dot" id="wsDot"></span>
    <span id="wsStatus">Baglaniyor...</span>
    &nbsp;&nbsp;
    <span class="dot ble" id="bleDot"></span>
    <span id="bleStatus">--</span>
  </div>

  <script>
    let ws, reconnectTimer;
    let ledR = 255, ledG = 180, ledB = 80, ledBrightness = 128;
    let ledSendTimer = null;

    function baglan() {
      ws = new WebSocket('ws://' + location.host + '/ws');
      ws.onopen = function() {
        document.getElementById('wsDot').className = 'dot ok';
        document.getElementById('wsStatus').textContent = 'WiFi Bagli';
        clearTimeout(reconnectTimer);
      };
      ws.onclose = function() {
        document.getElementById('wsDot').className = 'dot';
        document.getElementById('wsStatus').textContent = 'Tekrar deneniyor...';
        reconnectTimer = setTimeout(baglan, 3000);
      };
      ws.onerror = function() { ws.close(); };
      ws.onmessage = function(e) {
        try {
          let d = JSON.parse(e.data);
          document.getElementById('saat').textContent = d.saat || '--:--:--';

          if (d.dht_hata) {
            document.getElementById('sicaklik').innerHTML = '<span class="error-text">Hata</span>';
            document.getElementById('nem').innerHTML = '<span class="error-text">Hata</span>';
          } else {
            document.getElementById('sicaklik').innerHTML = Number(d.sicaklik).toFixed(1) + '<span class="card-unit"> &deg;C</span>';
            document.getElementById('nem').innerHTML = Number(d.nem).toFixed(1) + '<span class="card-unit"> %</span>';
          }

          document.getElementById('luks').innerHTML = Number(d.luks).toFixed(0) + '<span class="card-unit"> lx</span>';
          document.getElementById('mic').innerHTML = Number(d.mic).toFixed(0) + '<span class="card-unit"> %</span>';
          document.getElementById('eco2').innerHTML = Number(d.eco2).toFixed(0) + '<span class="card-unit"> ppm</span>';
          document.getElementById('tvoc').innerHTML = Number(d.tvoc).toFixed(0) + '<span class="card-unit"> ppb</span>';

          document.getElementById('ledDurum').textContent = d.led_durum;
          document.getElementById('gunesDogumu').textContent = d.gunes_dogumu || '--:--';
          document.getElementById('alarmSaati').textContent = d.alarm_saati || '--:--';
          document.getElementById('uptime').textContent = d.uptime || '--:--:--';
          document.getElementById('wifiKopma').textContent = d.wifi_kopma || 0;
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

          let bleD = document.getElementById('bleDurum');
          let bleDot = document.getElementById('bleDot');
          let bleS = document.getElementById('bleStatus');
          if (d.ble_bagli) {
            bleD.textContent = 'Bagli (' + d.ble_sayisi + ')';
            bleDot.className = 'dot ble';
            bleS.textContent = 'BLE Bagli';
          } else {
            bleD.textContent = 'Bekliyor';
            bleDot.className = 'dot';
            bleS.textContent = 'BLE Advertise';
          }
        } catch(err) {}
      };
    }

    function gonder(k) {
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

    function ledSetGonder() {
      ledBrightness = Math.max(0, Math.min(160, Number(ledBrightness) || 0));
      gonder('led_set:' + ledR + ',' + ledG + ',' + ledB + ',' + ledBrightness);
    }

    function ledSetPlanla() {
      clearTimeout(ledSendTimer);
      ledSendTimer = setTimeout(ledSetGonder, 150);
    }

    function renkDegisti() {
      let rgb = hexToRgb(document.getElementById('ledColor').value);
      ledR = rgb.r; ledG = rgb.g; ledB = rgb.b;
      ledSetPlanla();
    }

    function parlaklikDegisti() {
      ledBrightness = Math.max(0, Math.min(160, Number(document.getElementById('ledBrightness').value) || 0));
      document.getElementById('brightnessValue').textContent = ledBrightness;
      ledSetPlanla();
    }

    baglan();
  </script>
</body>
</html>
)rawliteral";
  return html;
}
