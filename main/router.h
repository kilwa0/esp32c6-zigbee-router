/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#pragma once

/* Primary: channels 15, 20, 25 — least WiFi overlap in the EU 2.4 GHz band.
 * Secondary: full 11-26 scan as fallback.
 * Having identical primary and secondary makes the BDB repeat the same
 * scan twice on NO_NETWORK, adding ~8-12 s of useless join latency.
 * See ZigBee spec §3.6.1.4.1. */
#define ESP_ZIGBEE_PRIMARY_CHANNEL_MASK   ((1UL << 15) | (1UL << 20) | (1UL << 25))
#define ESP_ZIGBEE_SECONDARY_CHANNEL_MASK (0x07FFF800U)

/* Sanity check: primary must be a subset of the full 802.15.4 channel range */
_Static_assert((ESP_ZIGBEE_PRIMARY_CHANNEL_MASK & ~0x07FFF800U) == 0,
               "Primary channel mask contains invalid 802.15.4 channels");

#define ESP_ZIGBEE_RANGE_EXTENDER_EP_ID   (1)

/* -------------------------------------------------------------------------
 * SECURITY: TC Link Key
 * -----------------------------------------------------------------------
 * The ZigBee Alliance standard Trust Center link key ("ZigBeeAlliance09")
 * is a PUBLIC, WELL-KNOWN value defined in the ZigBee spec.
 * It is NOT a secret credential and MUST NOT be treated as one.
 *
 * This key is intentionally kept in the header for documentation purposes
 * only.  The actual runtime value is defined as a static array in router.c
 * to restrict its linkage.  Any future migration to a network-specific
 * TCLK MUST be done via a secure provisioning channel (e.g. install codes).
 * -------------------------------------------------------------------------
 * SECURITY: Install Code policy
 * -----------------------------------------------------------------------
 * install_code_policy = true enforces that new devices must present a
 * valid install code before the coordinator issues a unique link key.
 * This prevents unauthorised nodes from joining the network during the
 * open permit-join window.
 *
 * To disable during initial commissioning, set CONFIG_ZB_OPEN_JOIN=y
 * in your local sdkconfig (never in sdkconfig.defaults for production).
 * ------------------------------------------------------------------------- */
#define ESP_ZIGBEE_TC_LINK_KEY_LEN  16U

#define ESP_ZIGBEE_STORAGE_PARTITION_NAME "zb_storage"

#define ESP_MANUFACTURER_NAME "\x09" "ESPRESSIF"
#define ESP_MODEL_IDENTIFIER  "\x08" "ESP32-C6"

/* Retry delays (ms) — named constants to avoid magic numbers in router.c */
#define ROUTER_BDB_INIT_RETRY_MS      2000U
#define ROUTER_STEERING_RETRY_MS      5000U

/* Maximum children this router will accept.
 * Lower values reduce the join surface and the risk of resource-exhaustion
 * attacks.  Increase only if the deployment requires more child nodes. */
#define ROUTER_MAX_CHILDREN           6U

/* Permit-join window duration in seconds.
 * 255 is the ZigBee spec maximum.  Use shorter values in production. */
#define ROUTER_JOIN_OPEN_DURATION_S   255U

/* Zigbee main task stack size.
 * Espressif documents 8 KB as the minimum; we add 2 KB of headroom for
 * esp_log calls (which push ~512-1024 bytes at DEBUG level) and for the
 * nested ezbee API calls inside register_router_endpoint(). */
#define ZIGBEE_MAIN_TASK_STACK_SIZE   (10240U)

#define ESP_ZIGBEE_ROUTER_CONFIG()                        \
    {                                                     \
        .device_type = EZB_NWK_DEVICE_TYPE_ROUTER,        \
        /* install_code_policy = true: requires nodes to  \
         * present a valid install code before joining.   \
         * Disable explicitly via CONFIG_ZB_OPEN_JOIN=y   \
         * only during initial commissioning. */          \
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
#warning "This firmware requires IEEE 802.15.4 support. For RCP-based setups refer to esp_zigbee_gateway."
#endif

#define ESP_ZIGBEE_DEFAULT_CONFIG()                         \
    {                                                       \
        .device_config = ESP_ZIGBEE_ROUTER_CONFIG(),        \
        .platform_config = ESP_ZIGBEE_PLATFORM_CONFIG(),    \
    };
