# Changelog

All notable changes to this project are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioned according to [Semantic Versioning](https://semver.org/):

| Component | When to increment |
|---|---|
| **MAJOR** | Externally observable behaviour change: new gestures, LED semantics, Zigbee parameters |
| **MINOR** | Non-disruptive new functionality: new gesture, new endpoint, new metric |
| **PATCH** | Pure bugfix with no observable behaviour change |

> **PATCH** tracking begins from v2.1.0. Earlier versions did not report the version to the hub and bugfixes were not versioned independently.

---

## [Unreleased]

### Added
- **OTA Firmware Upgrade Client**: Full ZCL OTA Upgrade Client cluster implementation with state-machine callback handling (`START`, `RECEIVING`, `CHECK`, `APPLY`, `FINISH`, `ABORT`). Supports both standard `esp_ota_ops.h` and delta OTA via `CONFIG_ZB_DELTA_OTA`.
- **Custom OTA File Parser** (`ota_file_parser.c/h`): Streaming TLV parser for ZCL OTA upgrade images with zero-copy value pointers and stateful header/element extraction. Optimized for memory-constrained ESP32-C6 environments.
- **New partition table entry**: Added `ota_0` partition (940K) for storing downloaded firmware images.
- **Structured light cluster config macro**: `EZB_ZHA_ON_OFF_LIGHT_KILWA_CONFIG()` replaces the generic `EZB_ZHA_ON_OFF_LIGHT_CONFIG()` with explicit basic, identify, groups, scenes, and on_off field initialization.

### Changed
- Updated firmware version to **7.0.0**.
- Registered OTA Upgrade Client cluster on the On/Off Light endpoint (EP 1) with manufacturer ID `0x1234` and image type ID `0x5678`.
- Set OTA download block size to **223 bytes** via `ezb_zcl_ota_upgrade_set_download_block_size()`.
- Added detailed Zigbee stack step logging (`set_min_join_lqi`, `set_primary_channel_set`, `set_secondary_channel_set`, `add_signal_handler`, `register_endpoints`, `start`) for improved debugging.
- Updated CMakeLists.txt to include `app_update` in PRIV_REQUIRES for OTA partition management support.
- Renamed `zb_fct` partition to `kzb_fct` in partitions.csv.

### Fixed
- Removed legacy `ESP_ZIGBEE_RANGE_EXTENDER_EP_ID` macro that was no longer used.

---

## 6.0.0

### Added
- Added Zigbee state synchronization for the silent mode endpoint when the hardware button toggles the mode.[cite:47]

### Changed
- Night mode now defaults to OFF at boot, so the status LED starts enabled.[cite:47]
- Updated the Zigbee model identifier to `ESP32-C6 Zigbee Router (DIY)`.[cite:47]

### Fixed
- Fixed the mismatch between local hardware button changes and the client-visible On/Off state by calling `silent_mode_zcl_sync()` after button toggles.[cite:47]

## [5.0.0] - 2026-07-13

### Addedd
- **Exposed Single-tap gesture through ZCL** Now you can configure ON/OFF led light using cluster capabilities

## [4.0.1] - 2026-06-21

- **Fix typo in documentation regarding about the idf version used** Release 6.2.0 does not exist and we are building this with 5.3.5

## [4.0.0] — 2026-06-21

### Added
- **Single-tap gesture** (tap window < 500 ms): toggles LED night mode. All Zigbee-state LED updates are suppressed while night mode is active via `button_is_night_mode()` guards in `router.c`. Gesture feedback from `button.c` is not suppressed. Volatile; resets to LED-on on reboot.
- **Double-tap gesture** (<500 ms between presses): opens a Permit Join window on the local PAN via `ezb_bdb_open_network()` for a configurable duration (`ROUTER_PERMIT_JOIN_DURATION_S`, default 60 s). A second double-tap while the window is open closes it immediately. The window closes automatically when the timer expires; no residual open state after reboot.
- **New LED colour — soft green pulsing** (0, 32, 0): active while the Permit Join window is open (triggered by double-tap). Returns to solid green when the window closes.
- **ZCL attribute `SWBuildID`** (`EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID`, `0x4000`) exposed on the Basic cluster with the firmware version string. The hub/coordinator can now read the firmware version directly over Zigbee.
- New constant `ROUTER_PERMIT_JOIN_DURATION_S` in `router.h` (default: `60` s).
- New function `button_is_night_mode()` in `button.h` — returns `true` while LED night mode is active.

### Changed
- Button module now manages **four** FreeRTOS software timers (previously three): `btn_tap` (tap window), `btn_hold` (5 s hold), `btn_blink` (LED blink driver), and `btn_pj` (permit-join window expiry).
- LED semantics updated: soft green pulsing is exclusively associated with a locally-opened Permit Join window (double-tap). The solid blue colour (`NWK_SIGNAL_PERMIT_JOIN_STATUS`) continues to indicate a network-level Permit Join opened by an external coordinator.

### Notes
- SemVer bump: **MAJOR** — new gestures (single-tap night mode, double-tap permit-join), new LED colour (soft green pulsing), new Zigbee attribute visible externally (`SWBuildID`).
- The double-tap window is **not persistent**: a reboot closes any open Permit Join window.
- Night mode is **not persistent**: a reboot restores normal LED operation.

---

## [3.0.0] — 2026-06-17

### Changed
- **Full Zigbee stack migration** from `esp_zb_*` (esp-zigbee-sdk 1.x) to `ezb_*` (esp-zigbee-lib ≥ 2.0.0). All API calls updated across `router.c`, `router.h`, and `button.c`.
- Component dependency updated from `esp-zigbee-sdk` to `esp-zigbee-lib ≥ 2.0.0` in `main/idf_component.yml`.

### Notes
- Runtime behaviour is **identic