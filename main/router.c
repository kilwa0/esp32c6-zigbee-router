#include <stdio.h>
#include "esp_check.h"
#include "esp_err.h"
#include "esp_ieee802154.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "alarm_timer.h"
#include "led_strip.h"
#include "led_strip_types.h"
#include "led_strip_rmt.h"
#include "driver/gpio.h"
#include "esp_zigbee.h"
#include "ezbee/secur.h"
#include "ezbee/af.h"
#include "ezbee/zcl/cluster/basic_desc.h"
#include "ezbee/zcl/cluster/identify_desc.h"
#include "ezbee/zcl/cluster/on_off_desc.h"
#include "ezbee/zcl/cluster/level_desc.h"
#include "ezbee/zcl/cluster/color_control_desc.h"
#include "ezbee/zcl/cluster/custom.h"
#include "ezbee/zcl/zcl_common.h"
#include "ezbee/zdo/zdo_dev_srv_disc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "router.h"
#include "button.h"

static const char *TAG = "ROUTER ESP32C6";

#define LED_ESTADO_GPIO 8

/* Colores del LED de estado (R, G, B) -- valores 0-255.
 *
 * Semantica operacional:
 *   RED    -> error / fuera de red
 *   AMBER  -> buscando red / join en progreso
 *   GREEN  -> unido y operativo
 *   BLUE   -> ventana permit-join abierta
 */
#define LED_R_RED    64
#define LED_G_RED     0
#define LED_B_RED     0

#define LED_R_GREEN   0
#define LED_G_GREEN  16
#define LED_B_GREEN   0

#define LED_R_BLUE    0
#define LED_G_BLUE    0
#define LED_B_BLUE   16

#define LED_R_AMBER  30
#define LED_G_AMBER   7
#define LED_B_AMBER   0

#define SET_LED_IF_AWAKE(r, g, b) \
    do { if (!button_is_night_mode()) { set_led((r), (g), (b)); } } while (0)

static led_strip_handle_t led_strip;

static const uint8_t s_tc_link_key[ESP_ZIGBEE_TC_LINK_KEY_LEN] = {
    0x5a, 0x69, 0x67, 0x42, 0x65, 0x65, 0x41, 0x6c,
    0x6c, 0x69, 0x61, 0x6e, 0x63, 0x65, 0x30, 0x39
};
_Static_assert(sizeof(s_tc_link_key) == ESP_ZIGBEE_TC_LINK_KEY_LEN,
               "TC link key length mismatch");

static _Atomic bool steering_retry_pending;
static _Atomic bool retry_with_initialization = false;

void set_led(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
}

void set_led_locked(uint8_t r, uint8_t g, uint8_t b)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    set_led(r, g, b);
    esp_zigbee_lock_release();
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Configurando LED strip (RMT, GPIO %d)", LED_ESTADO_GPIO);
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_ESTADO_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

/* =========================================================================
 * ZCL On/Off cluster -- command -> gesture mapping
 *
 *   Off    (0x00)  -> night mode OFF  (LED visible)
 *   On     (0x01)  -> night mode ON   (LED silenced)
 *   Toggle (0x02)  -> permit-join toggle
 *
 * Rationale: On/Off is the most universal ZCL cluster; every coordinator
 * UI exposes it.  Mapping On->silence / Off->restore is intuitive: "turn
 * off" the LED noise at night, "turn on" to see status again.
 * ========================================================================= */
static esp_err_t onoff_action_handler(
        ezb_zcl_core_action_callback_id_t callback_id,
        void                             *message)
{
    if (callback_id != EZB_ZCL_SET_ATTR_VALUE_CB_ID) return ESP_OK;

    ezb_zcl_set_attr_value_message_t *msg =
        (ezb_zcl_set_attr_value_message_t *)message;

    /* Only care about On/Off cluster, attribute 0x0000 (OnOff bool) */
    if (msg->info.cluster != EZB_ZCL_CLUSTER_ID_ON_OFF) return ESP_OK;
    if (msg->attribute.id  != EZB_ZCL_ATTR_ON_OFF_ON_OFF_ID) return ESP_OK;

    bool on = *(bool *)msg->attribute.data.value;
    ESP_LOGI(TAG, "On/Off attr -> %s => %s",
             on ? "ON" : "OFF",
             on ? "night_mode ON" : "night_mode OFF");
    button_remote_trigger(BUTTON_ACTION_NIGHT_MODE);
    return ESP_OK;
}

/* =========================================================================
 * ZCL Level Control cluster -- command -> gesture mapping
 *
 *   MoveToLevel (any level value)  -> tx_toggle  (8 dBm <-> 20 dBm)
 *
 * Rationale: "dim up" = boost TX; "dim down" = back to normal.
 * Any non-zero level triggers the toggle so the coordinator slider
 * behaves predictably regardless of current level value.
 * ========================================================================= */
static esp_err_t levelctrl_action_handler(
        ezb_zcl_core_action_callback_id_t callback_id,
        void                             *message)
{
    if (callback_id != EZB_ZCL_LEVEL_CONTROL_MOVE_TO_LEVEL_CB_ID) return ESP_OK;

    ezb_zcl_level_move_to_level_message_t *msg =
        (ezb_zcl_level_move_to_level_message_t *)message;
    (void)msg;

    ESP_LOGI(TAG, "Level MoveToLevel => tx_toggle");
    button_remote_trigger(BUTTON_ACTION_TX_TOGGLE);
    return ESP_OK;
}

/* =========================================================================
 * ZCL custom cluster 0xFC00 -- direct gesture commands (unchanged)
 * ========================================================================= */
static ezb_zcl_status_t gesture_process_cmd(
        const ezb_zcl_cmd_hdr_t *hdr,
        const uint8_t           *payload,
        uint16_t                 payload_len)
{
    (void)payload;
    (void)payload_len;

    if (EZB_ZCL_CMD_FC_IS_TO_CLI_DIRECTION(hdr->fc)) {
        return EZB_ZCL_STATUS_INVALID_FIELD;
    }

    ESP_LOGI(TAG, "Gesture cmd 0x%02x from 0x%04x ep%u",
             hdr->cmd_id, hdr->src_addr.u.short_addr, hdr->src_ep);

    esp_err_t err = button_remote_trigger((button_action_t)hdr->cmd_id);
    if (err == ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "Unknown gesture cmd 0x%02x -- UNSUP_CMD", hdr->cmd_id);
        return EZB_ZCL_STATUS_UNSUP_CMD;
    }
    return EZB_ZCL_STATUS_SUCCESS;
}

static uint8_t gesture_disc_cmd(bool is_recv, uint8_t **list)
{
    static uint8_t recv_cmds[] = {
        ROUTER_CMD_NIGHT_MODE_TOGGLE,
        ROUTER_CMD_PERMIT_JOIN,
        ROUTER_CMD_TX_TOGGLE,
        ROUTER_CMD_FACTORY_RESET,
    };
    static uint8_t send_cmds[] = {};
    if (is_recv) { *list = recv_cmds; return sizeof(recv_cmds); }
    *list = send_cmds; return 0;
}

static void gesture_cluster_init(uint8_t ep_id)
{
    (void)ep_id;
    ezb_zcl_custom_cluster_handlers_t handlers = {
        .cluster_id     = ROUTER_GESTURE_CLUSTER_ID,
        .cluster_role   = EZB_ZCL_CLUSTER_SERVER,
        .process_cmd_cb = gesture_process_cmd,
        .check_value_cb = NULL,
        .write_attr_cb  = NULL,
        .cmd_disc_cb    = gesture_disc_cmd,
    };
    ezb_zcl_custom_cluster_handlers_register(&handlers);
    ESP_LOGI(TAG, "Gesture cluster 0x%04x ready on ep%u",
             ROUTER_GESTURE_CLUSTER_ID, ep_id);
}

static void gesture_cluster_deinit(uint8_t ep_id) { (void)ep_id; }

/* =========================================================================
 * ZCL core action handler -- routes On/Off, Level, Default Response
 * ========================================================================= */
static void zcl_core_action_handler(
        ezb_zcl_core_action_callback_id_t callback_id,
        void                             *message)
{
    /* Delegate to per-cluster handlers; each returns ESP_OK if irrelevant. */
    onoff_action_handler(callback_id, message);
    levelctrl_action_handler(callback_id, message);

    if (callback_id == EZB_ZCL_CORE_DEFAULT_RSP_CB_ID) {
        ezb_zcl_cmd_default_rsp_message_t *rsp =
            (ezb_zcl_cmd_default_rsp_message_t *)message;
        ESP_LOGI(TAG, "ZCL Default Response: status=0x%02x", rsp->in.status_code);
    }
}

/* =========================================================================
 * Endpoint registration
 *
 * Device type: 0x010C  Color Temperature Light
 *   - Universally recognised by ZHA, zigbee2mqtt, and Tuya gateways.
 *   - Generates a native light control UI with On/Off toggle and
 *     brightness/colour-temp sliders -- we repurpose those controls
 *     to drive the router gestures (see handler comments above).
 *
 * Cluster layout (server side):
 *   0x0000  Basic          -- mandatory, exposes manufacturer/model/fw
 *   0x0003  Identify       -- mandatory per ZCL HA spec
 *   0x0006  On/Off         -- UI toggle; maps to night-mode & permit-join
 *   0x0008  Level Control  -- UI slider; maps to tx_toggle
 *   0x0300  Color Control  -- required by 0x010C device type
 *   0xFC00  Gesture        -- manufacturer-specific direct control
 * ========================================================================= */
static esp_err_t register_router_endpoint(void)
{
    esp_err_t ret = ESP_OK;

    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    if (!dev_desc) return ESP_ERR_NO_MEM;

    ezb_af_ep_config_t ep_config = {
        .ep_id              = ESP_ZIGBEE_RANGE_EXTENDER_EP_ID,
        .app_profile_id     = EZB_AF_HA_PROFILE_ID,
        .app_device_id      = EZB_AF_HA_COLOR_TEMPERATURE_LIGHT_DEVICE_ID,
        .app_device_version = 0,
    };
    ezb_af_ep_desc_t ep_desc = ezb_af_create_endpoint_desc(&ep_config);
    if (!ep_desc) { ret = ESP_ERR_NO_MEM; goto cleanup_dev; }

    /* --- Basic cluster (server) --- */
    {
        ezb_zcl_basic_cluster_server_config_t cfg = {
            .zcl_version  = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
            .power_source = EZB_ZCL_BASIC_POWER_SOURCE_SINGLE_PHASE_MAINS,
        };
        ezb_zcl_cluster_desc_t d = ezb_zcl_basic_create_cluster_desc(
                &cfg, EZB_ZCL_CLUSTER_SERVER);
        if (!d) { ret = ESP_ERR_NO_MEM; goto cleanup_ep; }
        ezb_zcl_basic_cluster_desc_add_attr(d, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                (const void *)ESP_MANUFACTURER_NAME);
        ezb_zcl_basic_cluster_desc_add_attr(d, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                (const void *)ESP_MODEL_IDENTIFIER);
        ezb_zcl_basic_cluster_desc_add_attr(d, EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID,
                (const void *)ESP_SW_BUILD_ID);
        ret = ezb_af_endpoint_add_cluster_desc(ep_desc, d);
        if (ret != ESP_OK) goto cleanup_ep;
    }

    /* --- Identify cluster (server) --- */
    {
        ezb_zcl_identify_cluster_server_config_t cfg = {
            .identify_time = EZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,
        };
        ezb_zcl_cluster_desc_t d = ezb_zcl_identify_create_cluster_desc(
                &cfg, EZB_ZCL_CLUSTER_SERVER);
        if (!d) { ret = ESP_ERR_NO_MEM; goto cleanup_ep; }
        ret = ezb_af_endpoint_add_cluster_desc(ep_desc, d);
        if (ret != ESP_OK) goto cleanup_ep;
    }

    /* --- On/Off cluster (server)
     *     On  -> night_mode ON  (LED silenced)
     *     Off -> night_mode OFF (LED visible)
     *     Toggle -> permit_join toggle
     * --- */
    {
        ezb_zcl_on_off_cluster_server_config_t cfg = {
            .on_off = false,   /* initial state: LED visible (not silenced) */
        };
        ezb_zcl_cluster_desc_t d = ezb_zcl_on_off_create_cluster_desc(
                &cfg, EZB_ZCL_CLUSTER_SERVER);
        if (!d) { ret = ESP_ERR_NO_MEM; goto cleanup_ep; }
        ret = ezb_af_endpoint_add_cluster_desc(ep_desc, d);
        if (ret != ESP_OK) goto cleanup_ep;
    }

    /* --- Level Control cluster (server)
     *     MoveToLevel (any value) -> tx_toggle (8 dBm <-> 20 dBm)
     * --- */
    {
        ezb_zcl_level_cluster_server_config_t cfg = {
            .current_level    = 127,
            .remaining_time   = 0,
            .min_level        = 0,
            .max_level        = 254,
            .on_level         = 127,
            .on_off_trans_time = 0,
        };
        ezb_zcl_cluster_desc_t d = ezb_zcl_level_create_cluster_desc(
                &cfg, EZB_ZCL_CLUSTER_SERVER);
        if (!d) { ret = ESP_ERR_NO_MEM; goto cleanup_ep; }
        ret = ezb_af_endpoint_add_cluster_desc(ep_desc, d);
        if (ret != ESP_OK) goto cleanup_ep;
    }

    /* --- Color Control cluster (server)
     *     Required by HA device type 0x010C.
     *     MoveToColorTemperature -> tx_toggle (mirrors level slider)
     * --- */
    {
        ezb_zcl_color_control_cluster_server_config_t cfg = {
            .color_mode             = EZB_ZCL_COLOR_CONTROL_COLOR_MODE_COLOR_TEMPERATURE,
            .color_temperature_mireds = 370,   /* ~2700 K warm white */
            .color_temp_physical_min  = 153,   /* 6500 K cool white  */
            .color_temp_physical_max  = 500,   /* 2000 K warm white  */
            .options                  = 0,
            .num_primaries            = 0,
        };
        ezb_zcl_cluster_desc_t d = ezb_zcl_color_control_create_cluster_desc(
                &cfg, EZB_ZCL_CLUSTER_SERVER);
        if (!d) { ret = ESP_ERR_NO_MEM; goto cleanup_ep; }
        ret = ezb_af_endpoint_add_cluster_desc(ep_desc, d);
        if (ret != ESP_OK) goto cleanup_ep;
    }

    /* --- Gesture control cluster 0xFC00 (server, unchanged) --- */
    {
        ezb_zcl_custom_cluster_config_t cfg = {
            .cluster_id  = ROUTER_GESTURE_CLUSTER_ID,
            .init_func   = gesture_cluster_init,
            .deinit_func = gesture_cluster_deinit,
        };
        ezb_zcl_cluster_desc_t d = ezb_zcl_custom_create_cluster_desc(
                &cfg, EZB_ZCL_CLUSTER_SERVER);
        if (!d) { ret = ESP_ERR_NO_MEM; goto cleanup_ep; }
        ret = ezb_af_endpoint_add_cluster_desc(ep_desc, d);
        if (ret != ESP_OK) goto cleanup_ep;
    }

    ret = ezb_af_device_add_endpoint_desc(dev_desc, ep_desc);
    if (ret != ESP_OK) goto cleanup_dev;

    ret = ezb_af_device_desc_register(dev_desc);
    if (ret != ESP_OK) goto cleanup_dev;

    ezb_zcl_core_action_handler_register(zcl_core_action_handler);

    ESP_LOGI(TAG, "Endpoint registrado: ep=%u profile=0x%04x device=0x%04x (Color Temp Light) fw=%s",
             ESP_ZIGBEE_RANGE_EXTENDER_EP_ID, EZB_AF_HA_PROFILE_ID,
             EZB_AF_HA_COLOR_TEMPERATURE_LIGHT_DEVICE_ID, ESP_SW_BUILD_ID + 1);
    ESP_LOGI(TAG, "  On/Off ON   -> night mode ON  (LED silenced)");
    ESP_LOGI(TAG, "  On/Off OFF  -> night mode OFF (LED visible)");
    ESP_LOGI(TAG, "  On/Off Toggle -> permit-join toggle");
    ESP_LOGI(TAG, "  Level slider  -> TX power toggle (8 <-> 20 dBm)");
    ESP_LOGI(TAG, "  Cluster 0xFC00 cmd 0x01-0x04 -> direct gesture");
    return ESP_OK;

cleanup_ep:
    ezb_af_free_endpoint_desc(ep_desc);
cleanup_dev:
    ezb_af_free_device_desc(dev_desc);
    return ret;
}

static const char *bdb_status_to_str(ezb_bdb_comm_status_t status)
{
    switch (status) {
    case EZB_BDB_STATUS_SUCCESS:                    return "SUCCESS";
    case EZB_BDB_STATUS_IN_PROGRESS:                return "IN_PROGRESS";
    case EZB_BDB_STATUS_NOT_AA_CAPABLE:             return "NOT_AA_CAPABLE";
    case EZB_BDB_STATUS_NO_NETWORK:                 return "NO_NETWORK";
    case EZB_BDB_STATUS_TARGET_FAILURE:             return "TARGET_FAILURE";
    case EZB_BDB_STATUS_FORMATION_FAILURE:          return "FORMATION_FAILURE";
    case EZB_BDB_STATUS_NO_IDENTIFY_QUERY_RESPONSE: return "NO_IDENTIFY_QUERY_RESPONSE";
    case EZB_BDB_STATUS_BINDING_TABLE_FULL:         return "BINDING_TABLE_FULL";
    case EZB_BDB_STATUS_NO_SCAN_RESPONSE:           return "NO_SCAN_RESPONSE";
    case EZB_BDB_STATUS_NOT_PERMITTED:              return "NOT_PERMITTED";
    case EZB_BDB_STATUS_TCLK_EX_FAILURE:            return "TCLK_EX_FAILURE";
    case EZB_BDB_STATUS_NOT_ON_A_NETWORK:           return "NOT_ON_A_NETWORK";
    case EZB_BDB_STATUS_ON_A_NETWORK:               return "ON_A_NETWORK";
    case EZB_BDB_STATUS_CANCELLED:                  return "CANCELLED";
    case EZB_BDB_STATUS_DEV_ANNCE_SEND_FAILURE:     return "DEV_ANNCE_SEND_FAILURE";
    default:                                         return "UNKNOWN";
    }
}

static void esp_zigbee_alarm_bdb_commissioning(alarm_timer_arg_t arg)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    (void)ezb_bdb_start_top_level_commissioning(arg);
    esp_zigbee_lock_release();
    steering_retry_pending = false;
}

static void send_device_announce(alarm_timer_arg_t arg)
{
    (void)arg;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_zdo_device_annce_req_t annce = {.cb = NULL, .user_ctx = NULL};
    ezb_zdo_device_annce_req(&annce);
    esp_zigbee_lock_release();
    ESP_LOGI(TAG, "Device Announce enviado");
}

static bool esp_zigbee_app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);

    switch (signal_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Inicializando Router Zigbee...");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        SET_LED_IF_AWAKE(LED_R_RED, LED_G_RED, LED_B_RED);
        break;

    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        ESP_LOGI(TAG, "Evento BDB: %s (%s / 0x%02x)",
                 signal_type == EZB_BDB_SIGNAL_DEVICE_FIRST_START ? "DEVICE_FIRST_START" : "DEVICE_REBOOT",
                 bdb_status_to_str(status), status);
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "BDB init OK. Factory new: %s", ezb_bdb_is_factory_new() ? "si" : "no");
            SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
            if (ezb_bdb_is_factory_new()) {
                ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "=== ROUTER RECONECTADO A LA RED ===");
                SET_LED_IF_AWAKE(LED_R_GREEN, LED_G_GREEN, LED_B_GREEN);
                ezb_zdo_device_annce_req_t annce = {.cb = NULL, .user_ctx = NULL};
                ezb_zdo_device_annce_req(&annce);
            }
        } else {
            ESP_LOGW(TAG, "BDB init fallo (%s / 0x%02x). Reintento en %ums...",
                     bdb_status_to_str(status), status, ROUTER_BDB_INIT_RETRY_MS);
            alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning,
                                 EZB_BDB_MODE_INITIALIZATION, ROUTER_BDB_INIT_RETRY_MS);
        }
    } break;

    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            steering_retry_pending = false;
            ESP_LOGI(TAG, "=== JOIN OK: PAN 0x%04hx, Canal %d, Addr 0x%04hx ===",
                     ezb_nwk_get_panid(), ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
            SET_LED_IF_AWAKE(LED_R_GREEN, LED_G_GREEN, LED_B_GREEN);
            alarm_timer_schedule(send_device_announce, 0, 3000);
            alarm_timer_schedule(send_device_announce, 0, 8000);
        } else {
            if (steering_retry_pending) break;
            ESP_LOGW(TAG, "Steering fallo (%s / 0x%02x). Reintento en %ums...",
                     bdb_status_to_str(status), status, ROUTER_STEERING_RETRY_MS);
            if (status == EZB_BDB_STATUS_NO_NETWORK) {
                SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
            } else if (status == EZB_BDB_STATUS_NOT_PERMITTED) {
                SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
            } else if (status == EZB_BDB_STATUS_TARGET_FAILURE) {
                SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
            }
            if (!steering_retry_pending) {
                alarm_timer_arg_t next_mode = EZB_BDB_MODE_NETWORK_STEERING;
                if (status == EZB_BDB_STATUS_NO_NETWORK && retry_with_initialization) {
                    next_mode = EZB_BDB_MODE_INITIALIZATION;
                }
                steering_retry_pending = true;
                alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning,
                                     next_mode, ROUTER_STEERING_RETRY_MS);
                if (status == EZB_BDB_STATUS_NO_NETWORK) {
                    retry_with_initialization = !retry_with_initialization;
                }
            }
        }
    } break;

    case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
        uint8_t duration = *(uint8_t *)ezb_app_signal_get_params(app_signal);
        if (duration > 0) {
            ESP_LOGI(TAG, "Permit join activo en PAN 0x%04hx durante %u s", ezb_nwk_get_panid(), duration);
            SET_LED_IF_AWAKE(LED_R_BLUE, LED_G_BLUE, LED_B_BLUE);
        } else {
            ESP_LOGW(TAG, "Permit join cerrado en PAN 0x%04hx", ezb_nwk_get_panid());
            SET_LED_IF_AWAKE(LED_R_GREEN, LED_G_GREEN, LED_B_GREEN);
        }
    } break;

    case EZB_ZDO_SIGNAL_LEAVE:
        ESP_LOGW(TAG, "Dispositivo salio de la red");
        SET_LED_IF_AWAKE(LED_R_RED, LED_G_RED, LED_B_RED);
        break;

    case EZB_ZDO_SIGNAL_LEAVE_INDICATION: {
        const ezb_zdo_signal_leave_indication_params_t *p = ezb_app_signal_get_params(app_signal);
        ESP_LOGW(TAG, "Nodo 0x%04hx abandono la red", p->short_addr);
    } break;

    default:
        ESP_LOGI(TAG, "ZB signal: %s (0x%02x)", ezb_app_signal_to_string(signal_type), signal_type);
        SET_LED_IF_AWAKE(LED_R_GREEN, LED_G_GREEN, LED_B_GREEN);
        break;
    }
    return true;
}

static void esp_zigbee_stack_main_task(void *pvParameters)
{
    esp_zigbee_config_t config = ESP_ZIGBEE_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_zigbee_init(&config));

    int8_t tx_power_dbm = ROUTER_TX_POWER_LOW_DBM;
    esp_err_t pw_err = esp_ieee802154_set_txpower(tx_power_dbm);
    if (pw_err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo fijar TX power a %d dBm: %s",
                 tx_power_dbm, esp_err_to_name(pw_err));
    } else {
        ESP_LOGI(TAG, "TX power fijado a %d dBm", tx_power_dbm);
    }

    ezb_aps_secur_enable_distributed_security(false);
    ezb_secur_set_global_link_key(s_tc_link_key);
    ESP_ERROR_CHECK(ezb_secur_set_tclk_exchange_required(true));
    ezb_nwk_set_min_join_lqi(0);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(ESP_ZIGBEE_PRIMARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(ESP_ZIGBEE_SECONDARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(esp_zigbee_app_signal_handler));
    ESP_ERROR_CHECK(register_router_endpoint());
    ESP_ERROR_CHECK(esp_zigbee_start(false));
    esp_zigbee_launch_mainloop();

    esp_zigbee_deinit();
    vTaskDelete(NULL);
}

void app_main(void)
{
    configure_led();
    ESP_ERROR_CHECK(button_init());

    ESP_LOGI(TAG, "app_main: antes de init");
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_flash_init_partition(ESP_ZIGBEE_STORAGE_PARTITION_NAME));
    ESP_LOGI(TAG, "Arrancando Router Zigbee Puro");

    BaseType_t ok = xTaskCreate(esp_zigbee_stack_main_task, "Zigbee_main",
                                ZIGBEE_MAIN_TASK_STACK_SIZE, NULL, 5, NULL);
    ESP_LOGI(TAG, "app_main: xTaskCreate=%ld (pdPASS=%ld)", (long)ok, (long)pdPASS);
}
