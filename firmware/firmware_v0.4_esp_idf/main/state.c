#include "state.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static device_snapshot_t g;
static SemaphoreHandle_t g_mtx = NULL;

#define LOCK()   xSemaphoreTake(g_mtx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(g_mtx)

void state_init(void)
{
    g_mtx = xSemaphoreCreateMutex();
    memset(&g, 0, sizeof(g));
    g.state = DEV_ST_BOOT;
    strcpy(g.nfc_uid, "SEARCHING...");
}

void state_get(device_snapshot_t *out)
{
    LOCK();
    memcpy(out, &g, sizeof(g));
    UNLOCK();
}

const char *state_name(device_state_t s)
{
    switch (s) {
    case DEV_ST_BOOT:          return "BOOTING";
    case DEV_ST_WIFI_WAIT:     return "WIFI...";
    case DEV_ST_NOT_ACTIVATED: return "NEED SLPT";
    case DEV_ST_ACTIVATING:    return "ACTIVATING";
    case DEV_ST_MQTT_WAIT:     return "MQTT...";
    case DEV_ST_IDLE:          return "READY";
    case DEV_ST_SEWING:        return "SEWING";
    case DEV_ST_ADJUSTING:     return "ADJUSTING";
    case DEV_ST_FINALIZING:    return "FINISHING";
    case DEV_ST_ERROR:         return "ERROR";
    default:                   return "?";
    }
}

void state_set_device(device_state_t s)             { LOCK(); g.state = s; UNLOCK(); }
void state_set_current_stitches(uint32_t n)         { LOCK(); g.current_stitch_count = n; UNLOCK(); }
void state_set_wifi(bool c)                         { LOCK(); g.wifi_connected = c; UNLOCK(); }
void state_set_mqtt(bool c)                         { LOCK(); g.mqtt_connected = c; UNLOCK(); }
void state_set_switches(uint8_t mask)               { LOCK(); g.switch_mask = mask; UNLOCK(); }

void state_set_nfc(const char *uid)
{
    LOCK();
    strncpy(g.nfc_uid, uid, sizeof(g.nfc_uid) - 1);
    g.nfc_uid[sizeof(g.nfc_uid) - 1] = '\0';
    UNLOCK();
}

void state_note_piece_complete(uint32_t seq, uint32_t cycle_ms,
                               uint32_t stitching_ms, uint32_t adjustment_count)
{
    LOCK();
    g.last_piece_seq         = seq;
    g.last_cycle_time_ms     = cycle_ms;
    g.last_stitching_ratio   = cycle_ms > 0 ? (double)stitching_ms / cycle_ms : 0.0;
    g.last_adjustment_count  = adjustment_count;
    g.pieces_today          += 1;
    g.current_stitch_count   = 0;
    UNLOCK();
}
