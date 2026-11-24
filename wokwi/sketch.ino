#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "secrets.h"

// ---------- PIN ----------
#define PIN_PIR    13
#define PIN_LDR    34
#define PIN_DHT    15
#define PIN_LAMPU  25
#define PIN_AC     26
#define PIN_BUZZER 27
#define RELAY_AKTIF HIGH            // ubah ke LOW bila relay terbalik

// ---------- PARAMETER (justifikasi di laporan) ----------
const unsigned long MS_PER_MENIT = 1000;  // 1 detik nyata = 1 menit simulasi
const int   T_OKUPASI_MENIT  = 10;
const int   T_TENGGANG_MENIT = 5;
const float SUHU_ON  = 26.0;
const float SUHU_OFF = 25.0;
const float LUX_TARGET = 300.0;
const int   DUTY_MIN = 20;
const int   DUTY_TENGGANG = 30;
const int   AC_JEDA_MIN = 3;
const float P_LAMPU_KW = 0.216;
const float P_AC_KW    = 1.56;
const float TARIF_KWH  = 1699.53;
const float RL10 = 50, GAMMA_LDR = 0.7;

// Mode jadwal (opsional, untuk simulasi lima hari). Ubah ke true bila dipakai.
bool MODE_JADWAL = false;
const int JADWAL[][2] = {{30, 150}, {390, 480}};

// ---------- OBJEK ----------
DHT dht(PIN_DHT, DHT22);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
char topikData[96];

// ---------- STATUS ----------
enum Status { KOSONG, OKUPASI, TENGGANG };
Status status = KOSONG;
const char* NAMA[] = {"KOSONG", "OKUPASI", "TENGGANG"};

unsigned long terakhirGerak = 0, waktuKosong = 0, acUbahTerakhir = 0, tAkum = 0, tMulai = 0;
bool pernahGerak = false, acOn = false;
int duty = 0, kosongEvent = 0, falseOff = 0;
float lux = 0, suhu = 28, hum = 60;
float kwhAktual = 0, kwhBase = 0, persenHemat = 0, rupiahHemat = 0, kwhHemat = 0;
unsigned long seq = 0;

// ---------- SENSOR ----------
float bacaLux() {
  float v = analogRead(PIN_LDR) / 4095.0 * 3.3;     // ESP32: 3,3 V, ADC 12-bit
  if (v <= 0.01) return 100000;
  if (v >= 3.29) return 0.1;
  float r = 10000.0 * v / (3.3 - v);
  return pow(RL10 * 1e3 * pow(10, GAMMA_LDR) / r, 1.0 / GAMMA_LDR);
}

bool adaGerak() {
  if (!MODE_JADWAL) return digitalRead(PIN_PIR) == HIGH;
  int m = (millis() - tMulai) / MS_PER_MENIT;
  bool kelas = false;
  for (auto &j : JADWAL) {
    if (m >= j[0] && m < j[1]) kelas = true;
  }
  static unsigned long tCek = 0;
  static bool g = false;
  if (millis() - tCek >= MS_PER_MENIT) {
    tCek = millis();
    g = kelas && random(100) < 60;
  }
  return g;
}

float sejakMenit() {
  return (millis() - terakhirGerak) / (float)MS_PER_MENIT;
}

// ---------- LOGIKA ----------
void updateStatus() {
  unsigned long now = millis();
  if (adaGerak()) {
    if (status == KOSONG && pernahGerak && (now - waktuKosong) < 2UL * MS_PER_MENIT) {
      falseOff++;
    }
    terakhirGerak = now;
    pernahGerak = true;
    status = OKUPASI;
    return;
  }
  if (!pernahGerak) return;

  float s = sejakMenit();
  if (status == OKUPASI && s > T_OKUPASI_MENIT) {
    status = TENGGANG;
  }
  if (status == TENGGANG && s > T_OKUPASI_MENIT + T_TENGGANG_MENIT) {
    status = KOSONG;
    kosongEvent++;
    waktuKosong = now;
  }
}

int hitungDuty(float l) {
  int d = (int)(100.0 * (LUX_TARGET - l) / LUX_TARGET);
  return constrain(d, DUTY_MIN, 100);
}

void aturAC(bool izinkan) {
  unsigned long now = millis();
  bool target = acOn;
  if (!izinkan) target = false;
  else if (suhu > SUHU_ON) target = true;
  else if (suhu <= SUHU_OFF) target = false;

  if (target && !acOn) {                    // menyalakan: hormati jeda minimum
    if (acUbahTerakhir == 0 || now - acUbahTerakhir >= AC_JEDA_MIN * MS_PER_MENIT) {
      acOn = true;
      acUbahTerakhir = now;
    }
  } else if (!target && acOn) {             // mematikan: boleh kapan saja
    acOn = false;
    acUbahTerakhir = now;
  }
  digitalWrite(PIN_AC, acOn ? RELAY_AKTIF : !RELAY_AKTIF);
}

void aksi() {
  if (status == OKUPASI) duty = hitungDuty(lux);
  else if (status == TENGGANG) duty = DUTY_TENGGANG;
  else duty = 0;

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcWrite(PIN_LAMPU, duty * 255 / 100);
#else
  ledcWrite(0, duty * 255 / 100);
#endif

  aturAC(status != KOSONG);

  static unsigned long tBip = 0;
  if (status == TENGGANG && millis() - tBip >= MS_PER_MENIT) {
    tBip = millis();
    tone(PIN_BUZZER, 2000, 150);
  }
}

void akumulasiEnergi() {
  if (MODE_JADWAL && (millis() - tMulai) / MS_PER_MENIT >= 540) return;
  unsigned long now = millis();
  float dtMenit = (now - tAkum) / (float)MS_PER_MENIT;
  // ... biarkan baris di bawahnya tetap sama
  tAkum = now;
  kwhAktual += (P_LAMPU_KW * duty / 100.0 + (acOn ? P_AC_KW : 0)) * dtMenit / 60.0;
  kwhBase   += (P_LAMPU_KW + P_AC_KW) * dtMenit / 60.0;
  kwhHemat   = max(0.0f, kwhBase - kwhAktual);
  persenHemat = kwhBase > 0 ? kwhHemat / kwhBase * 100.0 : 0;
  rupiahHemat = kwhHemat * TARIF_KWH;
}

// ---------- KONEKSI ----------
void sambungWifi() {
  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi terhubung");
}

void sambungMqtt() {
  while (!mqtt.connected()) {
    String id = "esp32-" + String(random(0xffff), HEX);
    bool ok = (strlen(MQTT_USER) > 0) ? mqtt.connect(id.c_str(), MQTT_USER, MQTT_PASS) : mqtt.connect(id.c_str());
    if (!ok) {
      Serial.println("MQTT gagal, coba lagi...");
      delay(2000);
    }
  }
  Serial.println("MQTT terhubung");
}

void kirimData() {
  char buf[420];
  snprintf(buf, sizeof(buf),
    "{\"seq\":%lu,\"status\":\"%s\",\"pir\":%d,\"sejak_gerak\":%.1f,\"lux\":%.0f,\"suhu\":%.1f,\"hum\":%.0f,"
    "\"lampu\":%d,\"ac\":%d,\"kwh_aktual\":%.4f,\"kwh_baseline\":%.4f,\"kwh_hemat\":%.4f,"
    "\"persen\":%.1f,\"rupiah\":%.1f,\"kosong_event\":%d,\"false_off\":%d}",
    ++seq, NAMA[status], adaGerak(), pernahGerak ? sejakMenit() : -1.0, lux, suhu, hum,
    duty, acOn, kwhAktual, kwhBase, kwhHemat, persenHemat, rupiahHemat, kosongEvent, falseOff);
  mqtt.publish(topikData, buf);
}

void tampilOled() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.printf("%s Lampu:%d%%\n", NAMA[status], duty);
  oled.printf("Suhu:%.1fC H:%.0f%%\n", suhu, hum);
  oled.printf("Lux:%.0f AC:%s\n", lux, acOn ? "ON" : "OFF");
  oled.printf("Gerak: %.0f mnt lalu\n", pernahGerak ? sejakMenit() : 0.0);
  oled.printf("Hemat:%.1f%%\n", persenHemat);
  oled.printf("Rp %.0f\n", rupiahHemat);
  oled.display();
}

// ---------- SETUP & LOOP ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_AC, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_AC, !RELAY_AKTIF);

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  ledcAttach(PIN_LAMPU, 5000, 8);
#else
  ledcSetup(0, 5000, 8);
  ledcAttachPin(PIN_LAMPU, 0);
#endif

  dht.begin();
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  snprintf(topikData, sizeof(topikData), "kelas/%s/%s/data", TIM_ID, RUANG_ID);

  sambungWifi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  sambungMqtt();
  tMulai = tAkum = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) sambungWifi();
  if (!mqtt.connected()) sambungMqtt();
  mqtt.loop();

  static unsigned long tDHT = 0, tOled = 0, tPub = 0;
  if (millis() - tDHT >= 2000) {
    tDHT = millis();
    float t = dht.readTemperature(), h = dht.readHumidity();
    if (!isnan(t)) suhu = t;
    if (!isnan(h)) hum = h;
  }

  lux = bacaLux();
  updateStatus();
  aksi();
  akumulasiEnergi();

  if (millis() - tOled >= 500) {
    tOled = millis();
    tampilOled();
  }
  if (millis() - tPub >= 3000) {
    tPub = millis();
    kirimData();
  }
  delay(20);
}