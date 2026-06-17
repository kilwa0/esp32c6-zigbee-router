# esp32c6-zigbee-router

> 🇪🇸 [Versión en español disponible: Docs/LNG/ES/README.md](Docs/LNG/ES/README.md)

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

### GPIO Function Table

| GPIO | Pin | Primary function | Alternate functions | Notes |
|------|-----|-----------------|---------------------|-------|
| IO0  |  7  | GPIO | XTAL_32K_P, LP_GPIO0, ADC1_CH0 | Strapping pin |
| IO1  |  8  | GPIO | XTAL_32K_N, LP_GPIO1, ADC1_CH1 | |
| IO2  | 12  | SPI FSPIQ | LP_GPIO2, ADC1_CH2 | |
| IO3  | 13  | GPIO | LP_GPIO3, ADC1_CH3 | |
| IO4  |  3  | JTAG MTMS / I2C SDA | LP_GPIO4, LP_UART_RXD, ADC1_CH4, FSPIHD | Strapping pin |
| IO5  |  4  | JTAG MTDI / I2C SCL | LP_GPIO5, LP_UART_TXD, ADC1_CH5, FSPIWP | Strapping pin |
| IO6  |  5  | JTAG MTCK | LP_GPIO6, LP_I2C_SDA, ADC1_CH6, FSPICLK | |
| IO7  |  6  | JTAG MTDO | LP_GPIO7, LP_I2C_SCL, FSPID | |
| **IO8** | **9** | **WS2812 LED (this fw)** | GPIO | **Strapping pin — used by this firmware** |
| **IO9** | 24 | **BOOT button** | ADC2_CH1 | Strapping pin — free as GPIO at runtime |
| IO10 | 10 | GPIO | — | |
| IO11 | 11 | GPIO / SPI Flash | — | May be reserved for internal flash |
| IO12 | 26 | USB JTAG D− | — | Native USB (avoid if USB CDC active) |
| IO13 | 27 | USB JTAG D+ | — | Native USB (avoid if USB CDC active) |
| IO15 | 28 | GPIO | — | Strapping pin |
| IO16 | 17 | GPIO | — | |
| IO17 | 23 | UART0 TX | FSPICS0 | Connected to USB–UART chip (CH343) |
| IO18 | 22 | UART0 RX | SDIO_CMD, FSPICS2 | Connected to USB–UART chip (CH343) |
| IO19 | 30 | USB D− | SDIO_CLK, FSPICS3 | Native USB |
| IO20 | 31 | USB D+ | SDIO_DATA0, FSPICS4 | Native USB |
| IO21 | 32 | GPIO / SPI CS | SDIO_DATA1, FSPICS5 | |
| IO22 | 33 | GPIO / SPI MOSI | SDIO_DATA2 | |
| IO23 | 34 | GPIO / SPI MISO | SDIO_DATA3 | |

### Usage Notes

- ⚠️ **Strapping pins** (IO0, IO4, IO5, IO8, IO9, IO15): the logic level at reset determines boot mode. Leave unconnected or add appropriate pull-up/pull-down resistors.
- 🔴 **IO8** is connected to the **onboard WS2812 LED** on the DevKitC-1 and is the pin used by this firmware for the status LED.
- 🟡 **IO4–IO7**: JTAG pins. Available as GPIO when no hardware debugger is attached.
- 🔵 **IO12–IO13, IO19–IO20**: native USB bus pins. Reserved if USB CDC/JTAG is in use.
- ⚪ **IO17/IO18**: internally connected to the USB–UART chip (CH343). These are the serial console pins (`idf.py monitor`).
- 🟢 **IO11**: may be reserved for internal flash in OSPI variants. Check the specific board schematic.
- 🔘 **IO9 (BOOT)**: at boot determines flash mode (HIGH = normal boot). Once firmware is running, it is a general-purpose GPIO used by `button.c` for control gestures.

---

## Configuration

The project uses `sdkconfig.defaults` and `sdkconfig.defaults.esp32c6` with pre-configured values. Editable parameters via `Kconfig` are in `main/Kconfig.projbuild`:

| Kconfig parameter | Description | Default |
|-------------------|-------------|---------|
| `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK` | Primary channels: 15, 20, 25 (minimum Wi-Fi 2.4 GHz overlap in the EU) | `0x02108000` |
| `ESP_ZIGBEE_SECONDARY_CHANNEL_MASK` | Secondary channels: full 11–26 scan (fallback) | `0x07FFF800` |

To customize manufacturer name and model identifier, edit `main/router.h`:

```c
#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"
#define ESP_MODEL_IDENTIFIER  "\x08""ESP32-C6"
```

> **Note**: The `\xNN` prefix is the string length in ZCL format (Pascal string). Update the length byte if you modify the text.

### Timing, Stack, and RF Power Constants

Defined in `main/router.h`:

| Constant | Default | Description |
|----------|---------|-------------|
| `ROUTER_BDB_INIT_RETRY_MS` | `2000` ms | Delay between BDB initialization retries |
| `ROUTER_STEERING_RETRY_MS` | `5000` ms | Delay between network steering retries |
| `ZIGBEE_MAIN_TASK_STACK_SIZE` | `10240` bytes | Zigbee main task stack size |
| `ROUTER_TX_POWER_LOW_DBM` | `8` dBm | Normal RF transmit power |
| `ROUTER_TX_POWER_HIGH_DBM` | `20` dBm | Boost RF transmit power (triple-tap) |

---

## Firmware Architecture

### Main Files

| File | Description |
|------|-------------|
| `main/router.c` | Core logic: initialization, Zigbee state machine, status LED |
| `main/router.h` | Configuration constants, channel masks, stack init macros |
| `main/button.c` | BOOT button gestures (GPIO9): ISR + FreeRTOS software timers, TX toggle, factory reset |
| `main/button.h` | Public button API: `button_init()` only |
| `main/Kconfig.projbuild` | Build system configuration options |
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
| `EZB_BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` failure | Retry after `ROUTER_BDB_INIT_RETRY_MS` | 🔴 Red |
| `EZB_BDB_SIGNAL_STEERING` OK | Joined network, sends Device Announce (×2: 3 s and 8 s) | 🟢 Green |
| `EZB_BDB_SIGNAL_STEERING` `NO_NETWORK` | Retry, alternates INIT/STEERING | 🟡 Amber |
| `EZB_BDB_SIGNAL_STEERING` `NOT_PERMITTED` / `TARGET_FAILURE` | Scheduled retry | 🟡 Amber |
| `EZB_BDB_SIGNAL_STEERING` `TCLK_EX_FAILURE` | Warning log, retry | — |
| `EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS` active | Join window open on PAN | 🔵 Blue |
| `EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS` closed | Join window closed | 🟢 Green |
| `EZB_ZDO_SIGNAL_LEAVE` | Device removed from network | 🔴 Red |

### Steering Retry Logic

When steering fails, the firmware alternates between two retry modes to maximize join probability:

- **`EZB_BDB_MODE_NETWORK_STEERING`**: direct join attempt
- **`EZB_BDB_MODE_INITIALIZATION`**: full stack re-initialization before the next attempt

`retry_with_initialization` controls the alternation. A retry is only scheduled if `steering_retry_pending` is `false`, preventing duplicate timer accumulation. Both variables are `_Atomic bool` to guarantee visibility between the timer task and the signal handler.

### Zigbee Security

- **Distributed security**: disabled (`ezb_aps_secur_enable_distributed_security(false)`)
- **Trust Center Link Key**: standard ZigBee Alliance HA key (`ZigBeeAlliance09`)
- **TCLK exchange**: mandatory (`ezb_secur_set_tclk_exchange_required(true)`)
- **Minimum LQI for join**: 0 (no filtering, maximum range compatibility)
- **Install code policy**: enabled (`install_code_policy = true`)

> ⚠️ The standard TC Link Key is public and well-known. For production networks with high security requirements, consider a full install code policy.

---

## BOOT Button (GPIO9)

| Gesture | Threshold | Action | LED feedback |
|---------|-----------|--------|--------------|
| Triple tap | < 500 ms between presses | Toggle TX power: 8 dBm ↔ 20 dBm | 3× bright red (boost) / 3× soft blue (normal) |
| Hold 5 s | GPIO LOW for 5 s | Factory reset (Zigbee NVS erase) + reboot | Fast magenta blink → solid red → reboot |

> **Factory reset note**: erase is performed with `nvs_flash_erase_partition()` directly on the flash driver. The chip IEEE address (EUI-64) **does not change** — it is burned into factory efuses.

> **Reboot note**: the DevKitC-1 has a dedicated **RST** button on the board. Software reboot gesture is not implemented — use the physical button.

---

## TX Power (IEEE 802.15.4)

| Mode | Value | When to use |
|------|-------|-------------|
| Normal (boot default) | **8 dBm** | Daily operation in a home network. Optimal range/power/interference balance. |
| Boost (triple-tap) | **20 dBm** | Distant node or point troubleshooting. Max ESP32-C6 hardware / CE ETSI EN 300 328 limit. |

Boost mode is **volatile**: reverts to 8 dBm on next reboot. Cycle: `8 dBm → 20 dBm → 8 dBm → ...`

---

## LED Status Colors

### Network state colors

| Color | R | G | B | State |
|-------|---|---|---|-------|
| 🔴 Soft red | 64 | 0 | 0 | No network / critical error / removed from network |
| 🟡 Amber | 30 | 7 | 0 | Searching for network / join rejected / active without network |
| 🟢 Green | 0 | 16 | 0 | Connected to Zigbee network |
| 🔵 Blue | 0 | 0 | 16 | Permit join active on PAN |

### Gesture feedback colors

| Color | R | G | B | Meaning |
|-------|---|---|---|--------|
| 🔴 Bright red | 255 | 0 | 0 | TX boost confirmed (20 dBm active) |
| 🔵 Soft blue | 0 | 0 | 64 | TX normal confirmed (8 dBm active) |
| 🟣 Magenta | 255 | 0 | 255 | Destructive action in progress (factory reset) |

**Color semantics:**
- 🔴 **Red** — critical error or total network absence / TX in boost mode
- 🟡 **Amber** — warning / in progress: device is active and retrying
- 🟢 **Green** — nominal state: router is integrated in the network and routing traffic
- 🔵 **Blue** — informational: PAN join window open / TX in normal mode
- 🟣 **Magenta** — destructive action in progress (blinking); do not interrupt

---

## Registered Zigbee Endpoint

| Parameter | Value |
|-----------|-------|
| Endpoint ID | 1 (`ESP_ZIGBEE_RANGE_EXTENDER_EP_ID`) |
| Profile | HA (`EZB_AF_HA_PROFILE_ID` / `0x0104`) |
| Device ID | Range Extender (`0x0008`) |
| Power Source (Basic cluster) | `SINGLE_PHASE_MAINS` (`0x01`) |
| Server clusters | Basic (with `ManufacturerName` and `ModelIdentifier`) |

> The endpoint registers only the Basic cluster with the mandatory identification attributes. No application clusters (On/Off, etc.) are included since the router acts exclusively as an infrastructure node.

---

## Partition Table

Configured in `partitions.csv` with a dedicated `zb_storage` partition for persistent Zigbee stack storage (network keys, address, channel). This ensures the device remembers its network after a power cycle without re-pairing.

---

## Versions

This project follows [Semantic Versioning](https://semver.org/). Full change history in [CHANGELOG.md](CHANGELOG.md).

| Version | Date | Summary |
|---------|------|---------|
| **v3.0.0** | 2026-06-17 | Full migration to `esp-zigbee-lib` ≥ 2.0.0 (`ezb_*` API). Compilation breaking change. |
| **v2.0.0** | 2026-06-16 | BOOT button: triple-tap TX toggle, 5 s hold factory reset. NVS erase fix. |
| **v1.0.0** | 2026-06-15 | Functional Zigbee router, RGB LED, steering retry with INIT/STEERING alternation. |

---

## Troubleshooting

| Symptom | Likely cause | Action |
|---------|--------------|--------|
| Build error `ezb_*` undeclared | SDK 1.x installed | Delete `build/` and components, run `idf.py reconfigure` with SDK ≥ 2.0.0 |
| 🟡 Amber LED constant | Coordinator permit join closed or out of range | Open permit join on coordinator and check distance |
| 🟡 Amber alternating with 🔴 Red | STEERING → INITIALIZATION cycle in progress | Normal when coordinator unavailable; wait or open permit join |
| `TCLK_EX_FAILURE` in logs | Coordinator security policy incompatible | Verify coordinator accepts standard TC Link Key |
| `NOT_PERMITTED` in logs | Permit join closed on coordinator | Open permit join (60–254 s) |
| WDT reset during steering | Insufficient Zigbee task stack | Check `ZIGBEE_MAIN_TASK_STACK_SIZE` ≥ 10240 bytes |
| Device not visible in coordinator UI | Incorrect `power_source` in Basic cluster | Check `power_source = SINGLE_PHASE_MAINS` |
| Router does not scan all channels | Primary mask too restrictive | Review `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK`; secondary fallback scans 11–26 |
| Triple-tap not responding | Presses too slow | Keep less than 500 ms between each press |
| Factory reset does not clear network | Error in `nvs_flash_erase_partition` | Check serial log; verify partition name in `partitions.csv` |
| TX power does not change after triple-tap | `esp_ieee802154_set_txpower` fails | Check log for `set_txpower(%d) failed` |

---

## License

The original source code of this project is published under **CC0-1.0** (public domain). See SPDX headers in source files.

This firmware **links and depends** on the following Espressif libraries:

| Dependency | License | Notes |
|------------|---------|-------|
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache-2.0 | Base framework |
| [esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk) | Apache-2.0 | High-level Zigbee SDK |
| [esp-zboss-lib](https://github.com/espressif/esp-zboss-lib) | [Espressif License](https://github.com/espressif/esp-zboss-lib/blob/master/LICENSE) | ZBOSS stack as binary; separate license |

To contribute, see [CONTRIBUTING.md](CONTRIBUTING.md).
