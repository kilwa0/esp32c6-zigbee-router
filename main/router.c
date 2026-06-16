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
#include "ezbee/zdo/zdo_dev_srv_disc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "router.h"

static const char *TAG = "ROUTER ESP32C6";

#define LED_ESTADO_GPIO 8

/* Colores del LED de estado (R, G, B) -- valores 0-255.
 * Semantica:
 *   RED   -> error critico / dispositivo fuera de red
 *   AMBER -> advertencia / buscando red / join rechazado (vivo, sin red)
 *   GREEN -> unido a la red y operativo
 *   BLUE  -> ventana permit-join abierta
 *
 * AMBER es preferible a ORANGE como nombre porque es el color
 * universalmente asociado a "atencion / en proceso" en semaforos y
 * electronica de estado (vs ORANGE que es solo un tono de color). */
#define LED_RED    16,  0,  0
#define LED_GREEN   0, 16,  0
#define LED_BLUE    0,  0, 16
#define LED_AMBER  30,  7,  0

static led_strip_handle_t led_strip;

/* -------------------------------------------------------------------------
 * SECURITY: TC Link Key — static linkage, not exported.
 *
 * This is the ZigBee Alliance standard Trust Center link key
 * ("ZigBeeAlliance09", ZigBee spec §4.6.3.2.1).
 * It is a PUBLIC, WELL-KNOWN value — NOT a secret credential.
 *
 * Keeping it static here (rather than in the public header) prevents
 * accidental re-use or export via other translation units.
 * Migration to a network-unique TCLK must go through a secure
 * provisioning channel such as install codes.
 * ------------------------------------------------------------------------- */
static const uint8_t s_tc_link_key[ESP_ZIGBEE_TC_LINK_KEY_LEN] = {
    0x5a, 0x69, 0x67, 0x42, 0x65, 0x65, 0x41, 0x6c,
    0x6c, 0x69, 0x61, 0x6e, 0x63, 0x65, 0x30, 0x39
};
_Static_assert(sizeof(s_tc_link_key) == ESP_ZIGBEE_TC_LINK_KEY_LEN,
               "TC link key length mismatch");

/* Accedidas desde alarm_timer callbacks (timer task) y signal handler
   (Zigbee main task): _Atomic garantiza visibilidad sin data race. */
static _Atomic bool steering_retry_pending;
/* Inicializacion explicita aunque el estandar C garantice cero para
   variables estaticas: comunica la intencion al lector. */
static _Atomic bool retry_with_initialization = false;

static inline void set_led(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
}

/* set_led_locked: safe wrapper for calling set_led() from outside the
 * Zigbee main task (e.g. alarm_timer callbacks running in the FreeRTOS
 * timer task).  Acquires the Zigbee stack lock before touching the LED
 * to avoid a potential race with the signal handler path.
 *
 * NOTE: led_strip_set_pixel / led_strip_refresh themselves are not
 * inherently thread-safe; the lock here serialises access via the same
 * mutual-exclusion mechanism used for all Zigbee stack calls. */
static inline void set_led_locked(uint8_t r, uint8_t g, uint8_t b)
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

/* Registra un endpoint minimo con device ID 0x0008 (range_extender, HA profile).
   Solo incluye el cluster Basic con manufacturer_name y model_identifier para
   que el coordinador pueda identificar el nodo. Sin clusters de aplicacion. */
static esp_err_t register_router_endpoint(void)
{
    esp_err_t ret = ESP_OK;

    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    if (!dev_desc) {
        ESP_LOGE(TAG, "No se pudo crear device_desc");
        return ESP_ERR_NO_MEM;
    }

    ezb_af_ep_config_t ep_config = {
        .ep_id              = ESP_ZIGBEE_RANGE_EXTENDER_EP_ID,
        .app_profile_id     = EZB_AF_HA_PROFILE_ID,
        .app_device_id      = 0x0008, /* range_extender */
        .app_device_version = 0,
    };
    ezb_af_ep_desc_t ep_desc = ezb_af_create_endpoint_desc(&ep_config);
    if (!ep_desc) {
        ESP_LOGE(TAG, "No se pudo crear ep_desc");
        ret = ESP_ERR_NO_MEM;
        goto cleanup_dev;
    }

    /* API: ezb_zcl_basic_create_cluster_desc(const void *cfg, uint8_t role_mask)
       Pasamos config explicita para fijar zcl_version y power_source
       (atributos mandatorios del cluster Basic server). */
    ezb_zcl_basic_cluster_server_config_t basic_cfg = {
        .zcl_version  = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = EZB_ZCL_BASIC_POWER_SOURCE_SINGLE_PHASE_MAINS,
    };
    ezb_zcl_cluster_desc_t basic_desc = ezb_zcl_basic_create_cluster_desc(
            &basic_cfg, EZB_ZCL_CLUSTER_SERVER);
    if (!basic_desc) {
        ESP_LOGE(TAG, "No se pudo crear Basic cluster desc");
        ret = ESP_ERR_NO_MEM;
        goto cleanup_ep;
    }

    ret = ezb_zcl_basic_cluster_desc_add_attr(
            basic_desc,
            EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
            (const void *)ESP_MANUFACTURER_NAME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo anadir ManufacturerName: %s", esp_err_to_name(ret));
        goto cleanup_ep;
    }

    ret = ezb_zcl_basic_cluster_desc_add_attr(
            basic_desc,
            EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
            (const void *)ESP_MODEL_IDENTIFIER);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo anadir ModelIdentifier: %s", esp_err_to_name(ret));
        goto cleanup_ep;
    }

    ret = ezb_af_endpoint_add_cluster_desc(ep_desc, basic_desc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo anadir Basic cluster al endpoint: %s", esp_err_to_name(ret));
        goto cleanup_ep;
    }
    /* basic_desc pertenece ahora a ep_desc */

    ret = ezb_af_device_add_endpoint_desc(dev_desc, ep_desc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo anadir endpoint desc: %s", esp_err_to_name(ret));
        /* Ownership de ep_desc respecto a dev_desc es indeterminado tras fallo:
           no llamar free_endpoint_desc por separado para evitar double-free. */
        goto cleanup_dev;
    }
    /* ep_desc pertenece ahora a dev_desc */

    ret = ezb_af_device_desc_register(dev_desc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo registrar device desc: %s", esp_err_to_name(ret));
        goto cleanup_dev;
    }
    /* dev_desc registrado: el stack es su dueno a partir de aqui */

    ESP_LOGI(TAG, "Endpoint registrado: ep=%u profile=0x%04x device=0x%04x (range_extender)",
             ESP_ZIGBEE_RANGE_EXTENDER_EP_ID, EZB_AF_HA_PROFILE_ID, 0x0008);
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

/* Unica funcion de Device Announce: reemplaza send_first_announce y
   send_second_announce que tenian cuerpos identicos (DRY). */
static void send_device_announce(alarm_timer_arg_t arg)
{
    (void)arg;
    /* alarm_timer callbacks se ejecutan en el timer task de FreeRTOS,
       fuera de la Zigbee main task: lock obligatorio al acceder al stack. */
    esp_zigbee_lock_acquire(portMAX_DELAY);
    ezb_zdo_device_annce_req_t annce = {.cb = NULL, .user_ctx = NULL};
    ezb_zdo_device_annce_req(&annce);
    esp_zigbee_lock_release();
    ESP_LOGI(TAG, "Device Announce enviado");
}

static bool esp_zigbee_app_signal_handler(const ezb_app_signal_t *app_signal)
{
    ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);
    /* NOTA: NO poner set_led() aqui. Hacerlo causaba un flash rojo en
       todas las senales, incluyendo EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS y
       el caso default, enmascarando el estado real de la red. Cada case
       gestiona su propio color de LED. */

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
            set_led(LED_AMBER);
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
            ezb_extpanid_t extended_pan_id;
            ezb_nwk_get_extended_panid(&extended_pan_id);
            ESP_LOGI(TAG, "=== JOIN OK: PAN 0x%04hx, Canal %d, Addr 0x%04hx ===",
                     ezb_nwk_get_panid(), ezb_nwk_get_current_channel(), ezb_nwk_get_short_address());
            set_led(LED_GREEN);
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
                ESP_LOGI(TAG, "Siguiente reintento: %s", retry_with_initialization ? "INITIALIZATION" : "NETWORK_STEERING");
                set_led(LED_AMBER);
            } else if (status == EZB_BDB_STATUS_NOT_PERMITTED) {
                ESP_LOGW(TAG, "Join no permitido por el coordinador (permit join cerrado)");
                set_led(LED_AMBER);
            } else if (status == EZB_BDB_STATUS_TARGET_FAILURE) {
                ESP_LOGW(TAG, "Fallo de join: el coordinador rechazo la solicitud de join");
                set_led(LED_AMBER);
            } else if (status == EZB_BDB_STATUS_TCLK_EX_FAILURE) {
                ESP_LOGW(TAG, "Fallo intercambio de Trust Center Link Key: revisar politica de seguridad del coordinador");
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
                    set_led(LED_AMBER);
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
        set_led(LED_GREEN);
        break;
    }
    return true;
}

static void esp_zigbee_stack_main_task(void *pvParameters)
{
    esp_zigbee_config_t config = ESP_ZIGBEE_DEFAULT_CONFIG();

    ESP_ERROR_CHECK(esp_zigbee_init(&config));

    /* RF: set TX power to legal maximum (20 dBm / 100 mW EIRP).
     * This is within CE/ETSI EN 300 328 and FCC Part 15 limits for
     * the 2.4 GHz ISM band.  Must be called after esp_zigbee_init()
     * and before esp_zigbee_start() so the radio is initialised but
     * not yet transmitting. */
    int8_t tx_power_dbm = ROUTER_TX_POWER_DBM;
    esp_err_t pw_err = esp_ieee802154_set_txpower(tx_power_dbm);
    if (pw_err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo fijar TX power a %d dBm: %s",
                 tx_power_dbm, esp_err_to_name(pw_err));
    } else {
        ESP_LOGI(TAG, "TX power fijado a %d dBm (maximo legal CE/ETSI)", tx_power_dbm);
    }

    /* SECURITY: use static s_tc_link_key (private to this TU, not the
     * header macro) to restrict linkage of the key material. */
    ezb_aps_secur_enable_distributed_security(false);
    ezb_secur_set_global_link_key(s_tc_link_key);
    ESP_ERROR_CHECK(ezb_secur_set_tclk_exchange_required(true));
    ESP_LOGI(TAG, "Canales de steering: primary=0x%08lx secondary=0x%08lx",
             (unsigned long)ESP_ZIGBEE_PRIMARY_CHANNEL_MASK,
             (unsigned long)ESP_ZIGBEE_SECONDARY_CHANNEL_MASK);
    ESP_LOGI(TAG, "Seguridad Zigbee: Trust Center link key estandar + TCLK exchange obligatorio");
    ESP_LOGI(TAG, "Politica install code: HABILITADA (install_code_policy=true)");
    ESP_LOGI(TAG, "Max children: %u", ROUTER_MAX_CHILDREN);
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

    BaseType_t ok = xTaskCreate(esp_zigbee_stack_main_task, "Zigbee_main",
                                ZIGBEE_MAIN_TASK_STACK_SIZE, NULL, 5, NULL);
    ESP_LOGI(TAG, "app_main: xTaskCreate=%ld (pdPASS=%ld)", (long)ok, (long)pdPASS);
}
