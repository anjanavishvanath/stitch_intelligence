/*
 * firmware_v0.4 — activation + MQTT publish + piece segmentation
 * --------------------------------------------------------------
 * Boot sequence:
 *   1. NVS + WiFi station; block until we have an IP.
 *   2. Load NVS: is this device activated?
 *      - No  → POST hardcoded SLPT + MAC to /api/devices/activate,
 *              save mqtt_pass, restart. Next boot takes the "yes" path.
 *      - Yes → init MQTT with (username=MAC, password=nvs.mqtt_pass).
 *   3. Set up GPIO ISRs, run the segmentation state machine, and publish
 *      each completed piece as JSON on `stitch/device/<MAC>/pieces`.
 *
 * To force re-activation (wipe our NVS namespace on boot), set
 * DEBUG_FORCE_REACTIVATION to 1, flash once, flip back to 0.
 *
 * Dependencies (main/CMakeLists.txt REQUIRES):
 *   esp_driver_gpio esp_timer cjson esp_wifi nvs_flash esp_netif esp_event
 *   esp_http_client esp-tls mqtt
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "mqtt_client.h"
#include "cJSON.h"


/* ============================================================
 * DEBUG
 * ============================================================ */
#define DEBUG_FORCE_REACTIVATION  0   /* 1 → wipe our NVS namespace on boot */


/* ============================================================
 * 1. Pin assignments — UPDATE TO MATCH YOUR PCB
 * ============================================================ */
#define PIN_ENC_A   GPIO_NUM_4
#define PIN_ENC_B   GPIO_NUM_5
#define PIN_FOOT    GPIO_NUM_6
#define PIN_TRIM    GPIO_NUM_7
#define PIN_WIPER   GPIO_NUM_8


/* ============================================================
 * 2. Segmentation tunables
 * ============================================================ */
#define DEBOUNCE_US               20000   /* 20 ms — buttons only */
#define STITCH_IDLE_MS_TO_ADJUST  300     /* no stitch pulse this long → adjust */
#define PIECE_ABANDON_MS          60000   /* adjust this long → emit ABANDONED */
#define MAX_SEGMENTS              32
#define EVENT_QUEUE_LEN           128


/* ============================================================
 * 3. WiFi credentials
 * ============================================================ */
#define WIFI_SSID       "SLT_FIBRE"
#define WIFI_PASS       "Anji@123"
#define WIFI_MAX_RETRY  5


/* ============================================================
 * 4. Cloud endpoints + provisioning
 * ============================================================ */
#define ACTIVATION_URL   "https://stitch-backend.fly.dev/api/devices/activate"
#define MQTT_BROKER_URI  "mqtt://213.188.218.253:1883"

/* Paste the 12-char SLPT from the dashboard's "Provision New Device" flow.
 * Single-use, expires in 10 minutes. Get a fresh one if activation returns 403. */
#define SLPT             "fee37f70bc48"


/* ============================================================
 * 5. NVS layout (our namespace, separate from the WiFi/system one)
 * ============================================================ */
#define NVS_NS              "stitch_dev"
#define NVS_K_ACTIVATED     "activated"    /* u8: 0/1 */
#define NVS_K_MQTT_PASS     "mqtt_pass"    /* string */


/* ============================================================
 * 6. Globals set at boot
 * ============================================================ */
static const char *TAG      = "PIECE";
static const char *TAG_WIFI = "WIFI";
static const char *TAG_ACT  = "ACTIVATE";
static const char *TAG_MQTT = "MQTT";

/* Device identity (formatted once in app_main) */
static char mac_str[18];         /* "AA:BB:CC:DD:EE:FF" */
static char mqtt_topic[64];      /* "stitch/device/<MAC>/pieces" */
static char mqtt_client_id[32];  /* "stitch-<MAC>" */

/* Activation credentials (loaded from NVS or set by try_activate) */
static bool activated_g   = false;
static char mqtt_pass_g[64] = {0};

/* WiFi + MQTT bookkeeping */
static EventGroupHandle_t   wifi_event_group;
static EventGroupHandle_t   mqtt_event_group;
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define MQTT_CONNECTED_BIT  BIT0

static int                       s_wifi_retry_num = 0;
static esp_mqtt_client_handle_t  mqtt_client      = NULL;


/* ============================================================
 * 7. WiFi
 * ============================================================ */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG_WIFI, "connecting to AP...");

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_wifi_retry_num++;
            ESP_LOGW(TAG_WIFI, "connect failed, retrying (%d/%d)", s_wifi_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG_WIFI, "max retries reached");
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) data;
        ESP_LOGI(TAG_WIFI, "got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_wifi_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any, inst_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* Block until connected or hard-failed */
static bool wifi_wait_for_ip(TickType_t timeout_ticks)
{
    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, timeout_ticks);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}


/* ============================================================
 * 8. NVS: our activation state
 * ============================================================ */
static void nvs_factory_reset_stitch(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGW(TAG, "NVS namespace '%s' wiped", NVS_NS);
    }
}

static void nvs_load_activation(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        activated_g = false;
        mqtt_pass_g[0] = '\0';
        return;
    }
    uint8_t flag = 0;
    nvs_get_u8(h, NVS_K_ACTIVATED, &flag);
    activated_g = (flag == 1);

    size_t len = sizeof(mqtt_pass_g);
    if (nvs_get_str(h, NVS_K_MQTT_PASS, mqtt_pass_g, &len) != ESP_OK) {
        mqtt_pass_g[0] = '\0';
    }
    nvs_close(h);

    /* Defensive: flag set but no password → treat as not activated */
    if (activated_g && mqtt_pass_g[0] == '\0') {
        ESP_LOGW(TAG, "NVS activated=true but mqtt_pass empty; forcing re-activation");
        activated_g = false;
    }
    ESP_LOGI(TAG, "NVS activated=%s", activated_g ? "true" : "false");
}

static void nvs_save_activation(const char *mqtt_pass)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_u8 (h, NVS_K_ACTIVATED, 1));
    ESP_ERROR_CHECK(nvs_set_str(h, NVS_K_MQTT_PASS, mqtt_pass));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    ESP_LOGI(TAG, "NVS: activated=1, mqtt_pass stored");
}


/* ============================================================
 * 9. HTTP activation
 * ============================================================ */
typedef struct {
    char   buf[1024];
    size_t len;
} http_resp_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data != NULL) {
        http_resp_t *r = (http_resp_t *) evt->user_data;
        int copy = evt->data_len;
        if (r->len + copy >= sizeof(r->buf)) {
            copy = sizeof(r->buf) - r->len - 1;
        }
        if (copy > 0) {
            memcpy(r->buf + r->len, evt->data, copy);
            r->len += copy;
            r->buf[r->len] = '\0';
        }
    }
    return ESP_OK;
}

static bool try_activate(void)
{
    if (strcmp(SLPT, "PASTE_SLPT_HERE") == 0 || SLPT[0] == '\0') {
        ESP_LOGE(TAG_ACT, "SLPT not set in firmware. Paste a fresh token and re-flash.");
        return false;
    }

    /* Build request body */
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "slpt", SLPT);
    cJSON_AddStringToObject(req, "mac",  mac_str);
    char *body = cJSON_PrintUnformatted(req);
    cJSON_Delete(req);
    if (!body) {
        ESP_LOGE(TAG_ACT, "req JSON alloc failed");
        return false;
    }

    http_resp_t resp = {0};

    esp_http_client_config_t config = {
        .url               = ACTIVATION_URL,
        .method            = HTTP_METHOD_POST,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
        .event_handler     = http_event_handler,
        .user_data         = &resp,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    ESP_LOGI(TAG_ACT, "POST %s", ACTIVATION_URL);
    ESP_LOGI(TAG_ACT, "body: %s", body);

    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);

    ESP_LOGI(TAG_ACT, "HTTP status=%d, body=%s", status, resp.buf);

    esp_http_client_cleanup(client);
    free(body);

    if (err != ESP_OK) {
        ESP_LOGE(TAG_ACT, "HTTP perform failed: %s", esp_err_to_name(err));
        return false;
    }
    if (status != 200) {
        if (status == 403 || status == 404 || status == 400) {
            ESP_LOGE(TAG_ACT, "non-recoverable (token used/expired/MAC mismatch). Fresh SLPT + re-flash.");
        }
        return false;
    }

    /* Parse response, extract mqtt_pass */
    cJSON *doc = cJSON_Parse(resp.buf);
    if (!doc) {
        ESP_LOGE(TAG_ACT, "response JSON parse failed");
        return false;
    }
    cJSON *pass_node = cJSON_GetObjectItem(doc, "mqtt_pass");
    if (!cJSON_IsString(pass_node) || pass_node->valuestring[0] == '\0') {
        ESP_LOGE(TAG_ACT, "response missing mqtt_pass");
        cJSON_Delete(doc);
        return false;
    }
    nvs_save_activation(pass_node->valuestring);
    cJSON_Delete(doc);
    return true;
}


/* ============================================================
 * 10. MQTT
 * ============================================================ */
static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;
    switch ((esp_mqtt_event_id_t) event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_MQTT, "connected as %s", mac_str);
        xEventGroupSetBits(mqtt_event_group, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG_MQTT, "disconnected");
        xEventGroupClearBits(mqtt_event_group, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG_MQTT, "publish ack (msg_id=%d)", event->msg_id);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG_MQTT, "error: type=%d", event->error_handle->error_type);
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    mqtt_event_group = xEventGroupCreate();

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri            = MQTT_BROKER_URI,
        .credentials.username          = mac_str,
        .credentials.authentication.password = mqtt_pass_g,
        .credentials.client_id         = mqtt_client_id,
        .session.keepalive             = 30,
        .buffer.size                   = 4096,   /* piece JSON can be ~1–2 KB with many segments */
        .network.disable_auto_reconnect = false,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG_MQTT, "init failed");
        return;
    }
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    /* Wait briefly for first CONNACK; proceed regardless (esp-mqtt auto-reconnects). */
    EventBits_t bits = xEventGroupWaitBits(
        mqtt_event_group, MQTT_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    if (bits & MQTT_CONNECTED_BIT) {
        ESP_LOGI(TAG_MQTT, "initial CONNACK received");
    } else {
        ESP_LOGW(TAG_MQTT, "no CONNACK in 15s — will keep retrying in background");
    }
}

static void publish_piece(const char *json, size_t len)
{
    if (mqtt_client == NULL) {
        ESP_LOGW(TAG_MQTT, "no client; piece not published");
        return;
    }
    /* QoS 1 so a transient disconnect doesn't silently drop the message. */
    int msg_id = esp_mqtt_client_publish(mqtt_client, mqtt_topic, json, (int)len, 1, 0);
    if (msg_id == -1) {
        ESP_LOGW(TAG_MQTT, "publish failed (not connected?)");
    } else {
        ESP_LOGI(TAG_MQTT, "publish queued (msg_id=%d, %u bytes)", msg_id, (unsigned)len);
    }
}


/* ============================================================
 * 11. Event pipeline (ISRs → task)
 * ============================================================ */
typedef enum {
    EVT_STITCH = 0,   /* one encoder pulse (any direction) = one stitch */
    EVT_FOOT   = 1,
    EVT_TRIM   = 2,
    EVT_WIPER  = 3,
} event_type_t;

typedef struct {
    event_type_t type;
    uint8_t      value;
    uint32_t     t_ms;
} gpio_event_t;

static QueueHandle_t event_queue = NULL;

typedef struct {
    gpio_num_t   pin;
    event_type_t type;
    int64_t      last_isr_time_us;
} button_ctx_t;

static button_ctx_t btn_foot  = { .pin = PIN_FOOT,  .type = EVT_FOOT,  .last_isr_time_us = 0 };
static button_ctx_t btn_trim  = { .pin = PIN_TRIM,  .type = EVT_TRIM,  .last_isr_time_us = 0 };
static button_ctx_t btn_wiper = { .pin = PIN_WIPER, .type = EVT_WIPER, .last_isr_time_us = 0 };

static inline uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}


/* ============================================================
 * 12. ISRs
 * ============================================================ */
static void IRAM_ATTR encoder_isr_handler(void *arg)
{
    gpio_event_t evt = {
        .type  = EVT_STITCH,
        .value = 0,
        .t_ms  = (uint32_t)(esp_timer_get_time() / 1000),
    };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(event_queue, &evt, &woken);
    portYIELD_FROM_ISR(woken);
}

static void IRAM_ATTR button_isr_handler(void *arg)
{
    button_ctx_t *btn = (button_ctx_t *) arg;
    int64_t now_us    = esp_timer_get_time();
    if (now_us - btn->last_isr_time_us > DEBOUNCE_US) {
        gpio_event_t evt = {
            .type  = btn->type,
            .value = (uint8_t) gpio_get_level(btn->pin),
            .t_ms  = (uint32_t)(now_us / 1000),
        };
        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(event_queue, &evt, &woken);
        portYIELD_FROM_ISR(woken);
    }
    btn->last_isr_time_us = now_us;
}


/* ============================================================
 * 13. Segmentation state
 * ============================================================ */
typedef enum { ST_IDLE, ST_SEWING, ST_ADJUSTING, ST_FINALIZING } piece_state_t;
typedef enum { SEG_STITCH = 0, SEG_ADJUST = 1 }                 segment_type_t;

typedef struct {
    segment_type_t type;
    uint32_t       start_ms;
    uint32_t       end_ms;
    uint32_t       stitch_count;
} segment_t;

static piece_state_t state                 = ST_IDLE;
static segment_t     segments[MAX_SEGMENTS];
static size_t        seg_count             = 0;

static uint32_t piece_start_ms             = 0;
static uint32_t last_stitch_ms             = 0;
static uint32_t last_activity_ms           = 0;
static uint32_t trim_start_ms              = 0;
static uint32_t last_piece_completed_at_ms = 0;
static uint32_t piece_seq                  = 0;
static uint32_t total_stitch_count         = 0;


/* ============================================================
 * 14. Segment helpers
 * ============================================================ */
static void open_segment(segment_type_t type, uint32_t t_ms)
{
    if (seg_count >= MAX_SEGMENTS) return;
    segments[seg_count].type         = type;
    segments[seg_count].start_ms     = t_ms;
    segments[seg_count].end_ms       = t_ms;
    segments[seg_count].stitch_count = 0;
    seg_count++;
}

static void close_current_segment(uint32_t t_ms)
{
    if (seg_count > 0) segments[seg_count - 1].end_ms = t_ms;
}


/* ============================================================
 * 15. Emit one piece — log to Serial AND publish to MQTT
 * ============================================================ */
static void emit_piece(uint32_t complete_ms, bool completed)
{
    cJSON *doc = cJSON_CreateObject();

    piece_seq++;
    cJSON_AddStringToObject(doc, "device_mac",            mac_str);
    cJSON_AddNumberToObject(doc, "piece_seq",             piece_seq);
    cJSON_AddNumberToObject(doc, "piece_started_at_ms",   piece_start_ms);
    cJSON_AddNumberToObject(doc, "piece_completed_at_ms", complete_ms);
    cJSON_AddNumberToObject(doc, "idle_before_ms",
        (last_piece_completed_at_ms > 0) ? (piece_start_ms - last_piece_completed_at_ms) : 0);
    cJSON_AddNumberToObject(doc, "total_cycle_time_ms",   complete_ms - piece_start_ms);

    uint32_t total_stitching_ms = 0;
    uint32_t total_adjust_ms    = 0;
    uint32_t adjust_count       = 0;
    for (size_t i = 0; i < seg_count; i++) {
        uint32_t dur = segments[i].end_ms - segments[i].start_ms;
        if (segments[i].type == SEG_STITCH) total_stitching_ms += dur;
        else                                 { total_adjust_ms += dur; adjust_count++; }
    }

    cJSON_AddNumberToObject(doc, "total_stitching_ms",    total_stitching_ms);
    cJSON_AddNumberToObject(doc, "total_adjustment_ms",   total_adjust_ms);
    cJSON_AddNumberToObject(doc, "total_stitches",        total_stitch_count);
    cJSON_AddNumberToObject(doc, "avg_stitch_hz",
        (total_stitching_ms > 0)
            ? ((double)total_stitch_count * 1000.0 / total_stitching_ms) : 0.0);
    cJSON_AddNumberToObject(doc, "adjustment_count",      adjust_count);
    cJSON_AddNumberToObject(doc, "trim_and_wipe_time_ms",
        (trim_start_ms > 0) ? (complete_ms - trim_start_ms) : 0);
    cJSON_AddStringToObject(doc, "status",                completed ? "COMPLETED" : "ABANDONED");

    cJSON *seg_array = cJSON_CreateArray();
    int    seg_idx   = 1;
    for (size_t i = 0; i < seg_count; i++) {
        uint32_t dur = segments[i].end_ms - segments[i].start_ms;
        if (dur == 0 && i < seg_count - 1) continue;

        cJSON *seg = cJSON_CreateObject();
        cJSON_AddNumberToObject(seg, "segment_index", seg_idx++);
        if (segments[i].type == SEG_STITCH) {
            cJSON_AddStringToObject(seg, "type",         "stitch");
            cJSON_AddNumberToObject(seg, "stitch_count", segments[i].stitch_count);
            cJSON_AddNumberToObject(seg, "duration_ms",  dur);
            cJSON_AddNumberToObject(seg, "avg_hz",
                (dur > 0) ? ((double)segments[i].stitch_count * 1000.0 / dur) : 0.0);
        } else {
            cJSON_AddStringToObject(seg, "type",        "adjust");
            cJSON_AddNumberToObject(seg, "duration_ms", dur);
        }
        cJSON_AddItemToArray(seg_array, seg);
    }
    cJSON_AddItemToObject(doc, "segments", seg_array);

    char *json_str = cJSON_PrintUnformatted(doc);
    if (json_str) {
        printf("\n=== PIECE %lu ===\n%s\n===============\n\n",
               (unsigned long)piece_seq, json_str);
        publish_piece(json_str, strlen(json_str));
        free(json_str);
    }
    cJSON_Delete(doc);

    last_piece_completed_at_ms = complete_ms;
    trim_start_ms              = 0;
    seg_count                  = 0;
    total_stitch_count         = 0;
}


/* ============================================================
 * 16. State-machine step
 * ============================================================ */
static void handle_event(const gpio_event_t *e)
{
    uint32_t t = e->t_ms;
    last_activity_ms = t;

    switch (e->type) {
    case EVT_STITCH:
        if (state == ST_IDLE) {
            state              = ST_SEWING;
            piece_start_ms     = t;
            seg_count          = 0;
            total_stitch_count = 0;
            open_segment(SEG_STITCH, t);
            ESP_LOGI(TAG, "piece #%lu started", (unsigned long)(piece_seq + 1));
        } else if (state == ST_ADJUSTING) {
            close_current_segment(t);
            state = ST_SEWING;
            open_segment(SEG_STITCH, t);
        }
        if (state == ST_SEWING && seg_count > 0) {
            segments[seg_count - 1].stitch_count++;
            segments[seg_count - 1].end_ms = t;
            total_stitch_count++;
            last_stitch_ms = t;
        }
        break;

    case EVT_FOOT:
        break;

    case EVT_TRIM:
        if (e->value == 0 && state != ST_IDLE && state != ST_FINALIZING) {
            close_current_segment(t);
            trim_start_ms = t;
            state         = ST_FINALIZING;
            ESP_LOGI(TAG, "trim detected → finalizing piece");
        }
        break;

    case EVT_WIPER:
        if (e->value == 1 && state == ST_FINALIZING) {
            emit_piece(t, /*completed=*/true);
            state = ST_IDLE;
        }
        break;
    }
}


/* ============================================================
 * 17. Timeouts
 * ============================================================ */
static void check_timeouts(uint32_t now)
{
    if (state == ST_SEWING && (now - last_stitch_ms) > STITCH_IDLE_MS_TO_ADJUST) {
        close_current_segment(last_stitch_ms);
        open_segment(SEG_ADJUST, last_stitch_ms);
        state = ST_ADJUSTING;
    }
    if (state == ST_ADJUSTING && (now - last_activity_ms) > PIECE_ABANDON_MS) {
        close_current_segment(now);
        ESP_LOGW(TAG, "piece abandoned after %lu ms of adjust silence",
                 (unsigned long)(now - last_activity_ms));
        emit_piece(now, /*completed=*/false);
        state = ST_IDLE;
    }
}


/* ============================================================
 * 18. GPIO setup
 * ============================================================ */
static void gpio_setup(void)
{
    gpio_config_t enc_conf = {
        .intr_type    = GPIO_INTR_NEGEDGE,
        .pin_bit_mask = (1ULL << PIN_ENC_A),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&enc_conf);

    gpio_reset_pin(PIN_ENC_B);
    gpio_set_direction(PIN_ENC_B, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_ENC_B, GPIO_PULLUP_ONLY);

    uint64_t button_mask = (1ULL << PIN_FOOT) | (1ULL << PIN_TRIM) | (1ULL << PIN_WIPER);
    gpio_config_t btn_conf = {
        .intr_type    = GPIO_INTR_ANYEDGE,
        .pin_bit_mask = button_mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    gpio_config(&btn_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_ENC_A, encoder_isr_handler, NULL);
    gpio_isr_handler_add(PIN_FOOT,  button_isr_handler,  &btn_foot);
    gpio_isr_handler_add(PIN_TRIM,  button_isr_handler,  &btn_trim);
    gpio_isr_handler_add(PIN_WIPER, button_isr_handler,  &btn_wiper);
}


/* ============================================================
 * 19. app_main
 * ============================================================ */
void app_main(void)
{
    /* Format MAC-derived strings early — esp_read_mac works before wifi start. */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(mac_str,        sizeof(mac_str),        "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(mqtt_topic,     sizeof(mqtt_topic),     "stitch/device/%s/pieces",     mac_str);
    snprintf(mqtt_client_id, sizeof(mqtt_client_id), "stitch-%s",                   mac_str);
    ESP_LOGI(TAG, "MAC:   %s", mac_str);
    ESP_LOGI(TAG, "topic: %s", mqtt_topic);

    /* WiFi + wait for IP */
    wifi_init_sta();
    if (!wifi_wait_for_ip(portMAX_DELAY)) {
        ESP_LOGE(TAG, "WiFi failed — restarting");
        vTaskDelay(pdMS_TO_TICKS(3000));
        esp_restart();
    }

#if DEBUG_FORCE_REACTIVATION
    nvs_factory_reset_stitch();
#endif

    /* Activation branch */
    nvs_load_activation();
    while (!activated_g) {
        ESP_LOGI(TAG_ACT, "not activated; attempting activation...");
        if (try_activate()) {
            ESP_LOGI(TAG_ACT, "success — restarting in 2s");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();   /* next boot takes the "already activated" path */
        }
        ESP_LOGW(TAG_ACT, "activation failed; retrying in 10s");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    /* Activated → bring up MQTT */
    mqtt_start();

    /* Set up GPIO + event queue */
    event_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(gpio_event_t));
    if (event_queue == NULL) {
        ESP_LOGE(TAG, "event_queue alloc failed");
        return;
    }
    gpio_setup();
    ESP_LOGI(TAG, "segmentation ready. sew a piece.");

    /* Main event loop */
    gpio_event_t evt;
    TickType_t   last_timeout_check = xTaskGetTickCount();
    const TickType_t timeout_period = pdMS_TO_TICKS(50);

    while (1) {
        if (xQueueReceive(event_queue, &evt, timeout_period) == pdTRUE) {
            handle_event(&evt);
            while (xQueueReceive(event_queue, &evt, 0) == pdTRUE) {
                handle_event(&evt);
            }
        }
        TickType_t now_ticks = xTaskGetTickCount();
        if ((now_ticks - last_timeout_check) >= timeout_period) {
            check_timeouts(millis());
            last_timeout_check = now_ticks;
        }
    }
}
