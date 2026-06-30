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
#include "ezbee/zcl/cluster/on_off.h"
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
 *
 * Colores de feedback de gestos (button.c):
 *   BRIGHT_RED (255,0,0)   -> TX boost 20 dBm activo
 *   SOFT_BLUE  (0,0,64)    -> TX normal 8 dBm activo
 *   MAGENTA    (255,0,255) -> accion destructiva en curso
 *
 * Night mode (button single-tap): todos los set_led() son silenciados
 * mientras button_is_night_mode() devuelva true.
 *
 * NOTA: Las macros LED_* se usan SOLO en comentarios y como referencia de
 * valores; NO se pasan como argumento a SET_LED_IF_AWAKE porque el
 * preprocesador de C no divide un argumento de macro en varios parametros
 * aunque se expanda a una lista separada por comas.  Los valores r,g,b se
 * escriben siempre de forma explicita en cada llamada. */
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

/* Helper macro: skips the LED update when night mode is active.
 * Only applies to Zigbee state transitions; button gesture feedback
 * in button.c bypasses this guard intentionally (the user triggered it). */
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

/* -------------------------------------------------------------------------
 * ZCL OnOff message handler
 *
 * The iHost sends On / Off / Toggle commands to one of the 4 OnOff server
 * endpoints. Each endpoint maps to a button gesture action:
 *
 *   ROUTER_EP_NIGHT  (1) -> do_night_mode_toggle()
 *   ROUTER_EP_JOIN   (2) -> do_permit_join()
 *   ROUTER_EP_TX     (3) -> do_tx_toggle()
 *   ROUTER_EP_RESET  (4) -> do_factory_reset()
 *
 * The handler is registered via ezb_zcl_on_off_register_message_handler().
 * It is called in the Zigbee stack task context -- no lock needed.
 *
 * The actual On/Off attribute state is ignored: any command (On, Off, Toggle)
 * triggers the action. Node-RED or the iHost sends Toggle to trigger a
 * gesture; the attribute state has no physical meaning here.
 *
 * API verified against:
 *   ezbee/zcl/cluster/on_off.h
 *   ezbee/zcl/cluster/on_off_desc.h
 * ---------------------------------------------------------------------- */
static void on_off_message_handler(ezb_zcl_on_off_message_t *msg)
{
    if (!msg) return;
    uint8_t ep = msg->info.src_ep;  /* endpoint the command was sent TO */

    switch (ep) {
    case ROUTER_EP_NIGHT:
        ESP_LOGI(TAG, "OnOff ep=%u -> night mode toggle", ep);
        do_night_mode_toggle();
        break;
    case ROUTER_EP_JOIN:
        ESP_LOGI(TAG, "OnOff ep=%u -> permit join", ep);
        do_permit_join();
        break;
    case ROUTER_EP_TX:
        ESP_LOGI(TAG, "OnOff ep=%u -> TX toggle", ep);
        do_tx_toggle();
        break;
    case ROUTER_EP_RESET:
        ESP_LOGI(TAG, "OnOff ep=%u -> factory reset", ep);
        do_factory_reset();
        break;
    default:
        ESP_LOGW(TAG, "OnOff ep=%u unknown, ignored", ep);
        break;
    }

    msg->out.result = EZB_ZCL_STATUS_SUCCESS;
}

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

/* -------------------------------------------------------------------------
 * Helper: create one OnOff server endpoint and add it to dev_desc.
 *
 * ep_id        : endpoint number (ROUTER_EP_NIGHT .. ROUTER_EP_RESET)
 * add_basic    : true only for ep 1 -- Basic cluster carries device identity
 *
 * Verified API:
 *   ezb_zcl_on_off_cluster_server_config_t  { bool on_off }
 *   ezb_zcl_on_off_create_cluster_desc(cfg, EZB_ZCL_CLUSTER_SERVER)
 *   ezb_zcl_basic_create_cluster_desc / ezb_zcl_basic_cluster_desc_add_attr
 *   ezb_af_create_endpoint_desc / ezb_af_endpoint_add_cluster_desc
 *   ezb_af_device_add_endpoint_desc
 * ---------------------------------------------------------------------- */
static esp_err_t add_onoff_endpoint(ezb_af_device_desc_t dev_desc,
                                    uint8_t ep_id,
                                    bool    add_basic)
{
    esp_err_t ret = ESP_OK;

    ezb_af_ep_config_t ep_cfg = {
        .ep_id              = ep_id,
        .app_profile_id     = EZB_AF_HA_PROFILE_ID,
        .app_device_id      = ROUTER_HA_DEVICE_ID_ON_OFF_LIGHT,
        .app_device_version = 0,
    };
    ezb_af_ep_desc_t ep_desc = ezb_af_create_endpoint_desc(&ep_cfg);
    if (!ep_desc) {
        ESP_LOGE(TAG, "ep %u: no se pudo crear ep_desc", ep_id);
        return ESP_ERR_NO_MEM;
    }

    /* Basic cluster -- only on ep 1 */
    if (add_basic) {
        ezb_zcl_basic_cluster_server_config_t basic_cfg = {
            .zcl_version  = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
            .power_source = EZB_ZCL_BASIC_POWER_SOURCE_SINGLE_PHASE_MAINS,
        };
        ezb_zcl_cluster_desc_t basic_desc =
            ezb_zcl_basic_create_cluster_desc(&basic_cfg, EZB_ZCL_CLUSTER_SERVER);
        if (!basic_desc) {
            ESP_LOGE(TAG, "ep %u: no se pudo crear Basic cluster", ep_id);
            ret = ESP_ERR_NO_MEM;
            goto cleanup_ep;
        }

        struct {
            uint16_t attr_id;
            const void *value;
        } basic_attrs[] = {
            { EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, ESP_MANUFACTURER_NAME },
            { EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,  ESP_MODEL_IDENTIFIER  },
            { EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID,        ESP_SW_BUILD_ID       },
        };
        for (size_t i = 0; i < sizeof(basic_attrs)/sizeof(basic_attrs[0]); i++) {
            ret = ezb_zcl_basic_cluster_desc_add_attr(
                    basic_desc, basic_attrs[i].attr_id, basic_attrs[i].value);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "ep %u: Basic attr 0x%04x error: %s",
                         ep_id, basic_attrs[i].attr_id, esp_err_to_name(ret));
                goto cleanup_ep;
            }
        }

        ret = ezb_af_endpoint_add_cluster_desc(ep_desc, basic_desc);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ep %u: no se pudo anadir Basic: %s",
                     ep_id, esp_err_to_name(ret));
            goto cleanup_ep;
        }
    }

    /* OnOff cluster (server) */
    ezb_zcl_on_off_cluster_server_config_t onoff_cfg = {
        .on_off = false,   /* initial state: off */
    };
    ezb_zcl_cluster_desc_t onoff_desc =
        ezb_zcl_on_off_create_cluster_desc(&onoff_cfg, EZB_ZCL_CLUSTER_SERVER);
    if (!onoff_desc) {
        ESP_LOGE(TAG, "ep %u: no se pudo crear OnOff cluster", ep_id);
        ret = ESP_ERR_NO_MEM;
        goto cleanup_ep;
    }

    ret = ezb_af_endpoint_add_cluster_desc(ep_desc, onoff_desc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ep %u: no se pudo anadir OnOff: %s",
                 ep_id, esp_err_to_name(ret));
        goto cleanup_ep;
    }

    ret = ezb_af_device_add_endpoint_desc(dev_desc, ep_desc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ep %u: no se pudo anadir al device_desc: %s",
                 ep_id, esp_err_to_name(ret));
        goto cleanup_ep;
    }

    ESP_LOGI(TAG, "ep %u registrado: profile=0x%04x device=0x%04x "
                  "clusters=[%sOnOff(server)]",
             ep_id, EZB_AF_HA_PROFILE_ID, ROUTER_HA_DEVICE_ID_ON_OFF_LIGHT,
             add_basic ? "Basic(server) " : "");
    return ESP_OK;

cleanup_ep:
    ezb_af_free_endpoint_desc(ep_desc);
    return ret;
}

/**
 * @brief Registra los 4 endpoints OnOff server y el handler de mensajes.
 *
 * Endpoints:
 *   ep 1 (ROUTER_EP_NIGHT)  Basic+OnOff  -> do_night_mode_toggle()
 *   ep 2 (ROUTER_EP_JOIN)   OnOff        -> do_permit_join()
 *   ep 3 (ROUTER_EP_TX)     OnOff        -> do_tx_toggle()
 *   ep 4 (ROUTER_EP_RESET)  OnOff        -> do_factory_reset()
 *
 * El mensaje handler se registra una sola vez; despacha por ep_id.
 */
static esp_err_t register_router_endpoints(void)
{
    esp_err_t ret = ESP_OK;

    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    if (!dev_desc) {
        ESP_LOGE(TAG, "No se pudo crear device_desc");
        return ESP_ERR_NO_MEM;
    }

    /* ep 1: Basic + OnOff */
    ret = add_onoff_endpoint(dev_desc, ROUTER_EP_NIGHT, true);
    if (ret != ESP_OK) goto cleanup;

    /* ep 2-4: OnOff only */
    ret = add_onoff_endpoint(dev_desc, ROUTER_EP_JOIN,  false);
    if (ret != ESP_OK) goto cleanup;

    ret = add_onoff_endpoint(dev_desc, ROUTER_EP_TX,    false);
    if (ret != ESP_OK) goto cleanup;

    ret = add_onoff_endpoint(dev_desc, ROUTER_EP_RESET, false);
    if (ret != ESP_OK) goto cleanup;

    ret = ezb_af_device_desc_register(dev_desc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo registrar device_desc: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    /* Register OnOff message handler -- single handler for all 4 endpoints */
    ezb_zcl_on_off_register_message_handler(on_off_message_handler);

    ESP_LOGI(TAG, "4 endpoints OnOff registrados (eps 1-4), fw=%s",
             ESP_SW_BUILD_ID + 1);
    return ESP_OK;

cleanup:
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
        ESP_LOGI(TAG, "Inicializando Router Zigbee Puro...");
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
                ESP_LOGI(TAG, "Device Announce enviado (rejoin)");
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
            if (steering_retry_pending) {
                break;
            }
            ESP_LOGW(TAG, "Steering fallo (%s / 0x%02x). Reintento en %ums...",
                     bdb_status_to_str(status), status, ROUTER_STEERING_RETRY_MS);
            if (status == EZB_BDB_STATUS_NO_NETWORK) {
                ESP_LOGW(TAG, "No se encontro red Zigbee: verificar coordinador en modo emparejamiento y dentro de rango");
                SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
            } else if (status == EZB_BDB_STATUS_NOT_PERMITTED) {
                ESP_LOGW(TAG, "Join no permitido por el coordinador (permit join cerrado)");
                SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
            } else if (status == EZB_BDB_STATUS_TARGET_FAILURE) {
                ESP_LOGW(TAG, "Fallo de join: el coordinador rechazo la solicitud de join");
                SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
            } else if (status == EZB_BDB_STATUS_TCLK_EX_FAILURE) {
                ESP_LOGW(TAG, "Fallo intercambio de Trust Center Link Key");
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

    /* TX power: arrancar a LOW (8 dBm), potencia de trabajo normal.
     * Triple-tap del boton BOOT alterna entre LOW (8) y HIGH (20) dBm. */
    int8_t tx_power_dbm = ROUTER_TX_POWER_LOW_DBM;
    esp_err_t pw_err = esp_ieee802154_set_txpower(tx_power_dbm);
    if (pw_err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo fijar TX power a %d dBm: %s",
                 tx_power_dbm, esp_err_to_name(pw_err));
    } else {
        ESP_LOGI(TAG, "TX power fijado a %d dBm (normal de trabajo)", tx_power_dbm);
    }

    ezb_aps_secur_enable_distributed_security(false);
    ezb_secur_set_global_link_key(s_tc_link_key);
    ESP_ERROR_CHECK(ezb_secur_set_tclk_exchange_required(true));
    ESP_LOGI(TAG, "Canales: primary=0x%08lx secondary=0x%08lx",
             (unsigned long)ESP_ZIGBEE_PRIMARY_CHANNEL_MASK,
             (unsigned long)ESP_ZIGBEE_SECONDARY_CHANNEL_MASK);
    ezb_nwk_set_min_join_lqi(0);
    ESP_ERROR_CHECK(ezb_bdb_set_primary_channel_set(ESP_ZIGBEE_PRIMARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_bdb_set_secondary_channel_set(ESP_ZIGBEE_SECONDARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(ezb_app_signal_add_handler(esp_zigbee_app_signal_handler));
    ESP_ERROR_CHECK(register_router_endpoints());

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
    ESP_LOGI(TAG, "Arrancando Router Zigbee Puro -- fw=%s", ESP_SW_BUILD_ID + 1);

    BaseType_t ok = xTaskCreate(esp_zigbee_stack_main_task, "Zigbee_main",
                                ZIGBEE_MAIN_TASK_STACK_SIZE, NULL, 5, NULL);
    ESP_LOGI(TAG, "app_main: xTaskCreate=%ld (pdPASS=%ld)", (long)ok, (long)pdPASS);
}
