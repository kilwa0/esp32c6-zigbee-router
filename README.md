# esp32c6-zigbee-router

> 🇪🇸 Versión en español: [README.es.md](README.es.md)

ESP-IDF firmware for the ESP32-C6 implementing a **pure Zigbee router** with an RGB status LED. It acts as an intermediate node in a Zigbee HA (Home Automation) network, extending the network range and re-routing traffic between end devices and the coordinator.

---

## Supported Hardware

| Component | Specification |
|-----------|---------------|
| SoC | ESP32-C6 (native IEEE 802.15.4) |
| Flash | 16 MB |
| Status LED | Addressable RGB LED (WS2812 or similar) on GPIO 8 via RMT |
| Framework | ESP-IDF ≥ 6.2.0 |

---

## Pinout — ESP32-C6-DevKitC-1 (WROOM-1 N6 module)

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

| GPIO | Pin | Primary Function | Alternate Functions | Notes |
|------|-----|------------------|---------------------|-------|
| IO0  |  7  | GPIO             | XTAL_32K_P, LP_GPIO0, ADC1_CH0 | Strapping pin |
| IO1  |  8  | GPIO             | XTAL_32K_N, LP_GPIO1, ADC1_CH1 | |
| IO2  | 12  | SPI FSPIQ        | LP_GPIO2, ADC1_CH2 | |
| IO3  | 13  | GPIO             | LP_GPIO3, ADC1_CH3 | |
| IO4  |  3  | JTAG MTMS / I2C SDA | LP_GPIO4, LP_UART_RXD, ADC1_CH4, FSPIHD | Strapping pin |
| IO5  |  4  | JTAG MTDI / I2C SCL | LP_GPIO5, LP_UART_TXD, ADC1_CH5, FSPIWP | Strapping pin |
| IO6  |  5  | JTAG MTCK        | LP_GPIO6, LP_I2C_SDA, ADC1_CH6, FSPICLK | |
| IO7  |  6  | JTAG MTDO        | LP_GPIO7, LP_I2C_SCL, FSPID | |
| **IO8**  |  **9**  | **WS2812 LED (this fw)** | GPIO | **Strapping pin — used by this firmware** |
| IO9  | 24  | BOOT button      | ADC2_CH1 | Strapping pin — BOOT button |
| IO10 | 10  | GPIO             | — | |
| IO11 | 11  | GPIO / SPI Flash | — | May be reserved for internal flash |
| IO12 | 26  | USB JTAG D−      | — | Native USB (do not use if USB CDC is active) |
| IO13 | 27  | USB JTAG D+      | — | Native USB (do not use if USB CDC is active) |
| IO15 | 28  | GPIO             | — | Strapping pin |
| IO16 | 17  | GPIO             | — | |
| IO17 | 23  | UART0 TX         | FSPICS0 | Connected to USB–UART chip (CH343) |
| IO18 | 22  | UART0 RX         | SDIO_CMD, FSPICS2 | Connected to USB–UART chip (CH343) |
| IO19 | 30  | USB D−           | SDIO_CLK, FSPICS3 | Native USB |
| IO20 | 31  | USB D+           | SDIO_DATA0, FSPICS4 | Native USB |
| IO21 | 32  | GPIO / SPI CS    | SDIO_DATA1, FSPICS5 | |
| IO22 | 33  | GPIO / SPI MOSI  | SDIO_DATA2 | |
| IO23 | 34  | GPIO / SPI MISO  | SDIO_DATA3 | |

### Usage Notes

- ⚠️ **Strapping pins** (IO0, IO4, IO5, IO8, IO9, IO15): the logic level at reset time determines the boot mode. Leave unconnected or add an appropriate pull-up/pull-down resistor.
- 🔴 **IO8** is connected to the **onboard WS2812 LED** on the DevKitC-1 and is the pin used by this firmware for the status LED.
- 🟡 **IO4–IO7**: JTAG pins. Available as GPIO when a hardware debugger is not in use.
- 🔵 **IO12–IO13, IO19–IO20**: SoC native USB bus pins. Reserved if USB CDC/JTAG is in use.
- ⚪ **IO17/IO18**: internally connected to the USB–UART chip (CH343). These are the serial console pins (`idf.py monitor`).
- 🟢 **IO11**: may be reserved for internal flash on OSPI variants. Check the schematic of the specific board.

---

## Build Requirements

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/) ≥ 6.2.0 with ESP32-C6 support
- `ezbee` component (high-level wrapper over `esp-zigbee-sdk`)
- `alarm_timer` component (non-blocking deferred timers)
- `led_strip` component (WS2812 driver via RMT)

Install the dependencies declared in `main/idf_component.yml`:

```bash
idf.py reconfigure
```

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

## Configuration

The project uses `sdkconfig.defaults` and `sdkconfig.defaults.esp32c6` with pre-configured values. The parameters editable via `Kconfig` are located in `main/Kconfig.projbuild`:

| Kconfig Parameter | Description | Default Value |
|-------------------|-------------|---------------|
| `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK` | Primary channels: 15, 20, 25 (minimum overlap with 2.4 GHz Wi-Fi in EU) | `0x02108000` |
| `ESP_ZIGBEE_SECONDARY_CHANNEL_MASK` | Secondary channels: full scan 11–26 (fallback) | `0x07FFF800` |

To customise the manufacturer name and device model, edit `main/router.h`:

```c
#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"
#define ESP_MODEL_IDENTIFIER  "\x08""ESP32-C6"
```

> **Note**: The `\xNN` prefix is the string length in ZCL format (Pascal string). Update the length byte if the text is changed.

### Timing and Stack Constants

Defined in `main/router.h`; no recompilation needed if adjusted via `Kconfig`:

| Constant | Default Value | Description |
|----------|---------------|-------------|
| `ROUTER_BDB_INIT_RETRY_MS` | `2000` ms | Time between BDB initialization retries |
| `ROUTER_STEERING_RETRY_MS` | `5000` ms | Time between network steering retries |
| `ZIGBEE_MAIN_TASK_STACK_SIZE` | `8192` bytes | Main Zigbee task stack size |

---

## Firmware Architecture

### Main Files

| File | Description |
|------|-------------|
| `main/router.c` | Core logic: initialisation, Zigbee state machine, status LED |
| `main/router.h` | Configuration constants, channel masks and stack initialisation macros |
| `main/Kconfig.projbuild` | Configuration options exported to the build system |
| `main/idf_component.yml` | IDF component dependencies |
| `sdkconfig.defaults` | Minimal ESP-IDF configuration (16 MB flash, Zigbee enabled, DEBUG log) |
| `partitions.csv` | Custom partition table with `zb_storage` partition for Zigbee NVS |

### Initialisation Flow

```
app_main()
  ├── configure_led()           — initialise RGB LED on GPIO 8 (RMT)
  ├── nvs_flash_init()          — general NVS
  ├── nvs_flash_init_partition("zb_storage")  — Zigbee NVS
  └── xTaskCreate(esp_zigbee_stack_main_task, stack=ZIGBEE_MAIN_TASK_STACK_SIZE)
        ├── esp_zigbee_init()
        ├── ezb_bdb_set_primary_channel_set(ESP_ZIGBEE_PRIMARY_CHANNEL_MASK)
        ├── ezb_bdb_set_secondary_channel_set(ESP_ZIGBEE_SECONDARY_CHANNEL_MASK)
        ├── ezb_secur_set_global_link_key()   — standard HA TC Link Key
        ├── ezb_secur_set_tclk_exchange_required(true)
        ├── register_router_endpoint()         — Range Extender, EP 1, device 0x0008
        └── esp_zigbee_launch_mainloop()       — Zigbee main loop
```

### State Machine (BDB signals)

The router manages its network lifecycle via the `esp_zigbee_app_signal_handler` handler. The main states are:

| Signal | Action | LED |
|--------|--------|-----|
| `ZDO_SIGNAL_SKIP_STARTUP` | Launches `BDB_MODE_INITIALIZATION` | 🔴 Red |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + factory-new | Launches `BDB_MODE_NETWORK_STEERING` | 🟡 Amber |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + rejoin | Immediate Device Announce | 🟢 Green |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` failure | Retry after `ROUTER_BDB_INIT_RETRY_MS` | 🔴 Red |
| `BDB_SIGNAL_STEERING` OK | Joined network, sends Device Announce (×2: 3 s and 8 s) | 🟢 Green |
| `BDB_SIGNAL_STEERING` `NO_NETWORK` | Retry after `ROUTER_STEERING_RETRY_MS`, alternates INIT/STEERING | 🟡 Amber |
| `BDB_SIGNAL_STEERING` `NOT_PERMITTED` / `TARGET_FAILURE` | Scheduled retry | 🟡 Amber |
| `BDB_SIGNAL_STEERING` `TCLK_EX_FAILURE` | Warning log, retry | — |
| `NWK_SIGNAL_PERMIT_JOIN_STATUS` active | Permit join open on the PAN | 🔵 Blue |
| `NWK_SIGNAL_PERMIT_JOIN_STATUS` closed | Permit join closed | 🟢 Green |
| `ZDO_SIGNAL_LEAVE` | Device removed from the network | 🔴 Red |
| Unknown signal | Informational log | ⚪ White |

### Steering Retry Logic

When steering fails, the firmware alternates between two retry modes to maximise the chances of joining the network:

- **`EZB_BDB_MODE_NETWORK_STEERING`**: direct join attempt
- **`EZB_BDB_MODE_INITIALIZATION`**: full stack re-initialisation before the next steering attempt

The `retry_with_initialization` variable controls the alternation. A retry is only scheduled if `steering_retry_pending` is `false`, preventing duplicate timer accumulation. Both variables are `_Atomic bool` to ensure visibility between the timer task and the signal handler.

### Zigbee Security

- **Distributed security**: disabled (`ezb_aps_secur_enable_distributed_security(false)`)
- **Trust Center Link Key**: standard ZigBee Alliance HA key (`ZigBeeAlliance09`)
- **TCLK exchange**: mandatory (`ezb_secur_set_tclk_exchange_required(true)`)
- **Minimum LQI for join**: 0 (no filtering, maximum range compatibility)

> ⚠️ The standard TC Link Key is public and well-known. For production networks with high security requirements, consider using a custom install code policy (`install_code_policy = true`).

---

## Status LED — Colour Table

| Colour | R | G | B | State |
|--------|---|---|---|-------|
| 🔴 Red | 16 | 0 | 0 | No network / critical error / device removed from network |
| 🟡 Amber | 30 | 7 | 0 | Searching for network / join rejected / device alive but networkless |
| 🟢 Green | 0 | 16 | 0 | Connected to Zigbee network |
| 🔵 Blue | 0 | 0 | 16 | Permit join active on the PAN |
| ⚪ White | 16 | 16 | 16 | Unknown Zigbee signal (diagnostic) |

**Colour semantics:**
- 🔴 **Red** — critical error or total absence of network (device not operational)
- 🟡 **Amber** — warning / in progress: device is alive and actively retrying
- 🟢 **Green** — nominal state: router is integrated into the network and routing traffic
- 🔵 **Blue** — informational: the PAN pairing window is open
- ⚪ **White** — diagnostic: Zigbee signal not recognised by the firmware

---

## Registered Zigbee Endpoint

| Parameter | Value |
|-----------|-------|
| Endpoint ID | 1 (`ESP_ZIGBEE_RANGE_EXTENDER_EP_ID`) |
| Profile | HA (`EZB_AF_HA_PROFILE_ID` / `0x0104`) |
| Device ID | Range Extender (`0x0008`) |
| Power Source (Basic cluster) | `SINGLE_PHASE_MAINS` (`0x01`) |
| Server clusters | Basic (with `ManufacturerName` and `ModelIdentifier`) |

> The endpoint registers only the Basic cluster with the mandatory identification attributes. It does not include application clusters (On/Off, etc.) since the router acts exclusively as an infrastructure node.

---

## Partition Table

Configured in `partitions.csv` with a dedicated `zb_storage` partition for persistent storage of the Zigbee stack (network keys, address, channel). This ensures the device remembers its network after a power cycle without needing to re-pair.

---

## Troubleshooting

| Symptom | Probable Cause | Action |
|---------|----------------|--------|
| Constant 🟡 amber LED | Coordinator not in permit join or out of range | Open permit join on the coordinator and check distance |
| 🟡 amber alternating with 🔴 red | STEERING → INITIALIZATION cycle in progress | Normal if coordinator is unavailable; wait or open permit join |
| `TCLK_EX_FAILURE` in logs | Incompatible coordinator security policy | Verify the coordinator accepts the standard TC Link Key |
| `NOT_PERMITTED` in logs | Permit join closed on the coordinator | Open permit join (60–254 s) |
| WDT reset during steering | Insufficient stack in the Zigbee task | Verify `ZIGBEE_MAIN_TASK_STACK_SIZE` ≥ 8192 bytes |
| Device not visible in coordinator UI | Incorrect `power_source` in Basic cluster | Verify `power_source = SINGLE_PHASE_MAINS` |
| Router does not scan all channels | Primary mask too restrictive | Check `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK`; secondary fallback will scan 11–26 |

---

## License

CC0-1.0 — see source file headers.
