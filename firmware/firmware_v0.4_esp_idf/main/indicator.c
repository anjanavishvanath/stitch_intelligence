#include "indicator.h"
#include "state.h"
#include "hardware_config.h"

#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_strip.h"

static const char *TAG = "LED";
static led_strip_handle_t g_strip = NULL;

/* Breathing envelope: sine 0..1 over `period_ms`, scaled to [min,max]. */
static uint8_t breathe(uint32_t t_ms, uint32_t period_ms, uint8_t min_v, uint8_t max_v)
{
    float phase = (float)(t_ms % period_ms) / (float)period_ms;
    float s     = 0.5f * (1.0f - cosf(2.0f * 3.14159265f * phase));
    return (uint8_t)(min_v + (max_v - min_v) * s);
}

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!g_strip) return;
    led_strip_set_pixel(g_strip, 0, r, g, b);
    led_strip_refresh(g_strip);
}

void indicator_init(void)
{
    led_strip_config_t sc = {
        .strip_gpio_num = NPLED_GPIO,
        .max_leds       = 1,
        .led_model      = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rc = {
        .clk_src        = RMT_CLK_SRC_DEFAULT,
        .resolution_hz  = 10 * 1000 * 1000,
    };
    esp_err_t err = led_strip_new_rmt_device(&sc, &rc, &g_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        g_strip = NULL;
        return;
    }
    led_strip_clear(g_strip);
    ESP_LOGI(TAG, "NeoPixel ready on GPIO %d", NPLED_GPIO);
}

static void paint_state(device_snapshot_t *s, uint32_t now_ms)
{
    uint8_t r = 0, g = 0, b = 0;
    uint8_t br = breathe(now_ms, 2000, 20, 180);

    switch (s->state) {
    case DEV_ST_BOOT:
    case DEV_ST_WIFI_WAIT:
        r = 0;  g = 0;             b = br;                  /* blue breathing */
        break;
    case DEV_ST_NOT_ACTIVATED:
    case DEV_ST_ACTIVATING:
        r = br; g = (uint8_t)(br * 0.65f); b = 0;           /* amber breathing */
        break;
    case DEV_ST_MQTT_WAIT:
        r = 0;  g = br;            b = br;                  /* cyan breathing */
        break;
    case DEV_ST_IDLE:
        r = 0;  g = 30;            b = 0;                   /* dim green solid */
        break;
    case DEV_ST_SEWING:
        r = 0;  g = breathe(now_ms, 800, 60, 220); b = 0;   /* bright green pulse */
        break;
    case DEV_ST_ADJUSTING:
        r = breathe(now_ms, 1500, 40, 200);
        g = (uint8_t)(r * 0.55f);
        b = 0;                                              /* amber pulse */
        break;
    case DEV_ST_FINALIZING:
        r = 0;
        g = breathe(now_ms, 400, 30, 180);
        b = breathe(now_ms, 400, 30, 180);                  /* cyan fast pulse */
        break;
    case DEV_ST_ERROR:
        r = breathe(now_ms, 500, 20, 255); g = 0; b = 0;    /* red fast pulse */
        break;
    }
    set_rgb(r, g, b);
}

static void indicator_task(void *pv)
{
    device_snapshot_t s;
    uint32_t last_piece_seq   = 0;
    uint32_t flash_until_ms   = 0;
    uint8_t  flash_r = 0, flash_g = 0, flash_b = 0;

    while (1) {
        state_get(&s);
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        /* Piece just completed? Set a colour-tinted flash for ~500 ms. */
        if (s.last_piece_seq != last_piece_seq && s.last_piece_seq > 0) {
            last_piece_seq  = s.last_piece_seq;
            flash_until_ms  = now + 500;
            if (s.last_stitching_ratio >= 0.50) {
                flash_r = 0;   flash_g = 255; flash_b = 0;    /* strong green — good piece */
            } else if (s.last_stitching_ratio >= 0.25) {
                flash_r = 255; flash_g = 180; flash_b = 0;    /* amber — mediocre */
            } else {
                flash_r = 255; flash_g = 0;   flash_b = 0;    /* red — heavy adjustment */
            }
        }

        if (now < flash_until_ms) {
            set_rgb(flash_r, flash_g, flash_b);
        } else {
            paint_state(&s, now);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void indicator_start_task(void)
{
    xTaskCreatePinnedToCore(indicator_task, "led", 3072, NULL, 3, NULL, 0);
}
