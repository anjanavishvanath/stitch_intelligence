#pragma once

/*
 * indicator — single WS2812 NeoPixel on NPLED_GPIO.
 * Reads shared state at 20 Hz and paints:
 *   - a per-state colour (booting = blue, sewing = green, adjusting = amber, etc.)
 *   - a brief flash on every completed piece, tinted by that piece's stitching ratio
 * Zero coupling to the segmentation code — it only reads state_get().
 */

void indicator_init(void);
void indicator_start_task(void);
