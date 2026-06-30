#pragma once

/*
 * Primary: channels 15, 20, 25 -- least WiFi overlap in the EU 2.4 GHz band.
 * Secondary: full 11-26 scan as fallback.
 */
#define ESP_ZIGBEE_PRIMARY_CHANNEL_MASK   ((1UL << 15) | (1UL << 20) | (1UL << 25))
#define ESP_ZIGBEE_SECONDARY_CHANNEL_MASK (0x07FFF800U)

_Static_assert((ESP_ZIGBEE_PRIMARY_CHANNEL_MASK & ~0x07FFF800U) == 0,
               "Primary channel mask contains invalid 802.15.4 channels");

#define ESP_ZIGBEE_RANGE_EXTENDER_EP_ID   (1)

#define ESP_ZIGBEE_TC_LINK_KEY_LEN  16U

#define ESP_ZIGBEE_STORAGE_PARTITION_NAME "zb_storage"

#define ESP_MANUFACTURER_NAME "\x09" "ESPRESSIF"
#define ESP_MODEL_IDENTIFIER  "\x08" "ESP32-C6"

/* Firmware version string exposed via ZCL Basic cluster, attribute SWBuildID (0x4000).
 * SemVer policy:
 *   MAJOR -> new gesture, LED semantic, new Zigbee-visible parameter
 *   MINOR -> non-disruptive feature
 *   PATCH -> bugfix only
 *
 * 7.0.0: device type changed to Color Temperature Light (0x010C);
 *        On/Off + Level Control + Color Control clusters added as servers;
 *        gesture->cluster mapping documented in router.c. */
#define ESP_SW_BUILD_ID       "\x05" "7.0.0"

#define ROUTER_BDB_INIT_RETRY_MS      2000U
#define ROUTER_STEERING_RETRY_MS      5000U

#define ROUTER_MAX_CHILDREN           6U
#define ROUTER_JOIN_OPEN_DURATION_S   255U

/* RF transmit power in dBm. */
#define ROUTER_TX_POWER_LOW_DBM        8
#define ROUTER_TX_POWER_HIGH_DBM      20

#define ZIGBEE_MAIN_TASK_STACK_SIZE   (10240U)

/* -------------------------------------------------------------------------
 * Manufacturer-specific cluster for remote gesture control.
 *
 * Cluster ID: 0xFC00  (ZCL private/manufacturer range 0xFC00-0xFFFF)
 *
 * Server-side commands (coordinator -> router):
 *   0x01  NIGHT_MODE_TOGGLE  -- equivalent to 1x tap
 *   0x02  PERMIT_JOIN        -- equivalent to 2x tap  (toggle open/close)
 *   0x03  TX_TOGGLE          -- equivalent to 3x tap  (8 dBm <-> 20 dBm)
 *   0x04  FACTORY_RESET      -- equivalent to hold 5 s (NVS erase + reboot)
 *
 * Standard cluster mapping (via On/Off 0x0006 + Level Control 0x0008):
 *   On/Off ON    -> night mode ON  (LED silenced)
 *   On/Off OFF   -> night mode OFF (LED restored)
 *   On/Off Toggle -> permit-join toggle
 *   Level slider  -> tx_toggle
 *
 * These values MUST match button_action_t in button.h.
 * ---------------------------------------------------------------------- */
#define ROUTER_GESTURE_CLUSTER_ID        0xFC00U

#define ROUTER_CMD_NIGHT_MODE_TOGGLE     0x01U
#define ROUTER_CMD_PERMIT_JOIN           0x02U
#define ROUTER_CMD_TX_TOGGLE             0x03U
#define ROUTER_CMD_FACTORY_RESET         0x04U

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
