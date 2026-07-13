// silent_mode_zcl.h
#pragma once
#include "esp_zigbee.h"

/**
 * @brief Synchronize the silent mode state with the Zigbee network.
 *
 * @param enabled true to enable silent mode, false to disable.
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t silent_mode_zcl_sync(bool enabled);