#pragma once

/*
 * Primary: channels 15, 20, 25 -- least WiFi overlap in the EU 2.4 GHz band.
 * Secondary: full 11-26 scan as fallback.
 */
#define ESP_ZIGBEE_PRIMARY_CHANNEL_MASK   ((1UL << 15) | (1UL << 20) | (1UL << 25))
#define ESP_ZIGBEE_SECONDARY_CHANNEL_MASK (0x07FFF800U)

_Static_assert((ESP_ZIGBEE_PRIMARY_CHANNEL_MASK & ~0x07FFF800U) == 0,
               "Primary channel mask contains invalid 802.15.4 channels");

/*
 * Endpoint layout -- 4 endpoints, one per button gesture.
 *
 * Each endpoint exposes:
 *   - Basic cluster (server) on ep 1 only  [device identity]
 *   - OnOff cluster (server)               [controllable by iHost]
 *
 * Device ID 0x0100 = HA On/Off Light -- the iHost recognises this
 * and exposes a toggle action in the UI for each endpoint.
 *
 * Gesture mapping:
 *   EP_NIGHT   (1) -> do_night_mode_toggle()   single tap
 *   EP_JOIN    (2) -> do_permit_join()          double tap
 *   EP_TX      (3) -> do_tx_toggle()            triple tap
 *   EP_RESET   (4) -> do_factory_reset()        hold 5 s
 */
#define ROUTER_EP_NIGHT   1U
#define ROUTER_EP_JOIN    2U
#define ROUTER_EP_TX      3U
#define ROUTER_EP_RESET   4U

/* HA On/Off Light -- Device ID recognised and surfaced by iHost */
#define ROUTER_HA_DEVICE_ID_ON_OFF_LIGHT  0x0100U

#define ESP_ZIGBEE_TC_LINK_KEY_LEN  16U

#define ESP_ZIGBEE_STORAGE_PARTITION_NAME "zb_storage"

#define ESP_MANUFACTURER_NAME "\x09" "ESPRESSIF"
#define ESP_MODEL_IDENTIFIER  "\x08" "ESP32-C6"

/* Firmware version string exposed via ZCL Basic cluster, attribute SWBuildID (0x4000).
 * Format: ZCL character string (Pascal-style, length-prefixed).
 *
 * SemVer policy:
 *   MAJOR -> new gesture, LED semantic change, new visible Zigbee parameter
 *   MINOR -> non-disruptive feature
 *   PATCH -> bugfix only
 *
 * 5.0.0 -- BREAKING: endpoint layout changed from 1x Range Extender to
 *           4x OnOff Light. Factory reset required after flash. */
#define ESP_SW_BUILD_ID       "\x05" "5.0.0"

#define ROUTER_BDB_INIT_RETRY_MS      2000U
#define ROUTER_STEERING_RETRY_MS      5000U

#define ROUTER_MAX_CHILDREN           6U
#define ROUTER_JOIN_OPEN_DURATION_S   255U

/* RF transmit power in dBm.
 *
 * LOW  (8 dBm): potencia de trabajo normal en red domestica.
 *               Balance optimo rango/consumo/interferencia.
 *               Es el valor por defecto al arrancar.
 *
 * HIGH (20 dBm): modo boost para nodos lejanos o troubleshooting.
 *                Maximo hardware del ESP32-C6 / limite CE ETSI EN 300 328.
 *                Activar con triple-tap del boton BOOT; volatile (revierte a
 *                LOW en el siguiente reboot).
 *
 * Ciclo del triple-tap:  8 dBm -> 20 dBm -> 8 dBm -> ... */
#define ROUTER_TX_POWER_LOW_DBM        8
#define ROUTER_TX_POWER_HIGH_DBM      20

#define ZIGBEE_MAIN_TASK_STACK_SIZE   (10240U)

#define ESP_ZIGBEE_ROUTER_CONFIG()                        \
    {                                                     \
        .device_type = EZB_NWK_DEVICE_TYPE_ROUTER,        \
        .install_code_policy = true,                      \
        .zczr_config = {                                  \
            .max_children = ROUTER_MAX_CHILDREN,          \
        },                                                \
    }

#if CONFIG_SOC_IEEE802154_SUPPORTED
#define ESP_ZIGBEE_PLATFORM_CONFIG()                                 \
    {                                                                \
        .storage_partition_name = ESP_ZIGBEE_STORAGE_PARTITION_NAME, \
        .radio_config = {                                            \
            .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,              \
        },                                                           \
    }
#else
#warning "This firmware requires IEEE 802.15.4 support."
#endif

#define ESP_ZIGBEE_DEFAULT_CONFIG()                         \
    {                                                       \
        .device_config = ESP_ZIGBEE_ROUTER_CONFIG(),        \
        .platform_config = ESP_ZIGBEE_PLATFORM_CONFIG(),    \
    };

#define ROUTER_PERMIT_JOIN_DURATION_S  60U
