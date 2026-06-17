# esp32c6-zigbee-router

> 🇪🇸 [Versión en español: Docs/LNG/ES/README.es.md](../ES/README.es.md)

ESP-IDF firmware for the ESP32-C6 implementing a **pure Zigbee router** with an RGB status LED and button gestures via the BOOT button. Acts as an intermediate node in a Zigbee HA (Home Automation) network, extending range and re-routing traffic between end devices and the coordinator.

---

## Supported Hardware

| Component | Specification |
|-----------|----------------|
| SoC | ESP32-C6 (native IEEE 802.15.4) |
| Flash | 16 MB |
| Status LED | Addressable RGB LED (WS2812 or compatible) on GPIO 8 via RMT |
| Framework | ESP-IDF ≥ 6.2.0 |
| Zigbee SDK | `esp-zigbee-lib` ≥ 2.0.0 (`ezb_*` API) |

---

## Build Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/) ≥ 6.2.0 with ESP32-C6 support
- `esp-zigbee-lib` ≥ 2.0.0 — high-level Zigbee SDK with `ezb_*` API
- `alarm_timer` — non-blocking deferred timers (fetched automatically from the `esp-zigbee-sdk` repo)
- `led_strip` ≥ 3.0.0 — WS2812 driver via RMT

Install all dependencies declared in `main/idf_component.yml`:

```bash
idf.py reconfigure
```

> **Migration note**: if you were building with `esp-zigbee-sdk` 1.x, delete the `build/` directory and the component cache before rebuilding — headers and symbols have changed completely.

---

## Build and Flash

```bash
# Set target
idf.py set-target esp32c6

# Build
idf.py build

# Flash and monitor
idf.py flash monitor
```

---

## GPIO Pinout — ESP32-C6-DevKitC-1 (WROOM-1 N6 module)

| GPIO | Pin | Primary function | Notes |
|------|-----|-----------------|-------|
| **IO8** | 9 | **WS2812 LED (this fw)** | Strapping pin — used by this firmware |
| **IO9** | 24 | **BOOT button** | Strapping pin — free as GPIO at runtime |
| IO17 | 23 | UART0 TX | Connected to USB–UART chip (CH343) |
| IO18 | 22 | UART0 RX | Connected to USB–UART chip (CH343) |

For the full pin table see the [Spanish README](../ES/README.es.md) or the [ESP32-C6-DevKitC-1 datasheet](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/index.html).

---

## Firmware Architecture

### Main Files

| File | Description |
|------|-------------|
| `main/router.c` | Core logic: initialization, Zigbee state machine, status LED |
| `main/router.h` | Configuration constants, channel masks, stack init macros |
| `main/button.c` | BOOT button gestures (GPIO9): ISR + FreeRTOS software timers, TX toggle, factory reset |
| `main/button.h` | Public button API: `button_init()` only |
| `main/idf_component.yml` | IDF component dependencies |
| `sdkconfig.defaults` | Minimal ESP-IDF config (16 MB flash, Zigbee enabled, DEBUG log) |
| `partitions.csv` | Partition table with `zb_storage` partition for Zigbee NVS |

### Initialization Flow

```
app_main()
  ├── configure_led()                        — init RGB LED on GPIO 8 (RMT)
  ├── button_init()                          — install GPIO9 ISR and FreeRTOS timers
  ├── nvs_flash_init()                       — general NVS
  ├── nvs_flash_init_partition("zb_storage") — Zigbee NVS
  └── xTaskCreate(esp_zigbee_stack_main_task, stack=ZIGBEE_MAIN_TASK_STACK_SIZE)
        ├── esp_zigbee_init()
        ├── esp_ieee802154_set_txpower(ROUTER_TX_POWER_LOW_DBM)  — 8 dBm on boot
        ├── ezb_bdb_set_primary_channel_set()
        ├── ezb_bdb_set_secondary_channel_set()
        ├── ezb_secur_set_global_link_key()   — standard HA TC Link Key
        ├── ezb_secur_set_tclk_exchange_required(true)
        ├── register_router_endpoint()        — Range Extender, EP 1, device 0x0008
        └── esp_zigbee_launch_mainloop()
```

### BDB State Machine

| Signal | Action | LED |
|--------|--------|-----|
| `EZB_ZDO_SIGNAL_SKIP_STARTUP` | Start `EZB_BDB_MODE_INITIALIZATION` | 🔴 Red |
| `EZB_BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + factory-new | Start `EZB_BDB_MODE_NETWORK_STEERING` | 🟡 Amber |
| `EZB_BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + rejoin | Immediate Device Announce | 🟢 Green |
| `EZB_BDB_SIGNAL_STEERING` OK | Joined network, sends Device Announce (×2: 3 s and 8 s) | 🟢 Green |
| `EZB_BDB_SIGNAL_STEERING` `NO_NETWORK` | Retry, alternates INIT/STEERING | 🟡 Amber |
| `EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS` active | Join window open on PAN | 🔵 Blue |
| `EZB_ZDO_SIGNAL_LEAVE` | Device removed from network | 🔴 Red |

---

## BOOT Button (GPIO9)

| Gesture | Threshold | Action | LED feedback |
|---------|-----------|--------|--------------|
| Triple tap | < 500 ms between presses | Toggle TX power: 8 dBm ↔ 20 dBm | 3× bright red (boost) / 3× soft blue (normal) |
| Hold 5 s | GPIO LOW for 5 s | Factory reset (Zigbee NVS erase) + reboot | Fast magenta blink → solid red → reboot |

> **Factory reset note**: erase is performed with `nvs_flash_erase_partition()` directly on the flash driver. The chip IEEE address (EUI-64) **does not change** — it is burned into factory efuses.

---

## TX Power (IEEE 802.15.4)

| Mode | Value | When to use |
|------|-------|-------------|
| Normal (boot default) | **8 dBm** | Daily operation in a home network. Optimal range/power/interference balance. |
| Boost (triple-tap) | **20 dBm** | Distant node, point troubleshooting. Max ESP32-C6 hardware / CE ETSI EN 300 328 limit. |

Boost mode is **volatile**: reverts to 8 dBm on next reboot.

---

## LED Status Colors

### Network state colors

| Color | R | G | B | State |
|-------|---|---|---|-------|
| 🔴 Soft red | 64 | 0 | 0 | No network / critical error / removed from network |
| 🟡 Amber | 30 | 7 | 0 | Searching for network / join rejected |
| 🟢 Green | 0 | 16 | 0 | Connected to Zigbee network |
| 🔵 Blue | 0 | 0 | 16 | Permit join active on PAN |

### Gesture feedback colors

| Color | R | G | B | Meaning |
|-------|---|---|---|--------|
| 🔴 Bright red | 255 | 0 | 0 | TX boost confirmed (20 dBm active) |
| 🔵 Soft blue | 0 | 0 | 64 | TX normal confirmed (8 dBm active) |
| 🟣 Magenta | 255 | 0 | 255 | Destructive action in progress (factory reset) |

---

## Registered Zigbee Endpoint

| Parameter | Value |
|-----------|-------|
| Endpoint ID | 1 (`ESP_ZIGBEE_RANGE_EXTENDER_EP_ID`) |
| Profile | HA (`EZB_AF_HA_PROFILE_ID` / `0x0104`) |
| Device ID | Range Extender (`0x0008`) |
| Power Source (Basic cluster) | `SINGLE_PHASE_MAINS` (`0x01`) |
| Server clusters | Basic (with `ManufacturerName` and `ModelIdentifier`) |

---

## Troubleshooting

| Symptom | Likely cause | Action |
|---------|--------------|--------|
| Build error `ezb_*` undeclared | SDK 1.x installed | Delete `build/` and components, run `idf.py reconfigure` with SDK ≥ 2.0.0 |
| 🟡 Amber LED constant | Coordinator permit join closed or out of range | Open permit join and check distance |
| `TCLK_EX_FAILURE` in logs | Coordinator security policy incompatible | Verify coordinator accepts standard TC Link Key |
| `NOT_PERMITTED` in logs | Permit join closed on coordinator | Open permit join (60–254 s) |
| WDT reset during steering | Insufficient Zigbee task stack | Check `ZIGBEE_MAIN_TASK_STACK_SIZE` ≥ 10240 bytes |
| Triple-tap not responding | Presses too slow | Keep less than 500 ms between each press |
| Factory reset does not clear network | Error in `nvs_flash_erase_partition` | Check serial log; verify partition name in `partitions.csv` |

---

## Versions

This project follows [Semantic Versioning](https://semver.org/). Full change history in [CHANGELOG.md](../../../CHANGELOG.md).

| Version | Date | Summary |
|---------|------|---------|
| **v3.0.0** | 2026-06-17 | Full migration to `esp-zigbee-lib` ≥ 2.0.0 (`ezb_*` API). Compilation breaking change. |
| **v2.0.0** | 2026-06-16 | BOOT button: triple-tap TX toggle, 5 s hold factory reset. NVS erase fix. |
| **v1.0.0** | 2026-06-15 | Functional Zigbee router, RGB LED, steering retry with INIT/STEERING alternation. |

---

## License

The original source code of this project is published under **CC0-1.0** (public domain). See SPDX headers in source files.

This firmware **links and depends** on the following Espressif libraries:

| Dependency | License | Notes |
|------------|---------|-------|
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache-2.0 | Base framework |
| [esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk) | Apache-2.0 | High-level Zigbee SDK |
| [esp-zboss-lib](https://github.com/espressif/esp-zboss-lib) | [Espressif License](https://github.com/espressif/esp-zboss-lib/blob/master/LICENSE) | ZBOSS stack as binary; separate license |

To contribute, see [CONTRIBUTING.md](../../../CONTRIBUTING.md).
