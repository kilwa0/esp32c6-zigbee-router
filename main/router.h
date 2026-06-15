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

#define ESP_ZIGBEE_RANGE_EXTENDER_EP_ID   (1)

/* ZigBee Alliance standard Trust Center link key ("ZigBeeAlliance09").
 * Defined in the ZigBee spec as the well-known default pre-configured
 * link key. This is a PUBLIC value -- not a secret credential. */
#define ESP_ZIGBEE_TC_LINK_KEY            \
    { 0x5a, 0x69, 0x67, 0x42, 0x65, 0x65, 0x41, 0x6c, \
      0x6c, 0x69, 0x61, 0x6e, 0x63, 0x65, 0x30, 0x39 }

#define ESP_ZIGBEE_STORAGE_PARTITION_NAME "zb_storage"

#define ESP_MANUFACTURER_NAME "\x09" "ESPRESSIF"
#define ESP_MODEL_IDENTIFIER  "\x08" "ESP32-C6"

/* Retry delays (ms) — named constants to avoid magic numbers in router.c */
#define ROUTER_BDB_INIT_RETRY_MS      2000U
#define ROUTER_STEERING_RETRY_MS      5000U

/* Zigbee main task stack size.
 * Espressif documents 8 KB as the minimum; we add 2 KB of headroom for
 * esp_log calls (which push ~512-1024 bytes at DEBUG level) and for the
 * nested ezbee API calls inside register_router_endpoint(). */
#define ZIGBEE_MAIN_TASK_STACK_SIZE   (10240U)

#define ESP_ZIGBEE_ROUTER_CONFIG()                   \
    {                                                \
        .device_type = EZB_NWK_DEVICE_TYPE_ROUTER,   \
        .install_code_policy = false,                \
        .zczr_config = {                             \
            .max_children = 10,                      \
        },                                           \
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
