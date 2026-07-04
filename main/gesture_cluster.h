#pragma once
/**
 * @file gesture_cluster.h
 * @brief Custom Zigbee cluster 0xFC01 — Button Gesture Reporting
 *
 * This cluster lives on the router (server side) and allows the
 * coordinator (client side) to receive unsolicited command frames
 * every time the user performs a button gesture on the router.
 *
 * Cluster ID : GESTURE_CLUSTER_ID  (0xFC01)
 * Profile    : HA (0x0104)
 * Device ID  : 0x0051  (reused, informational only)
 *
 * ┌──────────────┬───────┬──────────────────────────────────────────┐
 * │ Command name │ CmdID │ Payload (1 byte)                         │
 * ├──────────────┼───────┼──────────────────────────────────────────┤
 * │ NIGHT_MODE   │ 0x00  │ 0x01 = night on, 0x00 = night off        │
 * │ PERMIT_JOIN  │ 0x01  │ 0x01 = window opened, 0x00 = window closed│
 * │ TX_TOGGLE    │ 0x02  │ 0x01 = high power, 0x00 = normal power   │
 * │ FACTORY_RESET│ 0x03  │ 0x01 = reset triggered (device reboots)  │
 * └──────────────┴───────┴──────────────────────────────────────────┘
 *
 * Usage from button.c:
 *   #include "gesture_cluster.h"
 *   // After action is decided:
 *   ezb_gesture_cluster_send_cmd(GESTURE_CMD_NIGHT_MODE, s_night_mode ? 1 : 0);
 */

#include "esp_err.h"
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Cluster / endpoint constants
 * ---------------------------------------------------------------------- */

/** Manufacturer-specific cluster for button gestures (private range). */
#define GESTURE_CLUSTER_ID   0xFC01U

/** Router endpoint that hosts the gesture server cluster. */
#define GESTURE_EP_ID        1U

/** HA profile (required; coordinator must agree). */
#define GESTURE_PROFILE_ID   0x0104U

/** Informational device ID — re-used HA On/Off Switch. */
#define GESTURE_DEVICE_ID    0x0051U

/* -------------------------------------------------------------------------
 * Command IDs
 * ---------------------------------------------------------------------- */

/** @brief Single-tap: toggles LED night mode. */
#define GESTURE_CMD_NIGHT_MODE    0x00U

/** @brief Double-tap: opens / closes permit-join window. */
#define GESTURE_CMD_PERMIT_JOIN   0x01U

/** @brief Triple-tap: toggles TX power between normal and boost. */
#define GESTURE_CMD_TX_TOGGLE     0x02U

/** @brief Hold 5 s: factory reset triggered (router will reboot). */
#define GESTURE_CMD_FACTORY_RESET 0x03U

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/**
 * @brief Register the gesture cluster server on GESTURE_EP_ID.
 *
 * Must be called during Zigbee device setup, before
 * esp_zigbee_start().  Registers the endpoint with:
 *   - ZCL Basic cluster (server)
 *   - ZCL Identify cluster (server)
 *   - Gesture custom cluster (server, 0xFC01)
 *
 * @return ESP_OK on success.
 */
esp_err_t gesture_cluster_register(void);

/**
 * @brief Send a gesture command to the coordinator.
 *
 * Builds a ZCL manufacturer-specific command frame and sends it
 * via APS unicast to the coordinator (short address 0x0000,
 * endpoint 1).  The coordinator receives it as a cluster-specific
 * command on cluster 0xFC01.
 *
 * Must be called from a task context (not from ISR / timer cb
 * directly — use a deferred task or the Zigbee main loop).
 *
 * @param cmd_id  One of GESTURE_CMD_* constants.
 * @param value   Command-specific 8-bit payload (see table in header).
 * @return ESP_OK on success, or a Zigbee/APS error code.
 */
esp_err_t gesture_cluster_send_cmd(uint8_t cmd_id, uint8_t value);
