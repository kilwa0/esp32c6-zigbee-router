#include "silent_mode_zcl.h"

#include "esp_log.h"
#include "esp_zigbee.h"
#include "freertos/FreeRTOS.h"

#include "router.h"

static const char *TAG = "SILENT_MODE_ZCL";

esp_err_t silent_mode_zcl_sync(bool silent_mode_enabled) {
    uint8_t zb_val = silent_mode_enabled ? 0 : 1;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_err_t err = ezb_zcl_set_attr_value(
                        EP_ID,
                        EZB_ZCL_CLUSTER_ID_ON_OFF,
                        EZB_ZCL_CLUSTER_SERVER,
                        EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
                        EZB_ZCL_STD_MANUF_CODE,
                        &zb_val,
                        false
                    );

        if (err != ESP_OK) {
            esp_zigbee_lock_release();
            ESP_LOGW(TAG, "Failed to set On/Off attr: 0x%x", err);
            return ESP_FAIL;
            }

    ezb_zcl_report_attr_cmd_t report = {
        .cmd_ctrl = {
            .cluster_id = EZB_ZCL_CLUSTER_ID_ON_OFF,
            .dst_ep = EP_ID
        },
        .payload = {
            .attr_id = EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID
            }
        };

    err = ezb_zcl_report_attr_cmd_req(&report);
    esp_zigbee_lock_release();

    return (err == ESP_OK) ? ESP_OK : ESP_FAIL;
}