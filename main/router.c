#include "alarm_timer.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_ieee802154.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_zigbee.h"
#include "ezbee/af.h"
#include "ezbee/secur.h"
#include "ezbee/zcl/cluster/basic_desc.h"
#include "ezbee/zdo/zdo_dev_srv_disc.h"
#include "ezbee/zha.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "led_strip_types.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdio.h>

#include "button.h"
#include "ota_file_parser.h"
#include "router.h"

static const esp_partition_t *s_ota_partition = NULL;
static esp_zb_ota_file_parser_t *s_ota_file_parser = NULL;
static esp_ota_handle_t s_ota_handle = 0;

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
#define LED_R_RED 64
#define LED_G_RED 0
#define LED_B_RED 0

#define LED_R_GREEN 0
#define LED_G_GREEN 16
#define LED_B_GREEN 0

#define LED_R_BLUE 0
#define LED_G_BLUE 0
#define LED_B_BLUE 16

#define LED_R_AMBER 30
#define LED_G_AMBER 7
#define LED_B_AMBER 0

/* Helper macro: skips the LED update when night mode is active.
 * Only applies to Zigbee state transitions; button gesture feedback
 * in button.c bypasses this guard intentionally (the user triggered it). */
#define SET_LED_IF_AWAKE(r, g, b)                                              \
  do {                                                                         \
    if (!button_is_night_mode()) {                                             \
      set_led((r), (g), (b));                                                  \
    }                                                                          \
  } while (0)

static led_strip_handle_t led_strip;

static const uint8_t s_tc_link_key[ESP_ZIGBEE_TC_LINK_KEY_LEN] = {
    0x5a, 0x69, 0x67, 0x42, 0x65, 0x65, 0x41, 0x6c,
    0x6c, 0x69, 0x61, 0x6e, 0x63, 0x65, 0x30, 0x39};
_Static_assert(sizeof(s_tc_link_key) == ESP_ZIGBEE_TC_LINK_KEY_LEN,
               "TC link key length mismatch");

static _Atomic bool steering_retry_pending;
static _Atomic bool retry_with_initialization = false;

void set_led(uint8_t r, uint8_t g, uint8_t b) {
  led_strip_set_pixel(led_strip, 0, r, g, b);
  led_strip_refresh(led_strip);
}

void set_led_locked(uint8_t r, uint8_t g, uint8_t b) {
  esp_zigbee_lock_acquire(portMAX_DELAY);
  set_led(r, g, b);
  esp_zigbee_lock_release();
}

static void configure_led(void) {
  ESP_LOGI(TAG, "Configurando LED strip (RMT, GPIO %d)", LED_ESTADO_GPIO);
  led_strip_config_t strip_config = {
      .strip_gpio_num = LED_ESTADO_GPIO,
      .max_leds = 1,
  };
  led_strip_rmt_config_t rmt_config = {
      .resolution_hz = 10 * 1000 * 1000,
      .flags.with_dma = false,
  };
  ESP_ERROR_CHECK(
      led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
  led_strip_clear(led_strip);
}

static const char *bdb_status_to_str(ezb_bdb_comm_status_t status) {
  switch (status) {
  case EZB_BDB_STATUS_SUCCESS:
    return "SUCCESS";
  case EZB_BDB_STATUS_IN_PROGRESS:
    return "IN_PROGRESS";
  case EZB_BDB_STATUS_NOT_AA_CAPABLE:
    return "NOT_AA_CAPABLE";
  case EZB_BDB_STATUS_NO_NETWORK:
    return "NO_NETWORK";
  case EZB_BDB_STATUS_TARGET_FAILURE:
    return "TARGET_FAILURE";
  case EZB_BDB_STATUS_FORMATION_FAILURE:
    return "FORMATION_FAILURE";
  case EZB_BDB_STATUS_NO_IDENTIFY_QUERY_RESPONSE:
    return "NO_IDENTIFY_QUERY_RESPONSE";
  case EZB_BDB_STATUS_BINDING_TABLE_FULL:
    return "BINDING_TABLE_FULL";
  case EZB_BDB_STATUS_NO_SCAN_RESPONSE:
    return "NO_SCAN_RESPONSE";
  case EZB_BDB_STATUS_NOT_PERMITTED:
    return "NOT_PERMITTED";
  case EZB_BDB_STATUS_TCLK_EX_FAILURE:
    return "TCLK_EX_FAILURE";
  case EZB_BDB_STATUS_NOT_ON_A_NETWORK:
    return "NOT_ON_A_NETWORK";
  case EZB_BDB_STATUS_ON_A_NETWORK:
    return "ON_A_NETWORK";
  case EZB_BDB_STATUS_CANCELLED:
    return "CANCELLED";
  case EZB_BDB_STATUS_DEV_ANNCE_SEND_FAILURE:
    return "DEV_ANNCE_SEND_FAILURE";
  default:
    return "UNKNOWN";
  }
}

static void esp_zigbee_alarm_bdb_commissioning(alarm_timer_arg_t arg) {
  esp_zigbee_lock_acquire(portMAX_DELAY);
  (void)ezb_bdb_start_top_level_commissioning(arg);
  esp_zigbee_lock_release();
  steering_retry_pending = false;
}

static void send_device_announce(alarm_timer_arg_t arg) {
  (void)arg;
  esp_zigbee_lock_acquire(portMAX_DELAY);
  ezb_zdo_device_annce_req_t annce = {.cb = NULL, .user_ctx = NULL};
  ezb_zdo_device_annce_req(&annce);
  esp_zigbee_lock_release();
  ESP_LOGI(TAG, "Device Announce enviado");
}

static bool esp_zigbee_app_signal_handler(const ezb_app_signal_t *app_signal) {
  ezb_app_signal_type_t signal_type = ezb_app_signal_get_type(app_signal);

  switch (signal_type) {
  case EZB_ZDO_SIGNAL_SKIP_STARTUP:
    ESP_LOGI(TAG, "Inicializando Router Zigbee Puro...");
    ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
    SET_LED_IF_AWAKE(LED_R_RED, LED_G_RED, LED_B_RED);
    break;

  case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
  case EZB_BDB_SIGNAL_DEVICE_REBOOT: {
    ezb_bdb_comm_status_t status =
        *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
    ESP_LOGI(TAG, "Evento BDB: %s (%s / 0x%02x)",
             signal_type == EZB_BDB_SIGNAL_DEVICE_FIRST_START
                 ? "DEVICE_FIRST_START"
                 : "DEVICE_REBOOT",
             bdb_status_to_str(status), status);
    if (status == EZB_BDB_STATUS_SUCCESS) {
      ESP_LOGI(TAG, "BDB init OK. Factory new: %s",
               ezb_bdb_is_factory_new() ? "si" : "no");
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
                           EZB_BDB_MODE_INITIALIZATION,
                           ROUTER_BDB_INIT_RETRY_MS);
    }
  } break;

  case EZB_BDB_SIGNAL_STEERING: {
    ezb_bdb_comm_status_t status =
        *((ezb_bdb_comm_status_t *)ezb_app_signal_get_params(app_signal));
    if (status == EZB_BDB_STATUS_SUCCESS) {
      steering_retry_pending = false;
      ESP_LOGI(TAG, "=== JOIN OK: PAN 0x%04hx, Canal %d, Addr 0x%04hx ===",
               ezb_nwk_get_panid(), ezb_nwk_get_current_channel(),
               ezb_nwk_get_short_address());
      SET_LED_IF_AWAKE(LED_R_GREEN, LED_G_GREEN, LED_B_GREEN);
      alarm_timer_schedule(send_device_announce, 0, 3000);
    } else {
      if (steering_retry_pending) {
        break;
      }
      ESP_LOGW(TAG, "Steering fallo (%s / 0x%02x). Reintento en %ums...",
               bdb_status_to_str(status), status, ROUTER_STEERING_RETRY_MS);
      if (status == EZB_BDB_STATUS_NO_NETWORK) {
        ESP_LOGW(TAG, "No se encontro red Zigbee: verificar coordinador en "
                      "modo emparejamiento y dentro de rango");
        SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
      } else if (status == EZB_BDB_STATUS_NOT_PERMITTED) {
        ESP_LOGW(TAG,
                 "Join no permitido por el coordinador (permit join cerrado)");
        SET_LED_IF_AWAKE(LED_R_AMBER, LED_G_AMBER, LED_B_AMBER);
      } else if (status == EZB_BDB_STATUS_TARGET_FAILURE) {
        ESP_LOGW(TAG,
                 "Fallo de join: el coordinador rechazo la solicitud de join");
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
        alarm_timer_schedule(esp_zigbee_alarm_bdb_commissioning, next_mode,
                             ROUTER_STEERING_RETRY_MS);
        if (status == EZB_BDB_STATUS_NO_NETWORK) {
          retry_with_initialization = !retry_with_initialization;
        }
      }
    }
  } break;

  case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS: {
    uint8_t duration = *(uint8_t *)ezb_app_signal_get_params(app_signal);
    if (duration > 0) {
      ESP_LOGI(TAG, "Permit join activo en PAN 0x%04hx durante %u s",
               ezb_nwk_get_panid(), duration);
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
    const ezb_zdo_signal_leave_indication_params_t *p =
        ezb_app_signal_get_params(app_signal);
    ESP_LOGW(TAG, "Nodo 0x%04hx abandono la red", p->short_addr);
  } break;

  default:
    ESP_LOGI(TAG, "ZB signal: %s (0x%02x)",
             ezb_app_signal_to_string(signal_type), signal_type);
    SET_LED_IF_AWAKE(LED_R_GREEN, LED_G_GREEN, LED_B_GREEN);
    break;
  }
  return true;
}

/* ZCL OTA Upgrade cluster callbacks */
void ota_upgrade_client_handle_ota_progress(
    ezb_zcl_ota_upgrade_client_progress_message_t *message) {
  esp_err_t ret = ESP_OK;
  ESP_LOGI(TAG, "-- OTA Upgrade Client Progress");

  switch (message->in.progress) {
  case EZB_ZCL_OTA_UPGRADE_PROGRESS_START:
    ESP_LOGI(TAG,
             "OTA Start: manuf_code=0x%04x, image_type=0x%04x, "
             "file_version=0x%08lx, image_size=%ld",
             message->in.start.manuf_code, message->in.start.image_type,
             message->in.start.file_version, message->in.start.image_size);

    s_ota_partition = esp_ota_get_next_update_partition(NULL);
    ESP_GOTO_ON_FALSE(s_ota_partition, ESP_ERR_NOT_FOUND, exit, TAG,
                      "No OTA partition found");
    s_ota_file_parser =
        esp_zb_create_ota_file_parser(message->in.start.image_size);
    ESP_GOTO_ON_FALSE(s_ota_file_parser, ESP_ERR_NOT_FOUND, exit, TAG,
                      "Failed to create OTA file parser");
#if CONFIG_ZB_DELTA_OTA
    ret = esp_delta_ota_begin(s_ota_partition, 0, &s_ota_handle);
#else
    ret = esp_ota_begin(s_ota_partition, 0, &s_ota_handle);
#endif

    ESP_GOTO_ON_FALSE(s_ota_handle, ESP_ERR_NOT_FOUND, exit, TAG,
                      "Failed to start OTA");
    break;
  case EZB_ZCL_OTA_UPGRADE_PROGRESS_RECEIVING:
    ESP_LOGI(TAG, "OTA Receiving Block: file_offset=%ld, block_size=%d",
             message->in.receiving.file_offset,
             message->in.receiving.block_size);
    esp_zb_ota_file_parser_setup(s_ota_file_parser,
                                 message->in.receiving.block_size,
                                 message->in.receiving.block);
    do {
      ret = esp_zb_ota_file_parser_process(s_ota_file_parser);
      ESP_LOGW(TAG, "In progress: [%ld / %ld]",
               message->in.receiving.file_offset +
                   message->in.receiving.block_size,
               s_ota_file_parser->total_image_size);
      if (esp_zb_ota_file_parser_is_element_value(s_ota_file_parser)) {
        switch (s_ota_file_parser->element.type) {
        case UPGRADE_IMAGE:
          ESP_GOTO_ON_FALSE(s_ota_file_parser->element.total <=
                                s_ota_partition->size,
                            ESP_ERR_INVALID_SIZE, exit, TAG,
                            "OTA image size exceeds partition size");
#if CONFIG_ZB_DELTA_OTA
          ESP_GOTO_ON_ERROR(
              esp_delta_ota_write(s_ota_handle, s_ota_file_parser->element.val,
                                  s_ota_file_parser->element.length),
              exit, TAG, "Failed to write OTA image");
#else
          ESP_GOTO_ON_ERROR(esp_ota_write(s_ota_handle,
                                          s_ota_file_parser->element.val,
                                          s_ota_file_parser->element.length),
                            exit, TAG, "Failed to write OTA image");
#endif
          break;
        default:
          ESP_LOG_BUFFER_HEX_LEVEL(TAG, s_ota_file_parser->element.val,
                                   s_ota_file_parser->element.length,
                                   ESP_LOG_WARN);
          break;
        }
      }
    } while (ret == ESP_ERR_NOT_FINISHED);
    ret = ESP_OK;
    break;
  case EZB_ZCL_OTA_UPGRADE_PROGRESS_CHECK:
    ESP_LOGI(
        TAG,
        "OTA Check: manuf_code=0x%04x, image_type=0x%04x, file_version=0x%08lx",
        message->in.check.manuf_code, message->in.check.image_type,
        message->in.check.file_version);
    ret = esp_zb_ota_file_parser_check(s_ota_file_parser);
    break;
  case EZB_ZCL_OTA_UPGRADE_PROGRESS_APPLY:
    ESP_LOGI(
        TAG,
        "OTA Apply: manuf_code=0x%04x, image_type=0x%04x, file_version=0x%08lx",
        message->in.apply.manuf_code, message->in.apply.image_type,
        message->in.apply.file_version);
#if CONFIG_ZB_DELTA_OTA
    ret = esp_delta_ota_end(s_ota_handle);
#else
    ret = esp_ota_end(s_ota_handle);
#endif
    ESP_GOTO_ON_ERROR(ret, exit, TAG, "Failed to end OTA: error: %s",
                      esp_err_to_name(ret));
    ret = esp_ota_set_boot_partition(s_ota_partition);
    ESP_GOTO_ON_ERROR(ret, exit, TAG,
                      "Failed to set OTA boot partition, error: %s",
                      esp_err_to_name(ret));
    break;
  case EZB_ZCL_OTA_UPGRADE_PROGRESS_FINISH:
    ESP_LOGI(TAG, "OTA Finish: count_down_delay=%ld seconds",
             message->in.finish.count_down_delay);
    esp_restart();
    break;
  case EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT:
    ret = esp_ota_abort(s_ota_handle);
    ESP_LOGW(TAG, "OTA Abort, error: %s", esp_err_to_name(ret));
    if (s_ota_file_parser) {
      esp_zb_free_ota_file_parser(s_ota_file_parser);
      s_ota_file_parser = NULL;
    }
    s_ota_handle = 0;
    s_ota_partition = NULL;
    break;
  default:
    ESP_LOGW(TAG, "Unknown OTA progress status: %d", message->in.progress);
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
    break;
  }

exit:
  if (ret != ESP_OK) {
    if (s_ota_file_parser) {
      esp_zb_free_ota_file_parser(s_ota_file_parser);
      s_ota_file_parser = NULL;
    }
    s_ota_handle = 0;
    s_ota_partition = NULL;
  }
  message->out.result =
      ret == ESP_OK ? EZB_ZCL_STATUS_SUCCESS : EZB_ZCL_STATUS_ABORT;
}

static void ota_client_query_next_image(
    ezb_zcl_ota_upgrade_query_next_image_rsp_message_t *message) {
  ESP_LOGI(TAG, "-- OTA Upgrade Query Next Image Response");

  if (message->in.image.status == EZB_ZCL_OTA_UPGRADE_STATUS_CODE_SUCCESS) {
    ESP_LOGI(TAG, "New image available:");
    ESP_LOGI(TAG, "  Server address: 0x%016llx",
             *(uint64_t *)&message->in.image.server_address);
    ESP_LOGI(TAG, "  Endpoint: %d", message->in.image.ep_id);
    ESP_LOGI(TAG, "  Manufacturer code: 0x%04x", message->in.image.manuf_code);
    ESP_LOGI(TAG, "  Image type: 0x%04x", message->in.image.image_type);
    ESP_LOGI(TAG, "  File version: 0x%08lx", message->in.image.file_version);
    ESP_LOGI(TAG, "  Image size: %ld bytes", message->in.image.size);
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
  } else {
    ESP_LOGW(TAG, "No image available, status: 0x%02x",
             message->in.image.status);
    message->out.result = EZB_ZCL_STATUS_SUCCESS;
  }
}

/* CALLBACKS */

static void
zcl_core_set_attr_value_handler(ezb_zcl_set_attr_value_message_t *message) {
  ESP_RETURN_ON_FALSE(message, , TAG, "message is empty");
  ESP_LOGI(TAG,
           "ZCL SetAttributeValue message for endpoint(%d) cluster(0x%04x) %s "
           "with status(0x%02x)",
           message->info.dst_ep, message->info.cluster_id,
           message->info.cluster_role == EZB_ZCL_CLUSTER_SERVER ? "server"
                                                                : "client",
           message->info.status);
  if (message->info.cluster_id == EZB_ZCL_CLUSTER_ID_ON_OFF) {
    do_night_mode_toggle(*(uint8_t *)message->in.attribute.data.value);
    ESP_LOGI(TAG, "Set On/Off: %d",
             *(uint8_t *)message->in.attribute.data.value);
  } else {
    ESP_LOGW(TAG, "Unsupported cluster ID(0x%04x)", message->info.cluster_id);
  }
}

static void esp_zigbee_zcl_core_action_handler(
    ezb_zcl_core_action_callback_id_t callback_id, void *message) {
  switch (callback_id) {
  case EZB_ZCL_CORE_OTA_UPGRADE_CLIENT_PROGRESS_CB_ID:
    ota_upgrade_client_handle_ota_progress(message);
    break;
  case EZB_ZCL_CORE_OTA_UPGRADE_QUERY_NEXT_IMAGE_RSP_CB_ID:
    ota_client_query_next_image(message);
    break;
  case EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID:
    zcl_core_set_attr_value_handler(message);
    break;
  case EZB_ZCL_CORE_DEFAULT_RSP_CB_ID: {
    ezb_zcl_cmd_default_rsp_message_t *default_rsp =
        (ezb_zcl_cmd_default_rsp_message_t *)message;
    ESP_LOGI(TAG, "Received ZCL Default Response with status(0x%02x)",
             default_rsp->in.status_code);
  } break;
  default:
    ESP_LOGW(TAG, "ZCL Core Action: ID(0x%04lx)", callback_id);
    break;
  }
}

/* Endpoints */

esp_err_t esp_zigbee_register_endpoints(void) {
  ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
  ezb_zha_on_off_light_config_t light_cfg = EZB_ZHA_ON_OFF_LIGHT_CONFIG();
  light_cfg.on_off_cfg.on_off =
      EZB_ZCL_ON_OFF_ON_OFF_MAX_VALUE; // Encendido al arrancar
  ezb_af_ep_desc_t ep_desc = ezb_zha_create_on_off_light(EP_ID, &light_cfg);
  ezb_zcl_cluster_desc_t basic_desc = {0};
  ezb_zcl_cluster_desc_t ota_client_desc = EZB_INVALID_ZCL_CLUSTER_DESC;

  ezb_zcl_ota_upgrade_cluster_client_config_t client_default_cfg = {
      .upgrade_server_id = OTA_SERVER,
      .file_offset = 0,
      .image_upgrade_status =
          EZB_ZCL_OTA_UPGRADE_IMAGE_UPGRADE_STATUS_DEFAULT_VALUE,
      .manufacturer_id = 0x1234,
      .image_type_id = 0x5678,
  };
  light_cfg.on_off_cfg.on_off =
      EZB_ZCL_ON_OFF_ON_OFF_MAX_VALUE; // Encendido al arrancar
  basic_desc = ezb_af_endpoint_get_cluster_desc(
      ep_desc, EZB_ZCL_CLUSTER_ID_BASIC, EZB_ZCL_CLUSTER_SERVER);
  ezb_zcl_basic_cluster_desc_add_attr(basic_desc,
                                      EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID,
                                      (void *)ESP_MANUFACTURER_NAME);
  ezb_zcl_basic_cluster_desc_add_attr(basic_desc,
                                      EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID,
                                      (void *)ESP_MODEL_IDENTIFIER);
  ezb_zcl_basic_cluster_desc_add_attr(
      basic_desc, EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID, (void *)ESP_SW_BUILD_ID);
  // OTA client cluster
  ota_client_desc = ezb_zcl_ota_upgrade_create_cluster_desc(
      &client_default_cfg, EZB_ZCL_CLUSTER_CLIENT);
  ESP_RETURN_ON_FALSE(ota_client_desc != EZB_INVALID_ZCL_CLUSTER_DESC, ESP_FAIL,
                      TAG, "OTA client cluster desc invalido");
  ESP_ERROR_CHECK(ezb_af_endpoint_add_cluster_desc(ep_desc, ota_client_desc));
  ESP_ERROR_CHECK(ezb_af_device_add_endpoint_desc(dev_desc, ep_desc));
  ESP_ERROR_CHECK(ezb_af_device_desc_register(dev_desc));
  ESP_ERROR_CHECK(ezb_zcl_ota_upgrade_set_download_block_size(EP_ID, 223));

  ezb_zcl_core_action_handler_register(esp_zigbee_zcl_core_action_handler);
  ESP_LOGI(TAG, "Zigbee endpoints registered: On/Off Light (EP %d)", EP_ID);

  return ESP_OK;
}
static void esp_zigbee_stack_main_task(void *pvParameters) {
  esp_zigbee_config_t config = ESP_ZIGBEE_DEFAULT_CONFIG();
  ESP_ERROR_CHECK(esp_zigbee_init(&config));

  /* TX power: arrancar a LOW (8 dBm), potencia de trabajo normal.
   * Triple-tap del boton BOOT alterna entre LOW (8) y HIGH (20) dBm. */
  int8_t tx_power_dbm = ROUTER_TX_POWER_LOW_DBM;
  esp_err_t pw_err = esp_ieee802154_set_txpower(tx_power_dbm);
  if (pw_err != ESP_OK) {
    ESP_LOGW(TAG, "No se pudo fijar TX power a %d dBm: %s", tx_power_dbm,
             esp_err_to_name(pw_err));
  } else {
    ESP_LOGI(TAG, "TX power fijado a %d dBm (normal de trabajo)", tx_power_dbm);
  }

  ezb_aps_secur_enable_distributed_security(false);
  ezb_secur_set_global_link_key(s_tc_link_key);
  ESP_ERROR_CHECK(ezb_secur_set_tclk_exchange_required(true));
  ESP_LOGI(TAG, "Canales: primary=0x%08lx secondary=0x%08lx",
           (unsigned long)ESP_ZIGBEE_PRIMARY_CHANNEL_MASK,
           (unsigned long)ESP_ZIGBEE_SECONDARY_CHANNEL_MASK);
  ESP_LOGI(TAG, "ZB step: set_min_join_lqi");
  ezb_nwk_set_min_join_lqi(0);

  ESP_LOGI(TAG, "ZB step: set_primary_channel_set");
  ESP_ERROR_CHECK(
      ezb_bdb_set_primary_channel_set(ESP_ZIGBEE_PRIMARY_CHANNEL_MASK));

  ESP_LOGI(TAG, "ZB step: set_secondary_channel_set");
  ESP_ERROR_CHECK(
      ezb_bdb_set_secondary_channel_set(ESP_ZIGBEE_SECONDARY_CHANNEL_MASK));

  ESP_LOGI(TAG, "ZB step: add_signal_handler");
  ESP_ERROR_CHECK(ezb_app_signal_add_handler(esp_zigbee_app_signal_handler));

  ESP_LOGI(TAG, "ZB step: register_endpoints");
  ESP_ERROR_CHECK(esp_zigbee_register_endpoints());

  ESP_LOGI(TAG, "ZB step: start");
  ESP_ERROR_CHECK(esp_zigbee_start(false));

  esp_zigbee_launch_mainloop();

  esp_zigbee_deinit();
  vTaskDelete(NULL);
}

void app_main(void) {
  configure_led();
  ESP_ERROR_CHECK(button_init());

  ESP_LOGI(TAG, "app_main: antes de init");
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(nvs_flash_init_partition(ESP_ZIGBEE_STORAGE_PARTITION_NAME));
  ESP_LOGI(TAG, "Arrancando Router Zigbee Puro");

  BaseType_t ok = xTaskCreate(esp_zigbee_stack_main_task, "Zigbee_main",
                              ZIGBEE_MAIN_TASK_STACK_SIZE, NULL, 5, NULL);
  ESP_LOGI(TAG, "app_main: xTaskCreate=%ld (pdPASS=%ld)", (long)ok,
           (long)pdPASS);
}
