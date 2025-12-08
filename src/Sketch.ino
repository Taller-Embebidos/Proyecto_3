/*
  Contador de Rayos con ESP32 + ThingsBoard (MQTT) + Modo Offline
  ----------------------------------------------------------------
  - GPIO 4 recibe un pulso por cada rayo (botón en protoboard).
  - Interrupción + antirrebote.
  - Fecha y hora reales vía NTP (GMT-6) cuando hay WiFi.
  - Guarda SIEMPRE en NVS (memoria interna) el contador y último epoch.
  - Si hay conexión MQTT -> envía a ThingsBoard.
  - Si NO hay conexión -> sigue contando y guardando localmente.
*/

#include <Preferences.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

#define PIN_RAYO 4
#define DEBOUNCE_MS 120
#define TZ_OFFSET  (-6 * 3600)   // GMT-6
#define TZ_DST      0

// ================================
// WIFI (Simulación Wokwi o lo que tengas)
// ================================
const char* WIFI_SSID = "Wokwi-GUEST";  // Puedes cambiarlo luego
const char* WIFI_PASS = "";             // Vacío en Wokwi

// ================================
// MQTT THINGSBOARD  (Rellena estos)
// ================================
const char* TB_BROKER = "AQUI_SERVIDOR_THINGSBOARD";   // ej: "thingsboard.cloud"
const int   TB_PORT   = AQUI_PUERTO;                   // ej: 1883
const char* TB_TOKEN  = "AQUI_ACCESS_TOKEN";           // Access Token del dispositivo

// ================================

WiFiClient espClient;
PubSubClient mqtt(espClient);
Preferences prefs;

// Flags de estado
bool wifiReady = false;
bool timeReady = false;

// Tiempos de reintento
unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
const unsigned long WIFI_RETRY_MS = 10000;  // 10 s
const unsigned long MQTT_RETRY_MS = 10000;  // 10 s

// Variables compartidas con la ISR
volatile unsigned long lastStrikeMsISR = 0;
volatile uint32_t strikeCountISR = 0;
volatile bool strikePending = false;

// Variables normales
uint32_t strikeCount = 0;
unsigned long lastStrikeEpoch = 0;

// ================================
// INTERRUPCIÓN EN GPIO4
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
// WIFI – CONEXIÓN NO BLOQUEANTE
// ================================
void connectWiFiOnce() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiReady = true;
    return;
  }

  Serial.println("Intentando conectar a WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (millis() - start < 5000) {  // Máx 5 s
    if (WiFi.status() == WL_CONNECTED) {
      wifiReady = true;
      Serial.println("WiFi conectado.");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      return;
    }
    delay(300);
    Serial.print(".");
  }

  Serial.println("\nNo se pudo conectar WiFi (timeout). Modo offline.");
  wifiReady = false;
}

// ================================
// NTP – INICIALIZACIÓN (sólo si hay WiFi)
// ================================
void initNTPOnce() {
  if (!wifiReady) {
    timeReady = false;
    return;
  }

  Serial.println("Sincronizando NTP...");
  configTime(TZ_OFFSET, TZ_DST, "pool.ntp.org", "time.nist.gov");

  struct tm info;
  unsigned long start = millis();
  while (millis() - start < 5000) { // Máx 5 s
    if (getLocalTime(&info)) {
      Serial.println("NTP OK.");
      timeReady = true;
      return;
    }
    Serial.print(".");
    delay(300);
  }

  Serial.println("\nNo se pudo sincronizar NTP. Continuando sin hora real.");
  timeReady = false;
}

// ================================
// MQTT – CONEXIÓN (no bloquear si falla)
// ================================
void connectMQTTOnce() {
  if (!wifiReady) {
    return;
  }

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
// PUBLICAR UN RAYO EN THINGSBOARD
// ================================
void publishStrike(uint32_t id, unsigned long epoch) {
  if (!mqtt.connected()) {
    Serial.println("MQTT no conectado. No se publica, solo guardado local.");
    return;
  }

  struct tm ts;
  if (timeReady && epoch > 1000) {
    localtime_r((time_t*)&epoch, &ts);
  } else {
    // Si no hay tiempo real, llenar con 0
    memset(&ts, 0, sizeof(ts));
  }

  char fecha[16];
  char hora[16];

  if (timeReady && epoch > 1000) {
    strftime(fecha, sizeof(fecha), "%Y-%m-%d", &ts);
    strftime(hora, sizeof(hora), "%H:%M:%S", &ts);
  } else {
    strncpy(fecha, "1970-01-01", sizeof(fecha));
    strncpy(hora, "00:00:00", sizeof(hora));
  }

  char payload[250];
  snprintf(payload, sizeof(payload),
    "{\"rayo_id\": %u, \"epoch\": %lu, \"fecha\": \"%s\", \"hora\": \"%s\", \"gpio\": 4}",
    id, epoch, fecha, hora
  );

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

  Serial.println("\n=== ESP32 DETECTOR DE RAYOS (Offline + MQTT) ===");

  pinMode(PIN_RAYO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_RAYO), onStrikeISR, FALLING);

  prefs.begin("rayos", false);

  strikeCount = prefs.getUInt("count", 0);
  lastStrikeEpoch = prefs.getULong("last", 0);
  strikeCountISR = strikeCount;

  Serial.print("Rayos previos almacenados: ");
  Serial.println(strikeCount);

  // Primer intento de WiFi / NTP / MQTT (no bloquea infinito)
  connectWiFiOnce();
  if (wifiReady) {
    initNTPOnce();
    connectMQTTOnce();
  }
}

// ================================
// LOOP PRINCIPAL
// ================================
void loop() {
  // Mantener MQTT si está conectado
  if (mqtt.connected()) {
    mqtt.loop();
  }

  unsigned long now = millis();

  // Reintentar WiFi cada cierto tiempo si no está conectado
  if (!wifiReady && (now - lastWifiAttempt >= WIFI_RETRY_MS)) {
    lastWifiAttempt = now;
    connectWiFiOnce();
    if (wifiReady && !timeReady) {
      initNTPOnce();
    }
  }

  // Reintentar MQTT si hay WiFi pero no MQTT
  if (wifiReady && !mqtt.connected() && (now - lastMqttAttempt >= MQTT_RETRY_MS)) {
    lastMqttAttempt = now;
    connectMQTTOnce();
  }

  // Procesar rayos detectados
  if (strikePending) {
    noInterrupts();
    uint32_t c = strikeCountISR;
    strikePending = false;
    interrupts();

    strikeCount = c;

    if (timeReady) {
      lastStrikeEpoch = time(NULL);
    } else {
      // Si no hay NTP aún, guardamos 0 o podrías usar millis() si quieres
      lastStrikeEpoch = 0;
    }

    // Guardado SIEMPRE en memoria interna
    prefs.putUInt("count", strikeCount);
    prefs.putULong("last", lastStrikeEpoch);

    // Intentar publicar (solo si hay MQTT)
    publishStrike(strikeCount, lastStrikeEpoch);

    Serial.println("------------------------------");
    Serial.print("Rayo detectado #");
    Serial.println(strikeCount);
    Serial.print("Epoch almacenado: ");
    Serial.println(lastStrikeEpoch);
    Serial.print("WiFi: ");
    Serial.println(wifiReady ? "OK" : "NO");
    Serial.print("MQTT: ");
    Serial.println(mqtt.connected() ? "OK" : "NO");
    Serial.println("------------------------------");
  }

  delay(20);
}
