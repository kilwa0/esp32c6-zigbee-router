#pragma once

/*
 * Primary: channels 15, 20, 25 -- least WiFi overlap in the EU 2.4 GHz band.
 * Secondary: full 11-26 scan as fallback.
 */
#define ESP_ZIGBEE_PRIMARY_CHANNEL_MASK   ((1UL << 15) | (1UL << 20) | (1UL << 25))
#define ESP_ZIGBEE_SECONDARY_CHANNEL_MASK (0x07FFF800U)

_Static_assert((ESP_ZIGBEE_PRIMARY_CHANNEL_MASK & ~0x07FFF800U) == 0,
               "Primary channel mask contains invalid 802.15.4 channels");

#define ESP_ZIGBEE_TC_LINK_KEY_LEN  16U

#define ESP_ZIGBEE_STORAGE_PARTITION_NAME "zb_storage"

#define ESP_MANUFACTURER_NAME "\x03" "DIY"
#define ESP_MODEL_IDENTIFIER  "\x1B" "ESP32-C6 Zigbee Router (DIY)"

#define EZB_ZHA_ON_OFF_LIGHT_KILWA_CONFIG()                                              \
    {                                                                              \
        .basic_cfg =                                                               \
            {                                                                      \
                .zcl_version  = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,           \
                .power_source = EZB_ZCL_BASIC_POWER_SOURCE_DEFAULT_VALUE,          \
            },                                                                     \
        .identify_cfg =                                                            \
            {                                                                      \
                .identify_time = EZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE,     \
            },                                                                     \
        .groups_cfg =                                                              \
            {                                                                      \
                .name_support = EZB_ZCL_GROUPS_NAME_SUPPORT_DEFAULT_VALUE,         \
            },                                                                     \
        .scenes_cfg =                                                              \
            {                                                                      \
                .scene_count      = EZB_ZCL_SCENES_SCENE_COUNT_DEFAULT_VALUE,      \
                .current_scene    = EZB_ZCL_SCENES_CURRENT_SCENE_DEFAULT_VALUE,    \
                .current_group    = EZB_ZCL_SCENES_CURRENT_GROUP_DEFAULT_VALUE,    \
                .scene_valid      = EZB_ZCL_SCENES_SCENE_VALID_DEFAULT_VALUE,      \
                .name_support     = EZB_ZCL_SCENES_NAME_SUPPORT_DEFAULT_VALUE,     \
                .scene_table_size = EZB_ZCL_SCENES_SCENE_TABLE_SIZE_DEFAULT_VALUE, \
            },                                                                     \
        .on_off_cfg = {                                                            \
            .on_off = EZB_ZCL_ON_OFF_ON_OFF_MAX_VALUE,                         \
        },                                                                         \
    }


/* Firmware version string exposed via ZCL Basic cluster, attribute SWBuildID (0x4000).
 * Format: ZCL character string (Pascal-style, length-prefixed).
 *
 * SemVer policy:
 *   MAJOR -> new gesture, LED semantic change, new visible Zigbee parameter
 *   MINOR -> non-disruptive feature
 *   PATCH -> bugfix only */
#define ESP_SW_BUILD_ID       "\x05" "7.0.0"
#define EP_ID (1)
#define OTA_SERVER (0xe8f60afffefc83f8)

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