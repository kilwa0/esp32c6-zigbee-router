#pragma once

/*
 * Primary: channels 15, 20, 25 -- least WiFi overlap in the EU 2.4 GHz band.
 * Secondary: full 11-26 scan as fallback.
 */
#define ESP_ZIGBEE_PRIMARY_CHANNEL_MASK   ((1UL << 15) | (1UL << 20) | (1UL << 25))
#define ESP_ZIGBEE_SECONDARY_CHANNEL_MASK (0x07FFF800U)

_Static_assert((ESP_ZIGBEE_PRIMARY_CHANNEL_MASK & ~0x07FFF800U) == 0,
               "Primary channel mask contains invalid 802.15.4 channels");

/* EP 10: range-extender informational endpoint (Basic cluster server).
 * EP 1-4 are reserved for gesture On/Off CLIENT reporting (see below). */
#define ESP_ZIGBEE_RANGE_EXTENDER_EP_ID   (10)

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
 *   PATCH -> bugfix only */
#define ESP_SW_BUILD_ID       "\x05" "5.0.0"

/* Gesture endpoint IDs for ZCL On/Off Toggle reporting.
 * Each button gesture sends a Toggle on the corresponding endpoint;
 * the coordinator (iHost) exposes them as 4 independent switches
 * in its MQTT local broker (port 1883).
 *
 *   EP 1 -> single tap  (night mode toggle)
 *   EP 2 -> double tap  (permit-join 60 s)
 *   EP 3 -> triple tap  (TX power toggle 8/20 dBm)
 *   EP 4 -> hold 5 s    (factory reset) */
#define ROUTER_GESTURE_EP_1    1U
#define ROUTER_GESTURE_EP_2    2U
#define ROUTER_GESTURE_EP_3    3U
#define ROUTER_GESTURE_EP_4    4U

/* Report a button gesture to the coordinator via ZCL On/Off Toggle.
 * ep_id must be one of ROUTER_GESTURE_EP_1 .. ROUTER_GESTURE_EP_4.
 * Called from button.c after each gesture action completes.
 * Safe to call from any FreeRTOS task; acquires the Zigbee lock internally. */
void router_report_gesture(uint8_t ep_id);

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
