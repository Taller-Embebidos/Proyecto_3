
#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

#define PIN_RAYO    4
#define DEBOUNCE_MS 120
#define TZ_OFFSET   (-6 * 3600)   // GMT-6
#define TZ_DST      0

// ================================
// WiFi (Wokwi)
// ================================
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

// ================================
// MQTT ThingsBoard Cloud
// ================================
const char* TB_BROKER = "mqtt.thingsboard.cloud";
const int   TB_PORT   = 1883;
const char* TB_TOKEN  = "z77bybvtklxwszwmnl1d";

// ================================
// ID del dispositivo (fijo para demo)
// ================================
const uint32_t DEVICE_ID = 1;

WiFiClient espClient;
PubSubClient mqtt(espClient);
Preferences prefs;

bool wifiReady = false;
bool timeReady = false;

unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
const unsigned long WIFI_RETRY_MS = 10000;
const unsigned long MQTT_RETRY_MS = 10000;

// ISR vars
volatile unsigned long lastStrikeMsISR = 0;
volatile uint32_t strikeCountISR = 0;
volatile bool strikePending = false;

// Global counters
uint32_t strikeCount = 0;       // contador sesión (opcional para debug)
uint32_t totalRays   = 0;       // acumulado de rayos (persistente)
unsigned long lastStrikeEpoch = 0;

// ================================
// ISR GPIO4
// ================================
void IRAM_ATTR onStrikeISR() {
  unsigned long now = millis();
  if (now - lastStrikeMsISR >= DEBOUNCE_MS) {
    lastStrikeMsISR = now;
    strikeCountISR++;
    strikePending = true;
  }
}

// ================================
// WiFi
// ================================
void connectWiFiOnce() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    return;
  }

  Serial.println("Intentando conectar a WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiReady = true;
      Serial.println("WiFi conectado.");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      return;
    }
    Serial.print(".");
    delay(300);
  }

  Serial.println("\nNo se pudo conectar WiFi.");
  wifiReady = false;
}

// ================================
// NTP
// ================================
void initNTPOnce() {
  if (!wifiReady) return;

  Serial.println("Sincronizando NTP...");
  configTime(TZ_OFFSET, TZ_DST, "pool.ntp.org", "time.nist.gov");

  struct tm info;
  unsigned long start = millis();

  while (millis() - start < 5000) {
    if (getLocalTime(&info)) {
      Serial.println("NTP OK.");
      timeReady = true;
      return;
    }
    Serial.print(".");
    delay(300);
  }

  Serial.println("\nFallo NTP.");
  timeReady = false;
}

// ================================
// MQTT
// ================================
void connectMQTTOnce() {
  if (!wifiReady) return;

  mqtt.setServer(TB_BROKER, TB_PORT);

  Serial.println("Intentando conectar a ThingsBoard MQTT...");
  if (mqtt.connect("esp32-rayos", TB_TOKEN, NULL)) {
    Serial.println("MQTT conectado.");
  } else {
    Serial.print("Fallo MQTT, estado: ");
    Serial.println(mqtt.state());
  }
}

// ================================
// PUBLICAR EVENTO EN THINGSBOARD
// ================================
//  strikeId        -> id_rayo (ID único del rayo)
//  deviceId        -> disp_id
//  acumuladoRayos  -> acumulado_rayos
//  epoch           -> unix epoch del evento
void publishStrike(uint32_t strikeId,
                   uint32_t deviceId,
                   uint32_t acumuladoRayos,
                   unsigned long epoch) {

  if (!mqtt.connected()) {
    Serial.println("MQTT no conectado. No se publica.");
    return;
  }

  // Fecha y hora formateadas
  struct tm ts;
  char fecha[16];
  char hora[16];

  if (timeReady && epoch > 1000) {
    localtime_r((time_t*)&epoch, &ts);
    strftime(fecha, sizeof(fecha), "%Y-%m-%d", &ts);
    strftime(hora, sizeof(hora), "%H:%M:%S", &ts);
  } else {
    strcpy(fecha, "1970-01-01");
    strcpy(hora,  "00:00:00");
  }

  // Construir JSON SOLO con los campos solicitados
  char payload[256];
  snprintf(payload, sizeof(payload),
           "{"
             "\"id_rayo\": %u, "
             "\"disp_id\": %u, "
             "\"acumulado_rayos\": %u, "
             "\"epoch\": %lu, "
             "\"fecha\": \"%s\", "
             "\"hora\": \"%s\""
           "}",
           strikeId, deviceId, acumuladoRayos, epoch, fecha, hora);

  mqtt.publish("v1/devices/me/telemetry", payload);

  Serial.println("Publicado en ThingsBoard:");
  Serial.println(payload);
}

// ================================
// SETUP
// ================================
void setup() {
  Serial.begin(115200);
  delay(800);

  Serial.println("\n=== ESP32 DETECTOR DE RAYOS ===");

  pinMode(PIN_RAYO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_RAYO), onStrikeISR, FALLING);

  prefs.begin("rayos", false);

  // Recuperar valores persistentes
  strikeCount     = prefs.getUInt("count", 0);
  totalRays       = prefs.getUInt("total", 0);   // acumulado persistente
  lastStrikeEpoch = prefs.getULong("last", 0);
  strikeCountISR  = strikeCount;

  Serial.print("Rayos previos (sesión): ");
  Serial.println(strikeCount);
  Serial.print("Acumulado de rayos (acumulado_rayos): ");
  Serial.println(totalRays);

  connectWiFiOnce();
  if (wifiReady) {
    initNTPOnce();
    connectMQTTOnce();
  }
}

// ================================
// LOOP
// ================================
void loop() {
  if (mqtt.connected()) mqtt.loop();

  unsigned long now = millis();

  // Retry WiFi
  if (!wifiReady && (now - lastWifiAttempt >= WIFI_RETRY_MS)) {
    lastWifiAttempt = now;
    connectWiFiOnce();
    if (wifiReady && !timeReady)
      initNTPOnce();
  }

  // Retry MQTT
  if (wifiReady && !mqtt.connected() && (now - lastMqttAttempt >= MQTT_RETRY_MS)) {
    lastMqttAttempt = now;
    connectMQTTOnce();
  }

  // Evento detectado
  if (strikePending) {
    noInterrupts();
    uint32_t c = strikeCountISR;
    strikePending = false;
    interrupts();

    strikeCount = c;      // sigue existiendo para depuración local
    totalRays++;          // acumulado persistente
    uint32_t strikeId = totalRays;  // ID único del rayo

    if (timeReady)
      lastStrikeEpoch = time(NULL);
    else
      lastStrikeEpoch = 0;

    // Guardar en NVS
    prefs.putUInt("count", strikeCount);
    prefs.putUInt("total", totalRays);
    prefs.putULong("last", lastStrikeEpoch);

    // Publicar solo con los campos solicitados
    publishStrike(
      strikeId,
      DEVICE_ID,
      totalRays,        // acumulado_rayos
      lastStrikeEpoch
    );

    Serial.println("------------------------------");
    Serial.printf("Rayo detectado (contador sesión): %u\n", strikeCount);
    Serial.printf("ID único rayo (id_rayo): %u\n", strikeId);
    Serial.printf("Acumulado de rayos (acumulado_rayos): %u\n", totalRays);
    Serial.printf("Epoch: %lu\n", lastStrikeEpoch);
    Serial.printf("WiFi: %s\n", wifiReady ? "OK" : "NO");
    Serial.printf("MQTT: %s\n", mqtt.connected() ? "OK" : "NO");
    Serial.printf("ID dispositivo (disp_id): %u\n", DEVICE_ID);
    Serial.println("------------------------------");
  }

  delay(20);
}
