#include <stdio.h>
#include "esp_check.h"
#include "esp_err.h"
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
#include "ezbee/zha.h"
#include "ezbee/zcl/cluster/basic_desc.h"
#include "ezbee/zcl/cluster/identify_desc.h"
#include "ezbee/zcl/cluster/on_off_desc.h"
#include "ezbee/zdo/zdo_dev_srv_disc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "router.h"

static const char *TAG = "ROUTER ESP32C6";

#define LED_ESTADO_GPIO 8

/* Colores del LED de estado (R, G, B) — valores 0-255 */
#define LED_RED     16,  0,  0
#define LED_GREEN    0, 16,  0
#define LED_BLUE     0,  0, 16
#define LED_BLUE_BDB 0,  0, 255
#define LED_WHITE   16, 16, 16

static led_strip_handle_t led_strip;

/* Accedidas desde alarm_timer callbacks (timer task) y signal handler
   (Zigbee main task): _Atomic garantiza visibilidad sin data race. */
static _Atomic bool steering_retry_pending;
static _Atomic bool retry_with_initialization;

static inline void set_led(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
}

static void configure_led(void)
{
    ESP_LOGI(TAG, "Configurando LED strip (RMT, GPIO %d)", LED_ESTADO_GPIO);
    /* ESP32-C6 tiene RMT hardware — se usa directamente sin condicional Kconfig */
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
    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    if (!dev_desc) {
        ESP_LOGE(TAG, "No se pudo crear device_desc");
        return ESP_ERR_NO_MEM;
    }

    /*
     * Se crea el endpoint con la config de on_off_switch (Basic + Identify,
     * identico a range_extender) y luego se sobreescribe el app_device_id
     * a EZB_ZHA_RANGE_EXTENDER_DEVICE_ID (0x0008) via af API, ya que ezbee
     * no expone ezb_zha_create_range_extender() directamente.
     */
    ezb_zha_on_off_switch_config_t ep_cfg = EZB_ZHA_ON_OFF_SWITCH_CONFIG();
    ep_cfg.basic_cfg.power_source = EZB_ZCL_BASIC_POWER_SOURCE_SINGLE_PHASE_MAINS;

    ezb_af_ep_desc_t ep_desc = ezb_zha_create_on_off_switch(ESP_ZIGBEE_HA_ON_OFF_SWITCH_EP_ID, &ep_cfg);
    if (!ep_desc) {
        ESP_LOGE(TAG, "No se pudo crear ep_desc ZHA");
        return ESP_ERR_NO_MEM;
    }

    /* Sobreescribir device ID: Range Extender (ZHA 0x0008) */
    ezb_err_t err = ezb_af_ep_desc_set_app_device_id(ep_desc, EZB_ZHA_RANGE_EXTENDER_DEVICE_ID);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo fijar device ID a Range Extender (0x%04x): err=0x%x",
                 EZB_ZHA_RANGE_EXTENDER_DEVICE_ID, err);
        ezb_af_free_endpoint_desc(ep_desc);
        ezb_af_free_device_desc(dev_desc);
        return ESP_FAIL;
    }

    ezb_zcl_cluster_desc_t basic_desc = ezb_af_endpoint_get_cluster_desc(
        ep_desc, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);
    if (!basic_desc) {
        ESP_LOGE(TAG, "No se pudo obtener Basic cluster desc");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(
        ezb_zcl_basic_cluster_desc_add_attr(
            basic_desc,
            EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
            (const void *)ESP_MANUFACTURER_NAME),
        TAG, "No se pudo anadir ManufacturerName");

    ESP_RETURN_ON_ERROR(
        ezb_zcl_basic_cluster_desc_add_attr(
            basic_desc,
            EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
            (const void *)ESP_MODEL_IDENTIFIER),
        TAG, "No se pudo anadir ModelIdentifier");

    ESP_LOGI(TAG, "Endpoint: ep=%u profile=0x%04x device=0x%04x (Range Extender)",
             ESP_ZIGBEE_HA_ON_OFF_SWITCH_EP_ID, EZB_AF_HA_PROFILE_ID,
             EZB_ZHA_RANGE_EXTENDER_DEVICE_ID);
    ESP_LOGI(TAG, "Basic: manufacturer=%s model=%s power_source=0x%02x",
             &ESP_MANUFACTURER_NAME[1],
             &ESP_MODEL_IDENTIFIER[1],
             ep_cfg.basic_cfg.power_source);

    ESP_RETURN_ON_ERROR(
        ezb_af_device_add_endpoint_desc(dev_desc, ep_desc),
        TAG, "No se pudo anadir endpoint desc");

    ESP_RETURN_ON_ERROR(
        ezb_af_device_desc_register(dev_desc),
        TAG, "No se pudo registrar endpoint");

    return ESP_OK;
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

static void send_first_announce(alarm_timer_arg_t arg)
{
    (void)arg;
    ezb_zdo_device_annce_req_t annce = {.cb = NULL, .user_ctx = NULL};
    ezb_zdo_device_annce_req(&annce);
    ESP_LOGI(TAG, "Device Announce enviado (first join)");
}

static void send_second_announce(alarm_timer_arg_t arg)
{
    (void)arg;
    ezb_zdo_device_annce_req_t annce = {.cb = NULL, .user_ctx = NULL};
    ezb_zdo_device_annce_req(&annce);
    ESP_LOGI(TAG, "Device Announce reenviado (second announce)");
}

static bool esp_zigbee_app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);
    set_led(LED_RED);

    switch (signal_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Inicializando Router Zigbee Puro...");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        set_led(LED_RED);
        break;

    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        ESP_LOGI(TAG, "Evento BDB: %s (%s / 0x%02x)",
                 signal_type == EZB_BDB_SIGNAL_DEVICE_FIRST_START ? "DEVICE_FIRST_START" : "DEVICE_REBOOT",
                 bdb_status_to_str(status), status);
        if (status == EZB_BDB_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "BDB init OK. Factory new: %s", ezb_bdb_is_factory_new() ? "si" : "no");
            set_led(LED_BLUE_BDB);
            if (ezb_bdb_is_factory_new()) {
                ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "=== ROUTER RECONECTADO A LA RED ===");
                set_led(LED_GREEN);
                ezb_zdo_device_annce_req_t annce = {.cb = NULL, .user_ctx = NULL};
                ezb_zdo_device_annce_req(&annce);
                ESP_LOGI(TAG, "Device Announce enviado (rejoin)");
            }
        } else {
            ESP_LOGW(TAG, "BDB init fallo (%s / 0x%02x). Reintento en 2s...", bdb_status_to_str(status), status);
            alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, EZB_BDB_MODE_INITIALIZATION, 2000);
        }
    } break;

    case EZB_BDB_SIGNAL_STEERING: {
        ezb_bdb_comm_status_t status = *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
        if (status == EZB_BDB_STATUS_SUCCESS) {
            steering_retry_pending = false;
            ezb_extpanid_t extended_pan_id;
            ezb_nwk_get_extended_panid(&extended_pan_id);
            ESP_LOGI(TAG, "=== JOIN OK: PAN 0x%04hx, Canal %d, Addr 0x%04hx ===",
                     ezb_nwk_get_panid(), ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
            set_led(LED_GREEN);
            alarm_timer_schedule(send_first_announce,  0, 3000);
            alarm_timer_schedule(send_second_announce, 0, 8000);
        } else {
            if (steering_retry_pending) {
                break;
            }
            ESP_LOGW(TAG, "Steering fallo (%s / 0x%02x). Reintento en 5s...", bdb_status_to_str(status), status);
            if (status == EZB_BDB_STATUS_NO_NETWORK) {
                ESP_LOGW(TAG, "No se encontro red Zigbee: verificar coordinador en modo emparejamiento y dentro de rango");
                ESP_LOGI(TAG, "Siguiente reintento: %s", retry_with_initialization ? "INITIALIZATION" : "NETWORK_STEERING");
                set_led(LED_RED);
            } else if (status == EZB_BDB_STATUS_NOT_PERMITTED) {
                ESP_LOGW(TAG, "Join no permitido por el coordinador (permit join cerrado)");
                set_led(LED_RED);
            } else if (status == EZB_BDB_STATUS_TARGET_FAILURE) {
                ESP_LOGW(TAG, "Fallo de join: el coordinador rechazo la solicitud de join");
                set_led(LED_RED);
            } else if (status == EZB_BDB_STATUS_TCLK_EX_FAILURE) {
                ESP_LOGW(TAG, "Fallo intercambio de Trust Center Link Key: revisar politica de seguridad del coordinador");
            }
            if (!steering_retry_pending) {
                alarm_timer_arg_t next_mode = EZB_BDB_MODE_NETWORK_STEERING;
                if (status == EZB_BDB_STATUS_NO_NETWORK && retry_with_initialization) {
                    next_mode = EZB_BDB_MODE_INITIALIZATION;
                    set_led(LED_RED);
                }
                steering_retry_pending = true;
                alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, next_mode, 5000);
                if (status == EZB_BDB_STATUS_NO_NETWORK) {
                    set_led(LED_RED);
                    retry_with_initialization = !retry_with_initialization;
                }
            }
        }
    } break;

    case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
        uint8_t duration = *(uint8_t *)ezb_app_signal_get_params(app_signal);
        if (duration > 0) {
            ESP_LOGI(TAG, "Permit join activo en PAN 0x%04hx durante %u s", ezb_nwk_get_panid(), duration);
            set_led(LED_BLUE);
        } else {
            ESP_LOGW(TAG, "Permit join cerrado en PAN 0x%04hx", ezb_nwk_get_panid());
            set_led(LED_GREEN);
        }
    } break;

    case EZB_ZDO_SIGNAL_LEAVE:
        ESP_LOGW(TAG, "Dispositivo salio de la red");
        set_led(LED_RED);
        break;

    case EZB_ZDO_SIGNAL_LEAVE_INDICATION: {
        const ezb_zdo_signal_leave_indication_params_t *p = ezb_app_signal_get_params(app_signal);
        ESP_LOGW(TAG, "Nodo 0x%04hx abandono la red", p->short_addr);
    } break;

    default:
        ESP_LOGI(TAG, "ZB signal: %s (0x%02x)", ezb_app_signal_to_string(signal_type), signal_type);
        set_led(LED_WHITE);
        break;
    }
    return true;
}

static void esp_zigbee_stack_main_task(void *pvParameters)
{
    esp_zigbee_config_t config = ESP_ZIGBEE_DEFAULT_CONFIG();
    static const uint8_t standard_tc_link_key[] = {
        0x5a, 0x69, 0x67, 0x42, 0x65, 0x65, 0x41, 0x6c,
        0x6c, 0x69, 0x61, 0x6e, 0x63, 0x65, 0x30, 0x39,
    };

    ESP_ERROR_CHECK(esp_zigbee_init(&config));

    ezb_aps_secur_enable_distributed_security(false);
    ezb_secur_set_global_link_key(standard_tc_link_key);
    ESP_ERROR_CHECK(ezb_secur_set_tclk_exchange_required(true));
    ESP_LOGI(TAG, "Canales de steering: primary=0x%08lx secondary=0x%08lx",
             (unsigned long)ESP_ZIGBEE_PRIMARY_CHANNEL_MASK,
             (unsigned long)ESP_ZIGBEE_SECONDARY_CHANNEL_MASK);
    ESP_LOGI(TAG, "Seguridad Zigbee: Trust Center link key estandar + TCLK exchange obligatorio");
    ezb_nwk_set_min_join_lqi(0);
    ESP_LOGI(TAG, "Filtro de join LQI: %u", ezb_nwk_get_min_join_lqi());
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

    ESP_LOGI(TAG, "app_main: antes de init");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(nvs_flash_init_partition(ESP_ZIGBEE_STORAGE_PARTITION_NAME));

    ESP_LOGI(TAG, "Arrancando Router Zigbee Puro");

    BaseType_t ok = xTaskCreate(esp_zigbee_stack_main_task, "Zigbee_main", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "app_main: xTaskCreate=%ld (pdPASS=%ld)", (long)ok, (long)pdPASS);
}
