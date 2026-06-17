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

static _Atomic bool steering_retry_pending = false;
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

static esp_err_t register_router_endpoint(void)
{
    /* Basic cluster attributes */
    esp_zb_basic_cluster_cfg_t basic_cfg = {
        .zcl_version   = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source  = ESP_ZB_ZCL_BASIC_POWER_SOURCE_SINGLE_PHASE,
    };

    esp_zb_attribute_list_t *basic_attrs = esp_zb_basic_cluster_create(&basic_cfg);
    if (!basic_attrs) {
        ESP_LOGE(TAG, "esp_zb_basic_cluster_create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(
        esp_zb_basic_cluster_add_attr(basic_attrs,
            ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
            (void *)ESP_MANUFACTURER_NAME),
        TAG, "add ManufacturerName failed");

    ESP_RETURN_ON_ERROR(
        esp_zb_basic_cluster_add_attr(basic_attrs,
            ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
            (void *)ESP_MODEL_IDENTIFIER),
        TAG, "add ModelIdentifier failed");

    ESP_RETURN_ON_ERROR(
        esp_zb_basic_cluster_add_attr(basic_attrs,
            ESP_ZB_ZCL_ATTR_BASIC_SW_BUILD_ID,
            (void *)ESP_SW_BUILD_ID),
        TAG, "add SWBuildID failed");

    /* Cluster list: server-side Basic cluster only (router has no client clusters) */
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    if (!cluster_list) {
        ESP_LOGE(TAG, "esp_zb_zcl_cluster_list_create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(
        esp_zb_cluster_list_add_basic_cluster(cluster_list,
            basic_attrs, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE),
        TAG, "add Basic cluster to list failed");

    /* Endpoint descriptor */
    esp_zb_endpoint_config_t ep_cfg = {
        .endpoint       = ESP_ZIGBEE_RANGE_EXTENDER_EP_ID,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id  = ESP_ZB_HA_RANGE_EXTENDER_DEVICE_ID,
        .app_device_version = 0,
    };

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    if (!ep_list) {
        ESP_LOGE(TAG, "esp_zb_ep_list_create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(
        esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_cfg),
        TAG, "esp_zb_ep_list_add_ep failed");

    ESP_RETURN_ON_ERROR(
        esp_zb_device_register(ep_list),
        TAG, "esp_zb_device_register failed");

    ESP_LOGI(TAG, "Endpoint registrado: ep=%u profile=0x%04x device=0x%04x (fw=%s)",
             ESP_ZIGBEE_RANGE_EXTENDER_EP_ID,
             ESP_ZB_AF_HA_PROFILE_ID,
             ESP_ZB_HA_RANGE_EXTENDER_DEVICE_ID,
             ESP_SW_BUILD_ID + 1);  /* +1 skips ZCL length byte */
    return ESP_OK;
}

static const char *bdb_status_to_str(esp_zb_bdb_commissioning_status_t status)
{
    switch (status) {
    case ESP_ZB_BDB_STATUS_SUCCESS:                    return "SUCCESS";
    case ESP_ZB_BDB_STATUS_IN_PROGRESS:                return "IN_PROGRESS";
    case ESP_ZB_BDB_STATUS_NOT_AA_CAPABLE:             return "NOT_AA_CAPABLE";
    case ESP_ZB_BDB_STATUS_NO_NETWORK:                 return "NO_NETWORK";
    case ESP_ZB_BDB_STATUS_TARGET_FAILURE:             return "TARGET_FAILURE";
    case ESP_ZB_BDB_STATUS_FORMATION_FAILURE:          return "FORMATION_FAILURE";
    case ESP_ZB_BDB_STATUS_NO_IDENTIFY_QUERY_RESPONSE: return "NO_IDENTIFY_QUERY_RESPONSE";
    case ESP_ZB_BDB_STATUS_BINDING_TABLE_FULL:         return "BINDING_TABLE_FULL";
    case ESP_ZB_BDB_STATUS_NO_SCAN_RESPONSE:           return "NO_SCAN_RESPONSE";
    case ESP_ZB_BDB_STATUS_NOT_PERMITTED:              return "NOT_PERMITTED";
    case ESP_ZB_BDB_STATUS_TCLK_EX_FAILURE:            return "TCLK_EX_FAILURE";
    case ESP_ZB_BDB_STATUS_NOT_ON_A_NETWORK:           return "NOT_ON_A_NETWORK";
    case ESP_ZB_BDB_STATUS_ON_A_NETWORK:               return "ON_A_NETWORK";
    case ESP_ZB_BDB_STATUS_CANCELLED:                  return "CANCELLED";
    case ESP_ZB_BDB_STATUS_DEV_ANNCE_SEND_FAILURE:     return "DEV_ANNCE_SEND_FAILURE";
    default:                                            return "UNKNOWN";
    }
}

static void esp_zigbee_alarm_bdb_commissioning(alarm_timer_arg_t arg)
{
    esp_zigbee_lock_acquire(portMAX_DELAY);
    esp_zb_bdb_start_top_level_commissioning((esp_zb_bdb_commissioning_mode_mask_t)(uintptr_t)arg);
    esp_zigbee_lock_release();
    steering_retry_pending = false;
}

static void send_device_announce(alarm_timer_arg_t arg)
{
    (void)arg;
    esp_zigbee_lock_acquire(portMAX_DELAY);
    esp_zb_zdo_device_annce(esp_zb_get_short_address());
    esp_zigbee_lock_release();
    ESP_LOGI(TAG, "Device Announce enviado");
}

/*
 * esp_zb_app_signal_handler is the mandatory entry point called by the
 * Zigbee stack for every application-level signal.  The symbol must be
 * defined by the application -- the SDK declares it as a weak symbol and
 * calls it directly; no registration function is needed.
 */
void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t  err   = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t signal_type =
        (esp_zb_app_signal_type_t)(*p_sg_p);

    switch (signal_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Inicializando Router Zigbee Puro...");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        SET_LED_IF_AWAKE(LED_R_RED, LED_G_RED, LED_B_RED);
        break;

    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT: {
        esp_zb_bdb_commissioning_status_t status =
            (esp_zb_bdb_commissioning_status_t)
                (*(int *)esp_zb_app_signal_get_params(p_sg_p));
        ESP_LOGI(TAG, "Evento BDB: %s (%s / 0x%02x)",
                 signal_type == ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START
                     ? "DEVICE_FIRST_START" : "DEVICE_REBOOT",
                 bdb_status_to_str(status), status);
        if (err == ESP_OK && status == ESP_ZB_BDB_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "BDB init OK. Factory new: %s",
                     esp_zb_bdb_is_factory_new() ? "si" : "no");
            SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
            if (esp_zb_bdb_is_factory_new()) {
                esp_zb_bdb_start_top_level_commissioning(
                    ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "=== ROUTER RECONECTADO A LA RED ===");
                SET_LED_IF_AWAKE(LED_R_GREEN, LED_G_GREEN, LED_B_GREEN);
                esp_zb_zdo_device_annce(esp_zb_get_short_address());
                ESP_LOGI(TAG, "Device Announce enviado (rejoin)");
            }
        } else {
            ESP_LOGW(TAG, "BDB init fallo (%s / 0x%02x). Reintento en %ums...",
                     bdb_status_to_str(status), status,
                     ROUTER_BDB_INIT_RETRY_MS);
            alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning,
                                 (alarm_timer_arg_t)(uintptr_t)ESP_ZB_BDB_MODE_INITIALIZATION,
                                 ROUTER_BDB_INIT_RETRY_MS);
        }
    } break;

    case ESP_ZB_BDB_SIGNAL_STEERING: {
        esp_zb_bdb_commissioning_status_t status =
            (esp_zb_bdb_commissioning_status_t)
                (*(int *)esp_zb_app_signal_get_params(p_sg_p));
        if (err == ESP_OK && status == ESP_ZB_BDB_STATUS_SUCCESS) {
            steering_retry_pending = false;
            ESP_LOGI(TAG,
                     "=== JOIN OK: PAN 0x%04hx, Canal %d, Addr 0x%04hx ===",
                     esp_zb_get_pan_id(),
                     esp_zb_get_current_channel(),
                     esp_zb_get_short_address());
            SET_LED_IF_AWAKE(LED_R_GREEN, LED_G_GREEN, LED_B_GREEN);
            alarm_timer_schedule(send_device_announce, NULL, 3000);
            alarm_timer_schedule(send_device_announce, NULL, 8000);
        } else {
            if (steering_retry_pending) {
                break;
            }
            ESP_LOGW(TAG, "Steering fallo (%s / 0x%02x). Reintento en %ums...",
                     bdb_status_to_str(status), status,
                     ROUTER_STEERING_RETRY_MS);
            if (status == ESP_ZB_BDB_STATUS_NO_NETWORK) {
                ESP_LOGW(TAG, "No se encontro red Zigbee: verificar coordinador "
                              "en modo emparejamiento y dentro de rango");
                SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
            } else if (status == ESP_ZB_BDB_STATUS_NOT_PERMITTED) {
                ESP_LOGW(TAG, "Join no permitido por el coordinador "
                              "(permit join cerrado)");
                SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
            } else if (status == ESP_ZB_BDB_STATUS_TARGET_FAILURE) {
                ESP_LOGW(TAG, "Fallo de join: coordinador rechazo la solicitud");
                SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
            } else if (status == ESP_ZB_BDB_STATUS_TCLK_EX_FAILURE) {
                ESP_LOGW(TAG, "Fallo intercambio de Trust Center Link Key");
            }
            esp_zb_bdb_commissioning_mode_mask_t next_mode =
                ESP_ZB_BDB_MODE_NETWORK_STEERING;
            if (status == ESP_ZB_BDB_STATUS_NO_NETWORK
                    && retry_with_initialization) {
                next_mode = ESP_ZB_BDB_MODE_INITIALIZATION;
            }
            steering_retry_pending = true;
            alarm_timer_schedule(
                esp_zigbee_alarm_bdb_commissioning,
                (alarm_timer_arg_t)(uintptr_t)next_mode,
                ROUTER_STEERING_RETRY_MS);
            if (status == ESP_ZB_BDB_STATUS_NO_NETWORK) {
                retry_with_initialization = !retry_with_initialization;
            }
        }
    } break;

    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
        uint8_t duration =
            *(uint8_t *)esp_zb_app_signal_get_params(p_sg_p);
        if (duration > 0) {
            ESP_LOGI(TAG,
                     "Permit join activo en PAN 0x%04hx durante %u s",
                     esp_zb_get_pan_id(), duration);
            SET_LED_IF_AWAKE(LED_R_BLUE, LED_G_BLUE, LED_B_BLUE);
        } else {
            ESP_LOGW(TAG, "Permit join cerrado en PAN 0x%04hx",
                     esp_zb_get_pan_id());
            SET_LED_IF_AWAKE(LED_R_GREEN, LED_G_GREEN, LED_B_GREEN);
        }
    } break;

    case ESP_ZB_ZDO_SIGNAL_LEAVE:
        ESP_LOGW(TAG, "Dispositivo salio de la red");
        SET_LED_IF_AWAKE(LED_R_RED, LED_G_RED, LED_B_RED);
        break;

    case ESP_ZB_ZDO_SIGNAL_LEAVE_INDICATION: {
        const esp_zb_zdo_signal_leave_indication_params_t *p =
            (const esp_zb_zdo_signal_leave_indication_params_t *)
                esp_zb_app_signal_get_params(p_sg_p);
        ESP_LOGW(TAG, "Nodo 0x%04hx abandono la red", p->short_addr);
    } break;

    default:
        ESP_LOGI(TAG, "ZB signal: %s (0x%02x)",
                 esp_zb_zdo_signal_to_string(signal_type), signal_type);
        SET_LED_IF_AWAKE(LED_R_GREEN, LED_G_GREEN, LED_B_GREEN);
        break;
    }
}

static void esp_zigbee_stack_main_task(void *pvParameters)
{
    esp_zb_cfg_t config = ESP_ZIGBEE_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(esp_zb_init(&config));

    /* TX power: arrancar a LOW (8 dBm), potencia de trabajo normal.
     * Triple-tap del boton BOOT alterna entre LOW (8) y HIGH (20) dBm. */
    int8_t tx_power_dbm = ROUTER_TX_POWER_LOW_DBM;
    esp_err_t pw_err = esp_ieee802154_set_txpower(tx_power_dbm);
    if (pw_err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo fijar TX power a %d dBm: %s",
                 tx_power_dbm, esp_err_to_name(pw_err));
    } else {
        ESP_LOGI(TAG, "TX power fijado a %d dBm (normal de trabajo)",
                 tx_power_dbm);
    }

    esp_zb_aps_secur_enable_distributed_security(false);
    esp_zb_secur_set_global_link_key((uint8_t *)s_tc_link_key);
    ESP_ERROR_CHECK(esp_zb_secur_set_tclk_exchange_required(true));
    ESP_LOGI(TAG, "Canales: primary=0x%08lx secondary=0x%08lx",
             (unsigned long)ESP_ZIGBEE_PRIMARY_CHANNEL_MASK,
             (unsigned long)ESP_ZIGBEE_SECONDARY_CHANNEL_MASK);
    esp_zb_nwk_set_min_join_lqi(0);
    ESP_ERROR_CHECK(
        esp_zb_set_primary_network_channel_set(ESP_ZIGBEE_PRIMARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(
        esp_zb_set_secondary_network_channel_set(ESP_ZIGBEE_SECONDARY_CHANNEL_MASK));
    ESP_ERROR_CHECK(register_router_endpoint());
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();

    esp_zb_deinit();
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
    ESP_LOGI(TAG, "app_main: xTaskCreate=%ld (pdPASS=%ld)",
             (long)ok, (long)pdPASS);
}
