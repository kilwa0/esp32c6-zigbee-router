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

_No changes pending beyond v4.0.0._

---

## [4.0.0] — 2026-06-21

### Added
- **Double-tap gesture** (<500 ms between presses): opens a Permit Join window on the local PAN via `ezb_bdb_open_network()` for a configurable duration (`ROUTER_PERMIT_JOIN_DURATION_S`, default 60 s). The window closes automatically when the timer expires; no residual open state after reboot.
- **New LED colour — cyan pulsing** (0, 64, 64): active while the Permit Join window is open (triggered by double-tap). Returns to solid green when the window closes.
- **ZCL attribute `SWBuildID`** (`EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID`, `0x4000`) exposed on the Basic cluster with the firmware version string. The hub/coordinator can now read the firmware version directly over Zigbee.
- New constant `ROUTER_PERMIT_JOIN_DURATION_S` in `router.h` (default: `60` s).

### Changed
- Button module now manages **four** FreeRTOS software timers (previously three): tap detection, triple-tap confirmation, hold detection, and permit-join window expiry.
- LED semantics updated: cyan pulsing is exclusively associated with a locally-opened Permit Join window (double-tap). The solid blue colour (`NWK_SIGNAL_PERMIT_JOIN_STATUS`) continues to indicate a network-level Permit Join opened by an external coordinator.

### Notes
- SemVer bump: **MAJOR** — new gesture (double-tap), new LED colour (cyan pulsing), new Zigbee attribute visible externally (`SWBuildID`).
- The double-tap window is **not persistent**: a reboot closes any open Permit Join window.

---

## [3.0.0] — 2026-06-17

### Changed
- **Full Zigbee stack migration** from `esp_zb_*` (esp-zigbee-sdk 1.x) to `ezb_*` (esp-zigbee-lib ≥ 2.0.0). All API calls updated across `router.c`, `router.h`, and `button.c`.
- Component dependency updated from `esp-zigbee-sdk` to `esp-zigbee-lib ≥ 2.0.0` in `main/idf_component.yml`.

### Notes
- Runtime behaviour is **identical to v2.0.0**. No externally observable change.
- SemVer bump: **MAJOR** — the SDK migration is a breaking change at the source level; any fork or derivative built against sdk 1.x will not compile against this version.
- `esp_zb_*` symbols are not present in esp-zigbee-lib 2.x; using them is a build error.

---

## [2.0.0] — 2026-06-16

### Added
- **Module `button.c` / `button.h`**: BOOT button gestures (GPIO9) via ISR + three FreeRTOS software timers.
- **Triple-tap**: TX power toggle between 8 dBm (normal) and 20 dBm (boost). Visual feedback with 3× bright red blink (boost active) or 3× soft blue blink (normal restored). Boost mode is volatile and reverts on the next reboot.
- **Hold 5 s**: destructive factory reset. Erases the `zb_storage` partition using `nvs_flash_erase_partition()` directly on the flash driver (bypassing the NVS layer) and reboots the device. Visual feedback: fast magenta blink → solid red → reboot.
- TX power configurable via `ROUTER_TX_POWER_LOW_DBM` and `ROUTER_TX_POWER_HIGH_DBM` in `router.h`.
- Gesture feedback colours added to the LED table: bright red (255,0,0), soft blue (0,0,64), magenta (255,0,255).

### Fixed
- **Factory reset did not perform the actual erase**: the previous implementation used `nvs_open()` on the Zigbee partition, which fails silently if the partition is uninitialised or corrupt. Replaced with `nvs_flash_erase_partition()` operating at driver level, which works in any partition state.
- Removed the software reboot gesture (redundant with the physical RST button on DevKitC-1); button module simplified to a single 5 s hold stage.

### Changed
- `ZIGBEE_MAIN_TASK_STACK_SIZE` increased from 8192 to 10240 bytes.
- Initialisation flow: `button_init()` called before `nvs_flash_init()` so the button is active as early as possible.

---

## [1.0.0] — 2026-06-15

### Added
- Pure Zigbee router on ESP32-C6 with ESP-IDF ≥ 6.2.0 and `esp-zigbee-sdk`.
- WS2812 RGB LED on GPIO8 via RMT with four network states: red (no network), amber (searching), green (connected), blue (permit join active).
- Full BDB state machine: `INITIALIZATION` → `NETWORK_STEERING` with automatic retry.
- Steering retry logic with `STEERING` / `INITIALIZATION` alternation, controlled by `_Atomic bool` for cross-task safety.
- Zigbee HA endpoint: Range Extender (device `0x0008`), EP 1, Basic cluster with `ManufacturerName` and `ModelIdentifier`.
- Standard HA security: TC Link Key `ZigBeeAlliance09`, mandatory TCLK exchange, distributed security disabled.
- Custom partition table with `zb_storage` partition for persistent Zigbee NVS.
- Configurable channel masks via Kconfig: primary (15, 20, 25) and secondary (full scan 11–26).
- Double Device Announce after successful join (3 s and 8 s) to maximise coordinator visibility.
- High-level `ezbee` wrapper over `esp-zigbee-sdk`.

[Unreleased]: https://github.com/kilwa0/esp32c6-zigbee-router/compare/v4.0.0...HEAD
[4.0.0]: https://github.com/kilwa0/esp32c6-zigbee-router/compare/v3.0.0...v4.0.0
[3.0.0]: https://github.com/kilwa0/esp32c6-zigbee-router/compare/v2.0.0...v3.0.0
[2.0.0]: https://github.com/kilwa0/esp32c6-zigbee-router/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/kilwa0/esp32c6-zigbee-router/releases/tag/v1.0.0
