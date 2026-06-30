#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* -------------------------------------------------------------------------
 * Gesture action IDs -- shared between button.c (physical) and router.c
 * (remote ZCL trigger).  Values map 1-to-1 to the physical gestures:
 *
 *   BUTTON_ACTION_NIGHT_MODE  -- 1x tap
 *   BUTTON_ACTION_PERMIT_JOIN -- 2x tap
 *   BUTTON_ACTION_TX_TOGGLE   -- 3x tap
 *   BUTTON_ACTION_FACTORY_RST -- hold 5 s
 *
 * These values MUST match ROUTER_CMD_* in router.h.
 * ---------------------------------------------------------------------- */
typedef enum {
    BUTTON_ACTION_NIGHT_MODE  = 0x01,
    BUTTON_ACTION_PERMIT_JOIN = 0x02,
    BUTTON_ACTION_TX_TOGGLE   = 0x03,
    BUTTON_ACTION_FACTORY_RST = 0x04,
} button_action_t;

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

/**
 * @brief Execute a gesture action programmatically (remote trigger).
 *
 * Executes the same code path as the corresponding physical button
 * gesture, including LED feedback.  Safe to call from any FreeRTOS
 * task; internally acquires the Zigbee lock where needed (same as
 * the physical path).
 *
 * Intended to be called from the ZCL custom cluster command handler
 * in router.c when the coordinator sends a remote command.
 *
 * @param action  One of the BUTTON_ACTION_* values.
 * @return ESP_OK            on success.
 * @return ESP_ERR_INVALID_ARG  unknown action value.
 */
esp_err_t button_remote_trigger(button_action_t action);
