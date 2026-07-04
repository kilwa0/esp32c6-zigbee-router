#pragma once
/**
 * @file gesture_cluster.h
 * @brief Custom Zigbee cluster 0xFC01 — Button Gesture Reporting
 *
 * Cluster ID : GESTURE_CLUSTER_ID  (0xFC01)
 * Profile    : HA (0x0104)
 * Device ID  : 0x0051  (HA On/Off Switch, informational)
 *
 * The cluster is registered as SERVER on the router (EP 1).
 * The coordinator must register a CLIENT handler for the same cluster.
 *
 * ┌──────────────┬───────┬──────────────────────────────────────────┐
 * │ Command name │ CmdID │ Payload (1 byte)                         │
 * ├──────────────┼───────┼──────────────────────────────────────────┤
 * │ NIGHT_MODE   │ 0x00  │ 0x01 = night on,  0x00 = night off       │
 * │ PERMIT_JOIN  │ 0x01  │ 0x01 = window opened, 0x00 = closed      │
 * │ TX_TOGGLE    │ 0x02  │ 0x01 = high power, 0x00 = normal power   │
 * │ FACTORY_RESET│ 0x03  │ 0x01 = reset triggered (device reboots)  │
 * └──────────────┴───────┴──────────────────────────────────────────┘
 *
 * Integration:
 *   1. Call gesture_cluster_register() from register_router_endpoint()
 *      in router.c, BEFORE esp_zigbee_start().
 *   2. From button.c action callbacks call gesture_cluster_send_cmd();
 *      it acquires the Zigbee lock internally.
 */

#include "esp_err.h"
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Cluster / endpoint constants
 * ---------------------------------------------------------------------- */

/** Manufacturer-specific cluster ID (private range > 0xFC00). */
#define GESTURE_CLUSTER_ID   0xFC01U

/** Router endpoint that hosts the gesture server cluster. */
#define GESTURE_EP_ID        1U

/** HA profile. */
#define GESTURE_PROFILE_ID   0x0104U

/** Informational device ID — HA On/Off Switch. */
#define GESTURE_DEVICE_ID    0x0051U

/* -------------------------------------------------------------------------
 * Command IDs
 * ---------------------------------------------------------------------- */

/** Single-tap: toggles LED night mode. */
#define GESTURE_CMD_NIGHT_MODE    0x00U

/** Double-tap: opens / closes permit-join window. */
#define GESTURE_CMD_PERMIT_JOIN   0x01U

/** Triple-tap: toggles TX power between normal and boost. */
#define GESTURE_CMD_TX_TOGGLE     0x02U

/** Hold 5 s: factory reset triggered (router will reboot). */
#define GESTURE_CMD_FACTORY_RESET 0x03U

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * @brief Register the gesture cluster server on GESTURE_EP_ID.
 *
 * Registers an endpoint with Basic + Identify + gesture custom cluster
 * (server side, 0xFC01).  Must be called during device setup, inside
 * register_router_endpoint() in router.c, before esp_zigbee_start().
 *
 * @return ESP_OK on success.
 */
esp_err_t gesture_cluster_register(void);

/**
 * @brief Send a gesture command to the coordinator (0x0000, EP 1).
 *
 * Acquires the Zigbee stack lock internally, so this function is safe
 * to call from FreeRTOS timer callbacks (task context).  Do NOT call
 * from an ISR.
 *
 * @param cmd_id  One of GESTURE_CMD_* constants.
 * @param value   Command-specific 8-bit payload (see table in header).
 * @return ESP_OK on success, or an error code.
 */
esp_err_t gesture_cluster_send_cmd(uint8_t cmd_id, uint8_t value);
