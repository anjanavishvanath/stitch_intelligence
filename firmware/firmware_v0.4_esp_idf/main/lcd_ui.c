#include "lcd_ui.h"
#include "state.h"
#include "expander.h"
#include "hardware_config.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lvgl.h"

/* ---- Display config (landscape) --------------------------------------- */
#define LCD_H_RES              320
#define LCD_V_RES              240
#define LVGL_DRAW_BUF_LINES    30
#define LVGL_DRAW_BUF_SIZE     (LCD_H_RES * LVGL_DRAW_BUF_LINES)

/* ---- Touch config ----------------------------------------------------- */
#define TSC2007_I2C_ADDR       0x48
#define TSC2007_CMD_MEASURE_X  0xC0
#define TSC2007_CMD_MEASURE_Y  0xD0
#define TOUCH_MIN_X            250
#define TOUCH_MAX_X            3850
#define TOUCH_MIN_Y            350
#define TOUCH_MAX_Y            3750

static const char *TAG = "UI";
static i2c_master_dev_handle_t g_touch      = NULL;
static SemaphoreHandle_t       g_lcd_dma    = NULL;
static volatile bool           g_dma_busy   = false;

/* UI labels — mutated inside the LVGL timer (runs on the GUI task, single-threaded). */
static lv_obj_t *lbl_state      = NULL;
static lv_obj_t *lbl_stitches   = NULL;
static lv_obj_t *lbl_pieces     = NULL;
static lv_obj_t *lbl_cycle      = NULL;
static lv_obj_t *lbl_ratio      = NULL;
static lv_obj_t *lbl_adj        = NULL;
static lv_obj_t *lbl_nfc        = NULL;
static lv_obj_t *lbl_switches   = NULL;
static lv_obj_t *lbl_wifi       = NULL;
static lv_obj_t *lbl_mqtt       = NULL;

static uint8_t g_led_state = 0;

LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_18);
LV_FONT_DECLARE(lv_font_montserrat_14);


/* =====================================================================
 * LVGL system callbacks
 * ===================================================================== */
static uint32_t lvgl_tick_cb(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool notify_lcd_trans_done(esp_lcd_panel_io_handle_t io,
                                  esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    g_dma_busy = false;
    xSemaphoreGiveFromISR(g_lcd_dma, &woken);
    return woken == pdTRUE;
}

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t) lv_display_get_user_data(disp);
    if (g_dma_busy) xSemaphoreTake(g_lcd_dma, portMAX_DELAY);
    g_dma_busy = true;
    esp_lcd_panel_draw_bitmap(panel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(disp);
}


/* =====================================================================
 * Touch
 * ===================================================================== */
static esp_err_t tsc2007_read(uint8_t cmd, uint16_t *out)
{
    uint8_t rx[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(g_touch, &cmd, 1, rx, 2, -1);
    if (err == ESP_OK) *out = (rx[0] << 4) | (rx[1] >> 4);
    return err;
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    if (gpio_get_level(TOUCH_PENIRQ_GPIO) == 0) {
        uint16_t x = 0, y = 0;
        if (tsc2007_read(TSC2007_CMD_MEASURE_X, &x) == ESP_OK &&
            tsc2007_read(TSC2007_CMD_MEASURE_Y, &y) == ESP_OK) {
            if (x < TOUCH_MIN_X) x = TOUCH_MIN_X;
            if (x > TOUCH_MAX_X) x = TOUCH_MAX_X;
            if (y < TOUCH_MIN_Y) y = TOUCH_MIN_Y;
            if (y > TOUCH_MAX_Y) y = TOUCH_MAX_Y;
            data->point.x = ((x - TOUCH_MIN_X) * LCD_H_RES) / (TOUCH_MAX_X - TOUCH_MIN_X);
            data->point.y = ((y - TOUCH_MIN_Y) * LCD_V_RES) / (TOUCH_MAX_Y - TOUCH_MIN_Y);
            data->state = LV_INDEV_STATE_PRESSED;
            return;
        }
    }
    data->state = LV_INDEV_STATE_RELEASED;
}


/* =====================================================================
 * Dashboard
 * ===================================================================== */
static void led_btn_cb(lv_event_t *e)
{
    g_led_state = (g_led_state == 0) ? 0x0F : 0x00;
    expander_write_leds(g_led_state);
}

static void ui_timer_cb(lv_timer_t *t)
{
    /* Poll expander switches into shared state, then read snapshot for UI. */
    uint8_t sw = 0;
    if (expander_read_switches(&sw) == ESP_OK) state_set_switches(sw);

    device_snapshot_t s;
    state_get(&s);
    char buf[64];

    lv_label_set_text(lbl_state, state_name(s.state));

    snprintf(buf, sizeof(buf), "%lu", (unsigned long) s.current_stitch_count);
    lv_label_set_text(lbl_stitches, buf);

    snprintf(buf, sizeof(buf), "Pieces: %lu", (unsigned long) s.pieces_today);
    lv_label_set_text(lbl_pieces, buf);

    if (s.last_piece_seq > 0) {
        snprintf(buf, sizeof(buf), "Cycle:  %.1fs", s.last_cycle_time_ms / 1000.0);
        lv_label_set_text(lbl_cycle, buf);
        snprintf(buf, sizeof(buf), "Stitch: %.0f%%", s.last_stitching_ratio * 100.0);
        lv_label_set_text(lbl_ratio, buf);
        snprintf(buf, sizeof(buf), "Adj:    %lu", (unsigned long) s.last_adjustment_count);
        lv_label_set_text(lbl_adj, buf);
    }

    snprintf(buf, sizeof(buf), "NFC: %s", s.nfc_uid);
    lv_label_set_text(lbl_nfc, buf);

    snprintf(buf, sizeof(buf), "SW: 0x%X", s.switch_mask);
    lv_label_set_text(lbl_switches, buf);

    lv_label_set_text(lbl_wifi, s.wifi_connected ? "WiFi ✓" : "WiFi ×");
    lv_label_set_text(lbl_mqtt, s.mqtt_connected ? "MQTT ✓" : "MQTT ×");
    lv_obj_set_style_text_color(lbl_wifi,
        lv_color_hex(s.wifi_connected ? 0x4CAF50 : 0xFF5252), 0);
    lv_obj_set_style_text_color(lbl_mqtt,
        lv_color_hex(s.mqtt_connected ? 0x4CAF50 : 0xFF5252), 0);
}

static void build_dashboard(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x121214), 0);

    /* --- Header -------------------------------------------------------- */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, LCD_H_RES, 28);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1E1E24), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 2, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "STITCH INTELLIGENCE");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    lbl_wifi = lv_label_create(hdr);
    lv_label_set_text(lbl_wifi, "WiFi ×");
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_RIGHT_MID, -60, 0);

    lbl_mqtt = lv_label_create(hdr);
    lv_label_set_text(lbl_mqtt, "MQTT ×");
    lv_obj_set_style_text_font(lbl_mqtt, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_mqtt, LV_ALIGN_RIGHT_MID, -4, 0);

    /* --- Left column: current state + live stitch counter -------------- */
    lbl_state = lv_label_create(scr);
    lv_label_set_text(lbl_state, "BOOTING");
    lv_obj_set_style_text_color(lbl_state, lv_color_hex(0x00BCD4), 0);
    lv_obj_set_style_text_font(lbl_state, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_state, LV_ALIGN_TOP_LEFT, 12, 40);

    lbl_stitches = lv_label_create(scr);
    lv_label_set_text(lbl_stitches, "0");
    lv_obj_set_style_text_color(lbl_stitches, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl_stitches, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl_stitches, LV_ALIGN_TOP_LEFT, 12, 82);

    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "stitches");
    lv_obj_set_style_text_color(sub, lv_color_hex(0xA0A0B0), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 12, 118);

    /* --- Right column: rollup ------------------------------------------ */
    lbl_pieces = lv_label_create(scr);
    lv_label_set_text(lbl_pieces, "Pieces: 0");
    lv_obj_set_style_text_color(lbl_pieces, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_font(lbl_pieces, &lv_font_montserrat_18, 0);
    lv_obj_align(lbl_pieces, LV_ALIGN_TOP_RIGHT, -12, 38);

    lbl_cycle = lv_label_create(scr);
    lv_label_set_text(lbl_cycle, "Cycle:  --");
    lv_obj_set_style_text_color(lbl_cycle, lv_color_hex(0xA0A0B0), 0);
    lv_obj_set_style_text_font(lbl_cycle, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_cycle, LV_ALIGN_TOP_RIGHT, -12, 68);

    lbl_ratio = lv_label_create(scr);
    lv_label_set_text(lbl_ratio, "Stitch: --");
    lv_obj_set_style_text_color(lbl_ratio, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_text_font(lbl_ratio, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_ratio, LV_ALIGN_TOP_RIGHT, -12, 88);

    lbl_adj = lv_label_create(scr);
    lv_label_set_text(lbl_adj, "Adj:    --");
    lv_obj_set_style_text_color(lbl_adj, lv_color_hex(0xFFB300), 0);
    lv_obj_set_style_text_font(lbl_adj, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_adj, LV_ALIGN_TOP_RIGHT, -12, 108);

    /* --- Footer: NFC UID, switches, LED-toggle test button ------------- */
    lbl_nfc = lv_label_create(scr);
    lv_label_set_text(lbl_nfc, "NFC: SEARCHING...");
    lv_obj_set_style_text_color(lbl_nfc, lv_color_hex(0xFF7F27), 0);
    lv_obj_set_style_text_font(lbl_nfc, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_nfc, LV_ALIGN_BOTTOM_LEFT, 12, -34);

    lbl_switches = lv_label_create(scr);
    lv_label_set_text(lbl_switches, "SW: 0x0");
    lv_obj_set_style_text_color(lbl_switches, lv_color_hex(0xFFCC00), 0);
    lv_obj_set_style_text_font(lbl_switches, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_switches, LV_ALIGN_BOTTOM_LEFT, 12, -14);

    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 110, 30);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -12, -14);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFF3366), 0);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_add_event_cb(btn, led_btn_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "TOGGLE LEDS");
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);

    lv_timer_create(ui_timer_cb, 250, NULL);
}


/* =====================================================================
 * GUI task
 * ===================================================================== */
static void gui_task(void *pv)
{
    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t) pv;

    lv_init();
    lv_tick_set_cb(lvgl_tick_cb);

    uint16_t *buf1 = heap_caps_malloc(LVGL_DRAW_BUF_SIZE * sizeof(uint16_t),
                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    uint16_t *buf2 = heap_caps_malloc(LVGL_DRAW_BUF_SIZE * sizeof(uint16_t),
                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_user_data(disp, panel);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_buffers(disp, buf1, buf2,
                           LVGL_DRAW_BUF_SIZE * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read_cb);

    build_dashboard();

    while (1) {
        uint32_t d = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(d < 1 ? 1 : (d > 30 ? 30 : d)));
    }
}


/* =====================================================================
 * Public entry point — bring up LCD + touch, start GUI task
 * ===================================================================== */
void lcd_ui_start(i2c_master_bus_handle_t i2c_bus)
{
    g_lcd_dma = xSemaphoreCreateBinary();

    /* Touch controller on shared I2C bus */
    i2c_device_config_t touch_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = TSC2007_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &touch_cfg, &g_touch));

    gpio_config_t touch_irq = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pin_bit_mask = (1ULL << TOUCH_PENIRQ_GPIO),
    };
    gpio_config(&touch_irq);

    /* LCD pins reset */
    const gpio_num_t lcd_pins[] = {
        LCD_DATA0_GPIO, LCD_DATA1_GPIO, LCD_DATA2_GPIO, LCD_DATA3_GPIO,
        LCD_DATA4_GPIO, LCD_DATA5_GPIO, LCD_DATA6_GPIO, LCD_DATA7_GPIO,
        LCD_CS_GPIO,    LCD_DC_GPIO,    LCD_WR_GPIO,    LCD_RD_GPIO,
        LCD_RESET_GPIO, LCD_BL_CONTROL_GPIO,
    };
    for (size_t i = 0; i < sizeof(lcd_pins) / sizeof(lcd_pins[0]); i++) {
        gpio_reset_pin(lcd_pins[i]);
    }
    gpio_config_t rd_cfg = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = (1ULL << LCD_RD_GPIO) };
    gpio_config(&rd_cfg);
    gpio_set_level(LCD_RD_GPIO, 1);
    gpio_config_t bl_cfg = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = (1ULL << LCD_BL_CONTROL_GPIO) };
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_BL_CONTROL_GPIO, 1);

    /* i80 bus */
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .clk_src           = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num       = LCD_DC_GPIO,
        .wr_gpio_num       = LCD_WR_GPIO,
        .data_gpio_nums    = { LCD_DATA0_GPIO, LCD_DATA1_GPIO, LCD_DATA2_GPIO, LCD_DATA3_GPIO,
                               LCD_DATA4_GPIO, LCD_DATA5_GPIO, LCD_DATA6_GPIO, LCD_DATA7_GPIO },
        .bus_width         = 8,
        .max_transfer_bytes = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_cfg, &i80_bus));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i80_config_t io_cfg = {
        .cs_gpio_num       = LCD_CS_GPIO,
        .pclk_hz           = 10 * 1000 * 1000,
        .trans_queue_depth = 10,
        .dc_levels         = { .dc_idle_level = 0, .dc_cmd_level = 0,
                                .dc_dummy_level = 0, .dc_data_level = 1 },
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_cfg, &io));
    esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = notify_lcd_trans_done };
    esp_lcd_panel_io_register_event_callbacks(io, &cbs, NULL);

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RESET_GPIO,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,
        .data_endian    = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_cfg, &panel));
    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_swap_xy(panel, true);
    esp_lcd_panel_mirror(panel, false, true);
    esp_lcd_panel_disp_on_off(panel, true);

    xTaskCreatePinnedToCore(gui_task, "gui", 8192, panel, 5, NULL, 1);
    ESP_LOGI(TAG, "GUI task started on core 1");
}
