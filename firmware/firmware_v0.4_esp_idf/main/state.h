#pragma once

/*
 * state.h — a single mutex-protected snapshot of every runtime signal the
 * UI, indicator LED, or any other observer might care about. Producers
 * (segmentation, WiFi handler, MQTT handler, NFC task, expander poller)
 * call setters; consumers copy the whole struct atomically via state_get().
 */

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DEV_ST_BOOT,           /* just powered on */
    DEV_ST_WIFI_WAIT,      /* WiFi connecting */
    DEV_ST_NOT_ACTIVATED,  /* WiFi up, waiting to POST /activate */
    DEV_ST_ACTIVATING,     /* POST in flight */
    DEV_ST_MQTT_WAIT,      /* activated, waiting for MQTT CONNACK */
    DEV_ST_IDLE,           /* ready, no piece in progress */
    DEV_ST_SEWING,         /* stitches arriving */
    DEV_ST_ADJUSTING,      /* mid-piece pause */
    DEV_ST_FINALIZING,     /* trim seen; waiting for wipe */
    DEV_ST_ERROR,          /* something wedged */
} device_state_t;

typedef struct {
    device_state_t state;
    /* live piece */
    uint32_t current_stitch_count;
    /* last completed piece */
    uint32_t last_piece_seq;
    uint32_t last_cycle_time_ms;
    double   last_stitching_ratio;   /* 0.0..1.0 */
    uint32_t last_adjustment_count;
    /* aggregates */
    uint32_t pieces_today;
    /* connectivity */
    bool     wifi_connected;
    bool     mqtt_connected;
    /* peripherals */
    char     nfc_uid[32];
    uint8_t  switch_mask;            /* TCA9534 lower 4 bits */
} device_snapshot_t;

void        state_init(void);
void        state_get(device_snapshot_t *out);
const char *state_name(device_state_t s);

void state_set_device(device_state_t s);
void state_set_current_stitches(uint32_t n);
void state_note_piece_complete(uint32_t seq, uint32_t cycle_ms,
                               uint32_t stitching_ms, uint32_t adjustment_count);
void state_set_wifi(bool connected);
void state_set_mqtt(bool connected);
void state_set_nfc(const char *uid);
void state_set_switches(uint8_t mask);
