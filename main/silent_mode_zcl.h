// silent_mode_zcl.h
#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Synchronize the silent/night mode state with the Zigbee On/Off
 * attribute.
 *
 * When silent mode is enabled, the status LED is suppressed locally and the
 * Zigbee On/Off attribute is set to 0 (OFF) to reflect that visible state.
 *
 * @param silent_mode_enabled true to enable silent mode, false to disable.
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t silent_mode_zcl_sync(bool silent_mode_enabled);