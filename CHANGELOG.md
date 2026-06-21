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

_No changes pending beyond v4.0.1._

---

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