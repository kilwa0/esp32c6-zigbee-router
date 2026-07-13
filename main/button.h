#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Initialise the BOOT button subsystem.
 *
 * Configures GPIO9 as input with internal pull-up, installs the ISR,
 * and creates the four FreeRTOS software timers used for gesture
 * detection (tap window, hold, blink, permit-join expiry).
 *
 * Must be called from app_main() before esp_zigbee_start().
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if timer allocation fails.
 */
esp_err_t button_init(void);

/**
 * @brief Returns true while LED night mode is active.
 *
 * Router state machine calls this before every LED update to suppress
 * visual feedback while the user has silenced the LED via single-tap.
 * Night mode is volatile and resets on reboot.
 *
 * @return true  Night mode active; LED updates from router.c suppressed.
 * @return false Normal operation; LED updates proceed as usual.
 */
bool button_is_night_mode(void);
bool do_night_mode_toggle(bool power);