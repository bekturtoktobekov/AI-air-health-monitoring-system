#define BLYNK_TEMPLATE_ID   "TMPL65zxnV_fF"
#define BLYNK_TEMPLATE_NAME "Smart Indoor Air Quality Sensor"
#define BLYNK_AUTH_TOKEN    "XFqEpHt3z3E88NE5Ca9s_Kx_qB93xnML"
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <BlynkSimpleEsp32.h>

// ── Pins ─────────────────────────────────────────
#define DHT_PIN    14
#define GAS_A0      4
#define DUST_PM25  16
#define DUST_PM10  17

const char* WIFI_SSID = "MAYBACH";
const char* WIFI_PASS = "12240227";
const char* SERVER    = "http://192.168.0.117:5000/api/data";

DHT dht(DHT_PIN, DHT22);

// ── MQ-135 ppm conversion ─────────────────────────
#define MQ135_R0  1.97
#define MQ135_RL  10.0

float getMQ135ppm() {
  int raw = analogRead(GAS_A0);
  Serial.printf("ADC raw: %d\n", raw);
  
  float voltage = raw * (3.3 / 4095.0);
  Serial.printf("Voltage: %.3f V\n", voltage);
  
  if (voltage <= 0.01) return 0;
  float rs = MQ135_RL * (3.3 - voltage) / voltage;
  Serial.printf("Rs: %.2f kohm\n", rs);
  
  float ratio = rs / MQ135_R0;
  Serial.printf("Ratio Rs/R0: %.4f\n", ratio);
  
  float ppm = 116.6020682 * pow(ratio, -2.769034857);
  return ppm < 0 ? 0 : ppm;
}

// ── Dust ratio ────────────────────────────────────
float getDustRatio(int pin) {
  unsigned long start = millis();
  unsigned long lowTime = 0;
  while (millis() - start < 30000) {
    if (digitalRead(pin) == LOW) {
      unsigned long pulseStart = micros();
      while (digitalRead(pin) == LOW && (micros() - pulseStart) < 100000);
      lowTime += (micros() - pulseStart) / 1000UL;
    }
    yield();
    Blynk.run();
  }
  return (lowTime / 30000.0) * 100.0;
}

float dustToMg(float ratio) {
  float mg = 0.001915 * ratio * ratio + 0.09522 * ratio - 0.04884;
  return mg < 0 ? 0.0 : mg;
}

// ── Send to Flask ─────────────────────────────────
void sendToFlask(float temp, float hum, float pm25, float pm10, float gas_ppm) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(SERVER);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["temperature"] = temp;
  doc["humidity"]    = hum;
  doc["pm25"]        = pm25;
  doc["pm10"]        = pm10;
  doc["gas_ppm"]     = gas_ppm;

  String body;
  serializeJson(doc, body);
  int code = http.POST(body);
  Serial.printf("Flask → %d\n", code);
  http.end();
}

// ── Send to Blynk ─────────────────────────────────
void sendToBlynk(float temp, float hum, float pm25, float pm10, float gas_ppm) {
  Blynk.virtualWrite(V0, temp);
  Blynk.virtualWrite(V1, hum);
  Blynk.virtualWrite(V2, pm25);
  Blynk.virtualWrite(V3, pm10);
  Blynk.virtualWrite(V4, gas_ppm);
  Serial.println("Blynk → sent");
}

// ── Setup ─────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  dht.begin();
  delay(2000);
  pinMode(DUST_PM25, INPUT);
  pinMode(DUST_PM10, INPUT);

  Blynk.begin(BLYNK_AUTH_TOKEN, WIFI_SSID, WIFI_PASS);
  Serial.println("WiFi + Blynk connected");
  Serial.println("IP: " + WiFi.localIP().toString());

  Serial.println("Warming up sensors (60s)...");
  for (int i = 60; i > 0; i--) {
    Serial.printf("  %ds...\n", i);
    Blynk.run();
    delay(1000);
  }
  Serial.println("Ready!");
}

// ── Loop ──────────────────────────────────────────
void loop() {
  Blynk.run();
  Serial.println("\n=== New cycle ===");

  // DHT22
  float temp = NAN, hum = NAN;
  for (int i = 0; i < 3; i++) {
    delay(2200);
    temp = dht.readTemperature();
    hum  = dht.readHumidity();
    if (!isnan(temp) && !isnan(hum)) break;
    Serial.printf("DHT22 retry %d\n", i + 1);
  }
  if (isnan(temp)) temp = 0;
  if (isnan(hum))  hum  = 0;
  Serial.printf("Temp: %.1f°C  Hum: %.1f%%\n", temp, hum);

  // MQ-135
  float gas_ppm = getMQ135ppm();
  Serial.printf("Gas: %.1f ppm\n", gas_ppm);

  // Dust
  Serial.println("Sampling PM2.5 (30s)...");
  float pm25 = dustToMg(getDustRatio(DUST_PM25));
  Serial.println("Sampling PM1.0 (30s)...");
  float pm10 = dustToMg(getDustRatio(DUST_PM10));
  Serial.printf("PM2.5: %.4f mg/m3  PM1.0: %.4f mg/m3\n", pm25, pm10);

  sendToFlask(temp, hum, pm25, pm10, gas_ppm);
  sendToBlynk(temp, hum, pm25, pm10, gas_ppm);
}