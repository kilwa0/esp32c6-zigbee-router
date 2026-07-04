/**
 * @file gesture_cluster.c
 * @brief Implementation of the custom gesture cluster (0xFC01).
 *
 * Registers a ZCL endpoint with the gesture server cluster and
 * provides a send helper used by button.c.
 *
 * NOTE: This file uses the esp-zigbee-sdk EZB AF / ZCL wrapper
 * (ezb_af_*, ezb_zcl_*) which is the same API shown in Espressif's
 * custom-cluster example and already used by router.c in this project.
 */

#include "gesture_cluster.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee.h"          /* ezb_af_*, ezb_zcl_* types        */
#include "esp_zigbee_zcl_command.h"

static const char *TAG = "GESTURE_CLUSTER";

/* -------------------------------------------------------------------------
 * Coordinator destination (fixed for a router-to-coordinator report)
 * ---------------------------------------------------------------------- */
#define COORD_SHORT_ADDR   0x0000U
#define COORD_EP_ID        1U

/* -------------------------------------------------------------------------
 * Cluster init callback (mandatory even if empty)
 * ---------------------------------------------------------------------- */
static esp_err_t gesture_cluster_init(void)
{
    ESP_LOGI(TAG, "Gesture cluster 0x%04X initialised on EP %d",
             GESTURE_CLUSTER_ID, GESTURE_EP_ID);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * gesture_cluster_register()
 * ---------------------------------------------------------------------- */
esp_err_t gesture_cluster_register(void)
{
    /* ----- Cluster descriptors ----- */
    ezb_zcl_basic_cluster_server_config_t basic_cfg = {
        .zcl_version  = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = EZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,
    };
    ezb_zcl_cluster_desc_t basic_desc =
        ezb_zcl_basic_create_cluster_desc(&basic_cfg, EZB_ZCL_CLUSTER_SERVER);
    ezb_zcl_basic_cluster_desc_add_attr(
        basic_desc,
        EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
        (void *)"\x09" "ESPRESSIF");
    ezb_zcl_basic_cluster_desc_add_attr(
        basic_desc,
        EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
        (void *)"\x0C" "esp32c6-router");

    ezb_zcl_identify_cluster_server_config_t identify_cfg = {
        .identify_time = EZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };
    ezb_zcl_cluster_desc_t identify_desc =
        ezb_zcl_identify_create_cluster_desc(&identify_cfg, EZB_ZCL_CLUSTER_SERVER);

    /* Custom gesture cluster — server side */
    ezb_zcl_custom_cluster_config_t gesture_cfg = {
        .cluster_id  = GESTURE_CLUSTER_ID,
        .init_func   = gesture_cluster_init,
        .deinit_func = NULL,
    };
    ezb_zcl_cluster_desc_t gesture_desc =
        ezb_zcl_custom_create_cluster_desc(&gesture_cfg, EZB_ZCL_CLUSTER_SERVER);

    /* ----- Endpoint descriptor ----- */
    ezb_af_ep_config_t ep_cfg = {
        .ep_id              = GESTURE_EP_ID,
        .app_profile_id     = GESTURE_PROFILE_ID,
        .app_device_id      = GESTURE_DEVICE_ID,
        .app_device_version = 0,
    };
    ezb_af_ep_desc_t ep_desc = ezb_af_create_endpoint_desc(&ep_cfg);

    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, basic_desc));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, identify_desc));
    ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, gesture_desc));

    /* ----- Device descriptor ----- */
    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(dev_desc, ep_desc));
    ESP_ERROR_CHECK(ezb_af_device_desc_register(dev_desc));

    ESP_LOGI(TAG, "Gesture EP %d registered (cluster 0x%04X)",
             GESTURE_EP_ID, GESTURE_CLUSTER_ID);
    return ESP_OK;
}

/* -------------------------------------------------------------------------
 * gesture_cluster_send_cmd()
 *
 * Builds a ZCL cluster-specific command frame (direction: client→server
 * on the coordinator side) and sends it as APS unicast.
 *
 * Frame format (ZCL header + 1-byte payload):
 *   [Frame ctrl][Seq][Cmd ID][value]
 * ---------------------------------------------------------------------- */
esp_err_t gesture_cluster_send_cmd(uint8_t cmd_id, uint8_t value)
{
    /* Destination: coordinator short address 0x0000, EP 1 */
    esp_zb_zcl_custom_cluster_cmd_req_t req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = COORD_SHORT_ADDR,
            .dst_endpoint          = COORD_EP_ID,
            .src_endpoint          = GESTURE_EP_ID,
        },
        .address_mode  = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .profile_id    = GESTURE_PROFILE_ID,
        .cluster_id    = GESTURE_CLUSTER_ID,
        .custom_cmd_id = cmd_id,
        .direction     = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        /* 1-byte payload */
        .data.type     = ESP_ZB_ZCL_ATTR_TYPE_U8,
        .data.value    = &value,
    };

    esp_err_t err = esp_zb_zcl_custom_cluster_cmd_req(&req);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gesture cmd 0x%02X send failed: %s",
                 cmd_id, esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "gesture cmd 0x%02X sent (value=%u)", cmd_id, value);
    }
    return err;
}
