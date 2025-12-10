// main.c - ESP-IDF + FreeRTOS 

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "mqtt_client.h"

#include "driver/gpio.h"
#include "esp_sntp.h"
#include "esp_timer.h"

// ================================
// Definiciones generales
// ================================
#define PIN_RAYO    GPIO_NUM_4
#define DEBOUNCE_MS 120

// WiFi
static const char *WIFI_SSID = "red_wifi";
static const char *WIFI_PASS = "pasw_wifi";

// MQTT ThingsBoard
static const char *TB_BROKER_URI = "mqtt://mqtt.thingsboard.cloud";
static const int   TB_PORT       = 1883;
static const char *TB_TOKEN      = "z77bybvtklxwszwmnl1d";  // username

// ID del dispositivo
static const uint32_t DEVICE_ID = 1;

// TAG para logs
static const char *TAG = "RAYOS";

// Bits de EventGroup para WiFi
#define WIFI_CONNECTED_BIT BIT0
static EventGroupHandle_t s_wifi_event_group;

// Manejo de NVS
static nvs_handle_t nvs_handle;

// Estado global
static bool wifiReady = false;
static bool timeReady = false;
static bool mqttConnected = false;

// MQTT client
static esp_mqtt_client_handle_t mqtt_client = NULL;

// Variables ISR
static volatile uint32_t strikeCountISR = 0;
static volatile bool strikePending = false;
static int64_t lastStrikeUsISR = 0;

// Mutex para sección crítica ISR <-> tarea
static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;

// Contadores globales
static uint32_t strikeCount = 0;      // contador sesión
static uint32_t totalRays   = 0;      // acumulado persistente
static uint32_t lastStrikeEpoch = 0;

// ================================
// ISR GPIO4 (rayo)
// ================================
static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    int64_t nowUs = esp_timer_get_time();
    int64_t diffMs = (nowUs - lastStrikeUsISR) / 1000;

    if (diffMs >= DEBOUNCE_MS) {
        lastStrikeUsISR = nowUs;
        portENTER_CRITICAL_ISR(&spinlock);
        strikeCountISR++;
        strikePending = true;
        portEXIT_CRITICAL_ISR(&spinlock);
    }
}

// ================================
// WiFi event handler
// ================================
static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi desconectado, reintentando...");
        wifiReady = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi conectado, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifiReady = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ================================
// WiFi init (STA)
// ================================
static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        NULL
    ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        NULL
    ));

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg = (wifi_pmf_config_t){
        .capable = true,
        .required = false
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi inicializado, intentando conectar a %s", WIFI_SSID);
}

// ================================
// SNTP / NTP
// ================================
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Tiempo sincronizado vía NTP");
    timeReady = true;
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Inicializando SNTP...");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.nist.gov");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

static void obtain_time(void)
{
    // Configurar zona horaria GMT-6 (Costa Rica, sin DST)
    setenv("TZ", "CST6", 1); // CST-6 sin horario de verano
    tzset();

    initialize_sntp();

    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry <= retry_count) {
        ESP_LOGI(TAG, "Esperando sincronización NTP... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        timeReady = true;
        ESP_LOGI(TAG, "NTP OK.");
    } else {
        timeReady = false;
        ESP_LOGW(TAG, "Fallo NTP.");
    }
}

// ================================
// MQTT event handler
// ================================
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT conectado a ThingsBoard");
        mqttConnected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT desconectado");
        mqttConnected = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    mqtt_event_handler_cb(event);
}

// ================================
// MQTT init
// ================================
static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri  = TB_BROKER_URI,
        .broker.address.port = TB_PORT,
        .credentials.username = TB_TOKEN,
        // Sin password para ThingsBoard con token
        .session.keepalive = 60,
        .network.disable_auto_reconnect = false,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        mqtt_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    ));
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt_client));
}

// ================================
// PUBLICAR EVENTO EN THINGSBOARD
// ================================
static void publishStrike(uint32_t strikeId,
                          uint32_t deviceId,
                          uint32_t acumuladoRayos,
                          uint32_t epoch)
{
    if (!mqttConnected || mqtt_client == NULL) {
        ESP_LOGW(TAG, "MQTT no conectado. No se publica.");
        return;
    }

    // Fecha y hora formateadas
    struct tm ts;
    char fecha[16];
    char hora[16];

    if (timeReady && epoch > 1000) {
        time_t t = (time_t)epoch;
        localtime_r(&t, &ts);
        strftime(fecha, sizeof(fecha), "%Y-%m-%d", &ts);
        strftime(hora, sizeof(hora), "%H:%M:%S", &ts);
    } else {
        strcpy(fecha, "1970-01-01");
        strcpy(hora,  "00:00:00");
    }

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
             strikeId, deviceId, acumuladoRayos,
             (unsigned long)epoch, fecha, hora);

    int msg_id = esp_mqtt_client_publish(
        mqtt_client,
        "v1/devices/me/telemetry",
        payload,
        0,
        1,
        0
    );

    if (msg_id >= 0) {
        ESP_LOGI(TAG, "Publicado en ThingsBoard (msg_id=%d): %s", msg_id, payload);
    } else {
        ESP_LOGW(TAG, "Fallo al publicar en MQTT");
    }
}

// ================================
// NVS: carga y guarda
// ================================
static void nvs_load_state(void)
{
    esp_err_t err;

    err = nvs_open("rayos", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error abriendo NVS: %s", esp_err_to_name(err));
        return;
    }

    uint32_t tmp;

    err = nvs_get_u32(nvs_handle, "count", &tmp);
    if (err == ESP_OK) {
        strikeCount = tmp;
    } else {
        strikeCount = 0;
    }

    err = nvs_get_u32(nvs_handle, "total", &tmp);
    if (err == ESP_OK) {
        totalRays = tmp;
    } else {
        totalRays = 0;
    }

    err = nvs_get_u32(nvs_handle, "last", &tmp);
    if (err == ESP_OK) {
        lastStrikeEpoch = tmp;
    } else {
        lastStrikeEpoch = 0;
    }

    strikeCountISR = strikeCount;

    ESP_LOGI(TAG, "Rayos previos (sesión): %" PRIu32, strikeCount);
    ESP_LOGI(TAG, "Acumulado de rayos (acumulado_rayos): %" PRIu32, totalRays);
}

static void nvs_save_state(void)
{
    if (nvs_handle == 0) return;

    esp_err_t err;
    err = nvs_set_u32(nvs_handle, "count", strikeCount);
    if (err != ESP_OK) ESP_LOGW(TAG, "Error nvs_set_u32(count): %s", esp_err_to_name(err));

    err = nvs_set_u32(nvs_handle, "total", totalRays);
    if (err != ESP_OK) ESP_LOGW(TAG, "Error nvs_set_u32(total): %s", esp_err_to_name(err));

    err = nvs_set_u32(nvs_handle, "last", lastStrikeEpoch);
    if (err != ESP_OK) ESP_LOGW(TAG, "Error nvs_set_u32(last): %s", esp_err_to_name(err));

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) ESP_LOGW(TAG, "Error nvs_commit: %s", esp_err_to_name(err));
}

// ================================
// Inicializar GPIO + ISR
// ================================
static void gpio_init_strike(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << PIN_RAYO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE  // Falling
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_RAYO, gpio_isr_handler, NULL));

    ESP_LOGI(TAG, "GPIO %" PRIu32 " configurado para detector de rayos", (uint32_t)PIN_RAYO);
}

// ================================
// Tarea principal (equivalente a loop())
// ================================
static void main_loop_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Tarea principal iniciada");

    while (1) {
        bool localStrikePending = false;
        uint32_t localCountISR = 0;

        // Copiar estado de ISR de forma atómica
        portENTER_CRITICAL(&spinlock);
        if (strikePending) {
            localStrikePending = true;
            localCountISR = strikeCountISR;
            strikePending = false;
        }
        portEXIT_CRITICAL(&spinlock);

        if (localStrikePending) {
            strikeCount = localCountISR;
            totalRays++;
            uint32_t strikeId = totalRays;

            if (timeReady) {
                time_t now = time(NULL);
                lastStrikeEpoch = (uint32_t)now;
            } else {
                lastStrikeEpoch = 0;
            }

            // Guardar en NVS
            nvs_save_state();

            // Publicar
            publishStrike(
                strikeId,
                DEVICE_ID,
                totalRays,
                lastStrikeEpoch
            );

            ESP_LOGI(TAG, "------------------------------");
            ESP_LOGI(TAG, "Rayo detectado (contador sesión): %" PRIu32, strikeCount);
            ESP_LOGI(TAG, "ID único rayo (id_rayo): %" PRIu32, strikeId);
            ESP_LOGI(TAG, "Acumulado de rayos (acumulado_rayos): %" PRIu32, totalRays);
            ESP_LOGI(TAG, "Epoch: %" PRIu32, lastStrikeEpoch);
            ESP_LOGI(TAG, "WiFi: %s", wifiReady ? "OK" : "NO");
            ESP_LOGI(TAG, "MQTT: %s", mqttConnected ? "OK" : "NO");
            ESP_LOGI(TAG, "ID dispositivo (disp_id): %" PRIu32, DEVICE_ID);
            ESP_LOGI(TAG, "------------------------------");
        }

        vTaskDelay(pdMS_TO_TICKS(20));  // ~equivalente a delay(20)
    }
}

// ================================
// app_main
// ================================
void app_main(void)
{
    esp_err_t ret;

    // Inicializar NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== ESP32 DETECTOR DE RAYOS (ESP-IDF) ===");

    // Cargar estado persistente
    nvs_load_state();

    // Inicializar WiFi
    wifi_init_sta();

    // Esperar algo a que se conecte WiFi (pero no bloquear infinito)
    ESP_LOGI(TAG, "Esperando conexión WiFi hasta 10 segundos...");
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(10000)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi listo, iniciamos NTP y MQTT");
        obtain_time();
        mqtt_app_start();
    } else {
        ESP_LOGW(TAG, "No se logró conectar WiFi en el tiempo esperado. "
                      "Se seguirá contando rayos offline.");
    }

    // Inicializar GPIO e ISR
    gpio_init_strike();

    // Crear tarea principal (loop)
    xTaskCreate(
        main_loop_task,
        "main_loop_task",
        4096,
        NULL,
        5,
        NULL
    );
}
