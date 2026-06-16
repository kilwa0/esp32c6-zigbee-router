#pragma once

/*
 * button.h — BOOT button (GPIO9) gesture handler
 *
 * Gestures detected after firmware is running:
 *
 *   Triple-tap  (<500 ms between presses)  Toggle TX power 20 dBm <-> 5 dBm
 *   Hold 3 s                               Clean reboot
 *   Hold 5 s                               Factory reset (NVS erased) + reboot
 *
 * Call button_init() once from app_main(), after configure_led().
 * The module installs its own GPIO ISR and FreeRTOS software timers;
 * no further interaction is required from the caller.
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise GPIO9 as input and install gesture detection.
 *
 * Must be called after configure_led() (LED strip must be ready) and
 * before esp_zigbee_start() so that the ISR service is installed while
 * the Zigbee stack lock is not yet held.
 *
 * @return ESP_OK on success, or an esp_err_t forwarded from the GPIO
 *         driver / timer subsystem on failure.
 */
esp_err_t button_init(void);

#ifdef __cplusplus
}
#endif
