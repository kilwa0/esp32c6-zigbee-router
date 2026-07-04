/**
 * @file gesture_cluster.c
 * @brief Custom gesture cluster (0xFC01) — server side on the router.
 *
 * Follows the same patterns as router.c and the Espressif
 * data_producer example (esp-zigbee-sdk/examples/customized_devices).
 */

#include "gesture_cluster.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_zigbee.h"                         /* esp_zigbee_lock_*       */
#include "ezbee/af.h"                           /* ezb_af_*                */
#include "ezbee/zcl/cluster/basic_desc.h"       /* ezb_zcl_basic_*         */
#include "ezbee/zcl/cluster/identify_desc.h"    /* ezb_zcl_identify_*      */
#include "ezbee/zcl/cluster/custom.h"           /* ezb_zcl_custom_*        */

static const char *TAG = "GESTURE_CLUSTER";

/* -------------------------------------------------------------------------
 * Coordinator destination
 * ---------------------------------------------------------------------- */
#define COORD_SHORT_ADDR  0x0000U
#define COORD_EP_ID       1U

/* -------------------------------------------------------------------------
 * Command handler (coordinator → router inbound — not expected here,
 * but required by ezb_zcl_custom_cluster_handlers_t)
 * ---------------------------------------------------------------------- */
static ezb_zcl_status_t gesture_process_cmd_cb(const ezb_zcl_cmd_hdr_t *header,
                                               const uint8_t           *payload,
                                               uint16_t                 payload_length)
{
    (void)payload;
    (void)payload_length;
    ESP_LOGW(TAG, "Unexpected inbound cmd 0x%02X on gesture cluster", header->cmd_id);
    return EZB_ZCL_STATUS_UNSUP_CMD;
}

/* -------------------------------------------------------------------------
 * Cluster init callback — called by the stack when the EP is initialised.
 * Registers the command handler so the stack knows how to route frames.
 * ---------------------------------------------------------------------- */
static void gesture_cluster_init_cb(uint8_t ep_id)
{
    ESP_LOGI(TAG, "Gesture cluster 0x%04X init on EP %d", GESTURE_CLUSTER_ID, ep_id);

    static const ezb_zcl_custom_cluster_handlers_t handlers = {
        .cluster_id     = GESTURE_CLUSTER_ID,
        .cluster_role   = EZB_ZCL_CLUSTER_SERVER,
        .process_cmd_cb = gesture_process_cmd_cb,
        .check_value_cb = NULL,
        .write_attr_cb  = NULL,
        .cmd_disc_cb    = NULL,
    };
    ezb_err_t err = ezb_zcl_custom_cluster_handlers_register(&handlers);
    if (err != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "handlers_register failed: 0x%04X", err);
    }
}

/* -------------------------------------------------------------------------
 * gesture_cluster_register()
 * ---------------------------------------------------------------------- */
esp_err_t gesture_cluster_register(void)
{
    /* ----- Basic cluster ----- */
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

    /* ----- Identify cluster ----- */
    ezb_zcl_identify_cluster_server_config_t identify_cfg = {
        .identify_time = EZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
    };
    ezb_zcl_cluster_desc_t identify_desc =
        ezb_zcl_identify_create_cluster_desc(&identify_cfg, EZB_ZCL_CLUSTER_SERVER);

    /* ----- Custom gesture cluster (server) ----- */
    ezb_zcl_custom_cluster_config_t gesture_cfg = {
        .cluster_id  = GESTURE_CLUSTER_ID,
        .init_func   = gesture_cluster_init_cb,   /* registers cmd handlers */
        .deinit_func = NULL,
    };
    ezb_zcl_cluster_desc_t gesture_desc =
        ezb_zcl_custom_create_cluster_desc(&gesture_cfg, EZB_ZCL_CLUSTER_SERVER);

    /* ----- Endpoint ----- */
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
 * ezb_address_t layout (from ezbee/core_types.h):
 *
 *   typedef struct ezb_address_s {
 *       ezb_addr_mode_t addr_mode;   // EZB_ADDR_MODE_16_ENDP, etc.
 *       ezb_addr_t u;                // union { uint16_t addr_short; ... }
 *   } ezb_address_t;
 *
 * So the short address lives at  dst_addr.u.addr_short, NOT dst_addr.addr_short.
 *
 * fc.direction       = EZB_ZCL_CMD_DIRECTION_TO_SRV (frame goes to server)
 * fc.manuf_specific  = 0   (standard cluster-specific frame)
 * fc.dis_default_rsp = 1   (no default-response ack needed)
 * ---------------------------------------------------------------------- */
esp_err_t gesture_cluster_send_cmd(uint8_t cmd_id, uint8_t value)
{
    ezb_zcl_custom_cluster_cmd_t cmd = {
        .cmd_ctrl = {
            .dst_addr = {
                .addr_mode    = EZB_ADDR_MODE_16_ENDP,
                .u.addr_short = COORD_SHORT_ADDR,   /* coordinator 0x0000 */
            },
            .dst_ep     = COORD_EP_ID,
            .src_ep     = GESTURE_EP_ID,
            .cluster_id = GESTURE_CLUSTER_ID,
            .manuf_code = EZB_ZCL_STD_MANUF_CODE,
            .fc = {
                .manuf_specific  = 0,
                .direction       = EZB_ZCL_CMD_DIRECTION_TO_SRV,
                .dis_default_rsp = 1,
            },
        },
        .cmd_id      = cmd_id,
        .data_length = sizeof(value),
        .data        = &value,
    };

    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_err_t err = ezb_zcl_custom_cluster_cmd_req(&cmd);
    esp_zigbee_lock_release();

    if (err != EZB_ERR_NONE) {
        ESP_LOGE(TAG, "gesture cmd 0x%02X send failed: 0x%04X", cmd_id, err);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "gesture cmd 0x%02X sent (value=%u)", cmd_id, value);
    return ESP_OK;
}
