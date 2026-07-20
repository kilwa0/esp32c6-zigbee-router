# esp32c6-zigbee-router

> 🇪🇸 Versión en español: [Docs/LNG/ES/README.es.md](Docs/LNG/ES/README.es.md)

ESP-IDF firmware for the ESP32-C6 implementing a **pure Zigbee router** with an RGB status LED and button-based control gestures. It acts as an intermediate node in a Zigbee HA (Home Automation) network, extending range and relaying traffic between end devices and the coordinator.

---

## Supported hardware

| Component | Specification |
|-----------|---------------|
| SoC | ESP32-C6 (native IEEE 802.15.4) |
| Flash | 16 MB |
| Status LED | Addressable RGB LED (WS2812 or compatible) on GPIO 8 via RMT |
| Framework | ESP-IDF ≥ 5.3.5 |
| Zigbee SDK | `esp-zigbee-lib` ≥ 2.0.0 (`ezb_*` API) |

---

## Pinout — ESP32-C6-DevKitC-1 (module WROOM-1 N6)

```
                    ESP32-C6-DevKitC-1
                  .-------------------.
                  |  [ USB-C (UART) ] |
                  |  [ USB-C (USB)  ] |
                  |   [RST]  [BOOT]   |
                  |                   |
    3V3  ◄──  1 |                   | 38 ──►  GND
    RST  ◄──  2 |                   | 37 ──►  GND
    IO4  ◄──  3 |    ESP32-C6        | 36 ──►  5V
    IO5  ◄──  4 |    WROOM-1 N6      | 35 ──►  GND
    IO6  ◄──  5 |                   | 34 ──►  IO23
    IO7  ◄──  6 |                   | 33 ──►  IO22
    IO0  ◄──  7 |   [WS2812 LED]    | 32 ──►  IO21
    IO1  ◄──  8 |   [ GPIO 8 ]●     | 31 ──►  IO20
    IO8  ◄──  9 |                   | 30 ──►  IO19
   IO10  ◄── 10 |                   | 29 ──►  IO18
   IO11  ◄── 11 |                   | 28 ──►  IO15
    IO2  ◄── 12 |                   | 27 ──►  IO13
    IO3  ◄── 13 |                   | 26 ──►  IO12
    GND  ◄── 14 |                   | 25 ──►  GND
    5V   ◄── 15 |                   | 24 ──►  IO9
   NC    ◄── 16 |                   | 23 ──►  IO17 (TX0)
   IO16  ◄── 17 |                   | 22 ──►  IO16 (RX0)
                  |                   |
                  '-------------------'
```

### GPIO function table

| GPIO | Pin | Primary function | Alternate functions | Notes |
|------|-----|------------------|---------------------|-------|
| IO0  |  7  | GPIO | XTAL_32K_P, LP_GPIO0, ADC1_CH0 | Strapping pin |
| IO1  |  8  | GPIO | XTAL_32K_N, LP_GPIO1, ADC1_CH1 | |
| IO2  | 12  | SPI FSPIQ | LP_GPIO2, ADC1_CH2 | |
| IO3  | 13  | GPIO | LP_GPIO3, ADC1_CH3 | |
| IO4  |  3  | JTAG MTMS / I2C SDA | LP_GPIO4, LP_UART_RXD, ADC1_CH4, FSPIHD | Strapping pin |
| IO5  |  4  | JTAG MTDI / I2C SCL | LP_GPIO5, LP_UART_TXD, ADC1_CH5, FSPIWP | Strapping pin |
| IO6  |  5  | JTAG MTCK | LP_GPIO6, LP_I2C_SDA, ADC1_CH6, FSPICLK | |
| IO7  |  6  | JTAG MTDO | LP_GPIO7, LP_I2C_SCL, FSPID | |
| **IO8**  | **9**  | **LED WS2812 (this fw)** | GPIO | **Strapping pin — used by this firmware** |
| **IO9**  | 24 | **BOOT button** | ADC2_CH1 | Strapping pin — free at runtime |
| IO10 | 10  | GPIO | — | |
| IO11 | 11  | GPIO / SPI Flash | — | May be reserved for internal flash |
| IO12 | 26  | USB JTAG D− | — | Native USB (do not use if USB CDC active) |
| IO13 | 27  | USB JTAG D+ | — | Native USB (do not use if USB CDC active) |
| IO15 | 28  | GPIO | — | Strapping pin |
| IO16 | 17  | GPIO | — | |
| IO17 | 23  | UART0 TX | FSPICS0 | Connected to USB–UART chip (CH343) |
| IO18 | 22  | UART0 RX | SDIO_CMD, FSPICS2 | Connected to USB–UART chip (CH343) |
| IO19 | 30  | USB D− | SDIO_CLK, FSPICS3 | Native USB |
| IO20 | 31  | USB D+ | SDIO_DATA0, FSPICS4 | Native USB |
| IO21 | 32  | GPIO / SPI CS | SDIO_DATA1, FSPICS5 | |
| IO22 | 33  | GPIO / SPI MOSI | SDIO_DATA2 | |
| IO23 | 34  | GPIO / SPI MISO | SDIO_DATA3 | |

### Usage notes

- ⚠️ **Strapping pins** (IO0, IO4, IO5, IO8, IO9, IO15): the logic level at reset determines the boot mode. Leave unconnected or add an appropriate pull-up/pull-down resistor.
- 🔴 **IO8** is connected to the **on-board WS2812 LED** on the DevKitC-1 and is the pin used by this firmware for the status LED.
- 🟡 **IO4–IO7**: JTAG pins. Available as GPIO when no hardware debugger is in use.
- 🔵 **IO12–IO13, IO19–IO20**: native USB bus pins. Reserved if USB CDC/JTAG is active.
- ⚪ **IO17/IO18**: internally connected to the USB–UART chip (CH343). These are the serial console pins (`idf.py monitor`).
- 🟢 **IO11**: may be reserved for internal flash in OSPI variants. Check the board schematic.
- 🔘 **IO9 (BOOT)**: determines flash mode at boot (HIGH = normal boot). Once firmware is running it is a general-purpose GPIO used by `button.c` for control gestures.

---

## Build requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/) ≥ 5.3.5 with ESP32-C6 support
- Component `ezbee` (high-level wrapper over `esp-zigbee-lib`)
- Component `alarm_timer` (non-blocking deferred timers)
- Component `led_strip` (WS2812 driver via RMT)

Install dependencies declared in `main/idf_component.yml`:

```bash
idf.py reconfigure
```

---

## Build and flash

```bash
# Set target
idf.py set-target esp32c6

# Build
idf.py build

# Flash and monitor
idf.py flash monitor
```

---

## Configuration

The project uses `sdkconfig.defaults` and `sdkconfig.defaults.esp32c6` with preconfigured values. Parameters editable via `Kconfig` are in `main/Kconfig.projbuild`:

| Kconfig parameter | Description | Default |
|-------------------|-------------|--------|
| `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK` | Primary channels: 15, 20, 25 (minimum overlap with 2.4 GHz Wi-Fi in the EU) | `0x02108000` |
| `ESP_ZIGBEE_SECONDARY_CHANNEL_MASK` | Secondary channels: full scan 11–26 (fallback) | `0x07FFF800` |

To customise manufacturer name and model, edit `main/router.h`:

```c
#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"
#define ESP_MODEL_IDENTIFIER  "\x08""ESP32-C6"
```

> **Note**: The `\xNN` prefix is the string length in ZCL format (Pascal string). Update the length byte if you modify the text.
### Automatic On/Off State at Boot

The router now automatically turns on the light endpoint at boot time. This ensures that the device is immediately functional and visible to the Zigbee network upon startup.
### Timing, stack and RF power constants

Defined in `main/router.h`:

| Constant | Default | Description |
|----------|---------|-------------|
| `ROUTER_BDB_INIT_RETRY_MS` | `2000` ms | Delay between BDB initialisation retries |
| `ROUTER_STEERING_RETRY_MS` | `5000` ms | Delay between network steering retries |
| `ROUTER_PERMIT_JOIN_DURATION_S` | `60` s | Duration of the Permit Join window opened by double-tap |
| `ROUTER_JOIN_OPEN_DURATION_S` | `255` s | Duration passed to the coordinator when opening a network-level join window |
| `ROUTER_MAX_CHILDREN` | `6` | Maximum number of child devices the router will accept |
| `ZIGBEE_MAIN_TASK_STACK_SIZE` | `10240` bytes | Zigbee main task stack size |

---

## OTA Firmware Updates

The firmware includes a **ZCL OTA (Over-The-Air) Upgrade Client** implementation that allows the router to receive and apply firmware updates from a Zigbee coordinator or server.

### Supported image format

- **ZCL OTA Upgrade Image** (ZCL Specification Revision 23)
- Custom streaming TLV parser (`ota_file_parser.c/h`) for memory-efficient parsing of OTA payload data blocks

### Partition table

A dedicated `ota_0` partition (**940 KB**) is required in `partitions.csv` to store the incoming OTA image:

```csv
ota_0,    data,   fat,      0,   0x94000,
```

### Configuration

The OTA client expects upgrade images with matching identity parameters. Edit `main/router.h`:

```c
#define ESP_OTA_MANUFACTURER_ID  0x1234   // Manufacturer ID
#define ESP_OTA_IMAGE_TYPE       0x5678   // Image Type ID
```

> **Note**: The manufacturer ID and image type ID must match the values used by the OTA server/coordinator when generating the upgrade image.

### Key code components

| Component | Description |
|-----------|-------------|
| `ota_file_parser.c/h` | Custom streaming TLV parser for ZCL OTA payload data blocks. Creates a parser context via `esp_zb_create_ota_file_parser()` and processes data via `esp_zb_ota_file_parser_process()` |
| `router.c` | Registers the OTA Upgrade Client cluster and handles OTA progress callbacks via `ota_upgrade_client_handle_ota_progress()` |
| Block size | **223 bytes** — maximum data block size per OTA message |

### OTA progress states

The OTA client reports progress through the following ZCL OTA Upgrade Client callbacks:

| Signal | Meaning |
|--------|---------|
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_START` | OTA upgrade initiated |
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_RECEIVING` | Receiving image data blocks |
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_CHECK` | Image download complete; verifying image |
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_APPLY` | Applying image and switching boot partition |
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_FINISH` | Upgrade finished; rebooting |
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT` | Upgrade aborted; cleaning up |

### Build requirements

The OTA client requires the `app_update` component in `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "router.c" "ota_file_parser.c" "button.c" "silent_mode_zcl.c"
    INCLUDE_DIRS "."
    REQUIRES esp_zigbee_lib app_update
)
```

---

## Build requirements
| `ROUTER_TX_POWER_LOW_DBM` | `8` dBm | Normal RF operating power |
| `ROUTER_TX_POWER_HIGH_DBM` | `20` dBm | Boost RF power (triple-tap) |

---

## Firmware architecture

### Main files

| File | Description |
|------|-------------|
| `main/router.c` | Core logic: initialisation, Zigbee state machine, status LED |
| `main/router.h` | Configuration constants, channel masks and stack initialisation macros |
| `main/button.c` | BOOT button gestures (GPIO9): ISR + FreeRTOS timers, night mode, TX toggle, factory reset, permit-join |
| `main/button.h` | Public button module API: `button_init()` and `button_is_night_mode()` |
| `main/Kconfig.projbuild` | Configuration options exported to the build system |
| `main/idf_component.yml` | IDF component dependencies |
| `sdkconfig.defaults` | Minimal ESP-IDF configuration (16 MB flash, Zigbee enabled, DEBUG log) |
| `partitions.csv` | Partition table with `zb_storage` partition for Zigbee NVS |

### Initialisation flow

```
app_main()
  ├── configure_led()                        — initialise RGB LED on GPIO 8 (RMT)
  ├── button_init()                          — install GPIO9 ISR and FreeRTOS timers
  ├── nvs_flash_init()                       — general NVS
  ├── nvs_flash_init_partition("zb_storage") — Zigbee NVS
  └── xTaskCreate(esp_zigbee_stack_main_task, stack=ZIGBEE_MAIN_TASK_STACK_SIZE)
        ├── esp_zigbee_init()
        ├── esp_ieee802154_set_txpower(ROUTER_TX_POWER_LOW_DBM)  — 8 dBm at boot
        ├── ezb_aps_secur_enable_distributed_security(false)
        ├── ezb_secur_set_global_link_key()   — HA standard TC Link Key
        ├── ezb_secur_set_tclk_exchange_required(true)
        ├── ezb_nwk_set_min_join_lqi(0)       — no LQI filtering
        ├── ezb_bdb_set_primary_channel_set()
        ├── ezb_bdb_set_secondary_channel_set()
        ├── register_router_endpoint()        — Range Extender, EP 1, device 0x0008
        └── esp_zigbee_start() → esp_zigbee_launch_mainloop()
```

### State machine (BDB signals)

The router manages its network lifecycle via the `esp_zigbee_app_signal_handler`. Main states:

| Signal | Action | LED |
|--------|--------|-----|
| `ZDO_SIGNAL_SKIP_STARTUP` | Launch `BDB_MODE_INITIALIZATION` | 🔴 Red |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + factory-new | Launch `BDB_MODE_NETWORK_STEERING` | 🟡 Amber |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + rejoin | Immediate Device Announce | 🟢 Green |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` fail | Retry after `ROUTER_BDB_INIT_RETRY_MS` | 🔴 Red |
| `BDB_SIGNAL_STEERING` OK | Joined network, sends Device Announce (×2: 3 s and 8 s) | 🟢 Green |
| `BDB_SIGNAL_STEERING` `NO_NETWORK` | Retry after `ROUTER_STEERING_RETRY_MS`, alternates INIT/STEERING | 🟡 Amber |
| `BDB_SIGNAL_STEERING` `NOT_PERMITTED` / `TARGET_FAILURE` | Scheduled retry | 🟡 Amber |
| `BDB_SIGNAL_STEERING` `TCLK_EX_FAILURE` | Warning log, retry | — |
| `NWK_SIGNAL_PERMIT_JOIN_STATUS` active | PAN pairing window open (coordinator-triggered) | 🔵 Blue |
| `NWK_SIGNAL_PERMIT_JOIN_STATUS` closed | PAN pairing window closed | 🟢 Green |
| `ZDO_SIGNAL_LEAVE` | Device removed from network | 🔴 Red |

### Steering retry logic

When steering fails, the firmware alternates between two retry modes to maximise join probability:

- **`EZB_BDB_MODE_NETWORK_STEERING`**: direct join attempt
- **`EZB_BDB_MODE_INITIALIZATION`**: full stack reinitialisation before the next attempt

The variable `retry_with_initialization` controls the alternation. A retry is only scheduled if `steering_retry_pending` is `false`, preventing duplicate timer accumulation. Both variables are `_Atomic bool` for visibility between the timer task and the signal handler.

### Zigbee security

- **Distributed security**: disabled (`ezb_aps_secur_enable_distributed_security(false)`)
- **Trust Center Link Key**: standard ZigBee Alliance HA key (`ZigBeeAlliance09`)
- **TCLK exchange**: mandatory (`ezb_secur_set_tclk_exchange_required(true)`)
- **Minimum LQI for join**: 0 (no filtering, maximum range compatibility)
- **Install code policy**: enabled (`install_code_policy = true`) — new devices must present a valid install code before receiving a unique link key from the coordinator.

> ⚠️ The standard TC Link Key is public and well-known. For production networks with elevated security requirements, consider a full install code policy.

---

## BOOT button (GPIO9)

The DevKitC-1 BOOT button is a general-purpose GPIO once firmware is running. The `button.c` module installs an `ANYEDGE` ISR and manages **four** FreeRTOS software timers (`btn_tap`, `btn_hold`, `btn_blink`, `btn_pj`) to detect the following gestures:

| Gesture | Threshold | Action | LED feedback |
|---------|-----------|--------|--------------|
| Single-tap | < 500 ms tap window | Toggle LED night mode: silences / restores all Zigbee-state LED updates | LED off (night mode ON) / back to green (night mode OFF) |
| Double-tap | < 500 ms between presses | Open Permit Join window for `ROUTER_PERMIT_JOIN_DURATION_S` (60 s). A second double-tap while the window is open closes it immediately. | Slow green pulsing while window is open |
| Triple-tap | < 500 ms between presses | Toggle TX power: 8 dBm ↔ 20 dBm | 3× bright red (boost) / 3× soft blue (normal) |
| Hold 5 s | GPIO LOW for 5 s | Factory reset (Zigbee NVS erase) + reboot | Fast magenta blink → solid red → reboot |

> **Note on night mode**: only Zigbee state-transition LED updates are suppressed. Gesture feedback from `button.c` (TX toggle flashes, factory reset blink, permit-join pulse) is **not** suppressed — the user explicitly triggered it.

> **Note on reboot**: the DevKitC-1 has a dedicated **RST** button on the board. A software reboot gesture is not implemented — use the physical button.

> **Note on factory reset**: erase is performed with `nvs_flash_erase_partition()` directly on the flash driver, bypassing the NVS layer. This guarantees correct erasure even if the partition is uninitialised or corrupt. The IEEE address (EUI-64) of the chip **does not change** — it is factory-burned into eFuses.

> **Note on Permit Join**: the double-tap opens a local Permit Join window. Any device that joins during this window will use the standard HA trust model. The window closes automatically; it does not persist across reboots.

### Silent mode

A single press on the BOOT button toggles the status LED silent mode. Since version 6.0.0, this change is no longer only a local LED action: it also synchronizes the Zigbee endpoint state associated with silent mode through `silent_mode_zcl_sync()`, so the client can observe the new state when the change originates from the hardware button.

Default boot state:
- Silent mode now starts disabled (`night mode OFF`), so the status LED is enabled after boot.

Zigbee identity:
- Basic cluster model identifier: `ESP32-C6 Zigbee Router (DIY)`.
- Exposed firmware version: `6.0.0`.

---

## TX power (IEEE 802.15.4)

| Mode | Value | When to use |
|------|-------|-------------|
| Normal (boot default) | **8 dBm** | Daily operation in a home network. Optimal range/power/interference balance. |
| Boost (triple-tap) | **20 dBm** | Distant node that cannot reach the network, occasional troubleshooting. Maximum ESP32-C6 hardware / CE ETSI EN 300 328 limit. |

Boost mode is **volatile**: reverts to 8 dBm on the next reboot. The triple-tap cycle is: `8 dBm → 20 dBm → 8 dBm → …`

---

## Status LED — colour table

### Network state colours

| Colour | R | G | B | State |
|--------|---|---|---|-------|
| 🔴 Soft red | 64 | 0 | 0 | No network / critical error / device removed from network |
| 🟡 Amber | 30 | 7 | 0 | Searching for network / join rejected / active device without network |
| 🟢 Green | 0 | 16 | 0 | Connected to the Zigbee network |
| 🔵 Blue | 0 | 0 | 16 | Permit Join active in the PAN (coordinator-triggered) |
| 🟢 Soft green pulsing | 0 | 32 | 0 | Permit Join window open (locally triggered by double-tap) |

### Gesture feedback colours

| Colour | R | G | B | Meaning |
|--------|---|---|---|---------|
| 🔴 Bright red | 255 | 0 | 0 | TX boost confirmation (20 dBm active) |
| 🔵 Soft blue | 0 | 0 | 64 | TX normal confirmation (8 dBm active) |
| 🟣 Magenta | 255 | 0 | 255 | Destructive action in progress (factory reset) |

**Colour semantics:**
- 🔴 **Red** — critical error or total absence of network (device not operational) / TX in boost mode
- 🟡 **Amber** — warning / in progress: device is active and retrying
- 🟢 **Green** — nominal state: router is integrated in the network and relaying traffic
- 🔵 **Blue** — informational: PAN pairing window open (coordinator-triggered) / TX in normal mode
- 🟢 **Soft green pulsing** — Permit Join window open locally (double-tap); new devices can join
- 🟣 **Magenta** — destructive action in progress (pulsing); do not interrupt

---

## Registered Zigbee endpoint

| Parameter | Value |
|-----------|-------|
| Cluster | `EZB_ZHA_ON_OFF_LIGHT_CONFIG` |
| Endpoint ID | 1 (`EP_ID`) |
| Server clusters | Basic (with `ManufacturerName`, `ModelIdentifier`, and `SWBuildID`) |

### ZCL attribute SWBuildID

| Attribute | ID | Type | Value |
|-----------|----|------|-------|
| `SWBuildID` | `0x4000` (`EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID`) | String | Firmware version (e.g. `"v4.0.0"`) |

This attribute is readable over Zigbee by the coordinator or any hub. It allows remote identification of the firmware version without physical access to the device.

> The endpoint registers only the Basic cluster with mandatory identification attributes. It does not include application clusters (On/Off, etc.) since the router acts exclusively as an infrastructure node.

---

## Partition table

Configured in `partitions.csv` with a dedicated `zb_storage` partition for persistent Zigbee stack storage (network keys, address, channel). This ensures the device remembers its network across power cycles without re-pairing.

---

## Versions

This project follows [Semantic Versioning](https://semver.org/). The full change history is in [CHANGELOG.md](CHANGELOG.md).

| Version | Date | Summary |
|---------|------|---------|
| **v4.0.0** | 2026-06-21 | Single-tap night mode, double-tap Permit Join, soft-green pulsing LED, `SWBuildID` ZCL attribute. |
| **v3.0.0** | 2026-06-17 | Full Zigbee SDK migration from `esp_zb_*` to `ezb_*` (esp-zigbee-lib 2.x). No runtime change. |
| **v2.0.0** | 2026-06-16 | BOOT button: triple-tap TX toggle, 5 s hold factory reset. NVS erase fix. |
| **v1.0.0** | 2026-06-15 | Functional Zigbee router, RGB LED, steering retry with INIT/STEERING alternation. |

---

## Troubleshooting

| Symptom | Probable cause | Action |
|---------|----------------|--------|
| LED off, device operational | Night mode active (single-tap) | Single-tap again to restore LED |
| LED 🟡 amber constant | Coordinator without permit join open or out of range | Open permit join on the coordinator and verify distance |
| 🟡 amber alternating with 🔴 red | STEERING → INITIALIZATION cycle in progress | Normal if coordinator unavailable; wait or open permit join |
| `TCLK_EX_FAILURE` in logs | Incompatible coordinator security policy | Verify coordinator accepts the standard TC Link Key |
| `NOT_PERMITTED` in logs | Permit join closed on coordinator | Open permit join (60–254 s) |
| WDT reset during steering | Insufficient stack in Zigbee task | Verify `ZIGBEE_MAIN_TASK_STACK_SIZE` ≥ 10240 bytes |
| Device not visible in coordinator UI | Incorrect `power_source` in Basic cluster | Verify `power_source = SINGLE_PHASE_MAINS` |
| Router does not scan all channels | Primary mask too restrictive | Check `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK`; secondary fallback scans 11–26 |
| Single-tap does not toggle night mode | Tap too slow or counted as double-tap start | Single clean tap within 500 ms window |
| Double-tap does not open permit join | Taps too slow or interrupted by triple-tap detection | Keep < 500 ms between presses; exactly 2 taps |
| Triple-tap does not respond | Presses too slow | Keep < 500 ms between each press |
| Factory reset does not clear network | Error in `nvs_flash_erase_partition` | Check serial log — error logged with `ESP_LOGE`; verify partition name in `partitions.csv` |
| TX power does not change after triple-tap | `esp_ieee802154_set_txpower` fails | Check log: `set_txpower(%d) failed` — may indicate conflict with Zigbee stack |

---

## License

The original source code of this project is published under **CC0-1.0** (public domain). See SPDX headers in source files.

However, this firmware **links and depends on** the following Espressif libraries, which carry their own licences:

| Dependency | Licence | Notes |
|------------|---------|-------|
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache-2.0 | Base framework |
| [esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk) | Apache-2.0 | High-level Zigbee SDK |
| [esp-zboss-lib](https://github.com/espressif/esp-zboss-lib) | [Espressif Licence](https://github.com/espressif/esp-zboss-lib/blob/master/LICENSE) | Binary ZBOSS stack; separate licence |

> ⚠️ Any redistribution or derivative work must comply with the terms of **all** the above licences, including Apache-2.0 attribution requirements and the specific conditions of `esp-zboss-lib`.

To contribute, see [CONTRIBUTING.md](CONTRIBUTING.md).
