# esp32c6-zigbee-router

> 🇬🇧 [English version: Docs/LNG/EN/README.en.md](Docs/LNG/EN/README.en.md)

Firmware ESP-IDF para el ESP32-C6 que implementa un **router Zigbee puro** con LED RGB de estado y gestos de control mediante el botón BOOT. Actúa como nodo intermedio en una red Zigbee HA (Home Automation), extendiendo el alcance y reenrutando tráfico entre dispositivos finales y el coordinador.

---

## Hardware soportado

| Componente | Especificación |
|-----------|----------------|
| SoC | ESP32-C6 (IEEE 802.15.4 nativo) |
| Flash | 16 MB |
| LED de estado | LED RGB direccionable (WS2812 o compatible) en GPIO 8 vía RMT |
| Framework | ESP-IDF ≥ 6.2.0 |
| SDK Zigbee | `esp-zigbee-lib` ≥ 2.0.0 (API `ezb_*`) |

---

## Pinout — ESP32-C6-DevKitC-1 (módulo WROOM-1 N6)

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

### Tabla de funciones GPIO

| GPIO | Pin | Función principal | Funciones alternativas | Notas |
|------|-----|-------------------|------------------------|-------|
| IO0  |  7  | GPIO              | XTAL_32K_P, LP_GPIO0, ADC1_CH0 | Pin de strapping |
| IO1  |  8  | GPIO              | XTAL_32K_N, LP_GPIO1, ADC1_CH1 | |
| IO2  | 12  | SPI FSPIQ         | LP_GPIO2, ADC1_CH2 | |
| IO3  | 13  | GPIO              | LP_GPIO3, ADC1_CH3 | |
| IO4  |  3  | JTAG MTMS / I2C SDA | LP_GPIO4, LP_UART_RXD, ADC1_CH4, FSPIHD | Pin de strapping |
| IO5  |  4  | JTAG MTDI / I2C SCL | LP_GPIO5, LP_UART_TXD, ADC1_CH5, FSPIWP | Pin de strapping |
| IO6  |  5  | JTAG MTCK         | LP_GPIO6, LP_I2C_SDA, ADC1_CH6, FSPICLK | |
| IO7  |  6  | JTAG MTDO         | LP_GPIO7, LP_I2C_SCL, FSPID | |
| **IO8**  |  **9**  | **LED WS2812 (este fw)** | GPIO | **Pin de strapping — usado por este firmware** |
| **IO9**  | 24  | **Botón BOOT**    | ADC2_CH1 | Pin de strapping — botón BOOT, libre en runtime |
| IO10 | 10  | GPIO              | — | |
| IO11 | 11  | GPIO / SPI Flash  | — | Puede estar reservado para flash interno |
| IO12 | 26  | USB JTAG D−       | — | USB nativo (no usar si USB CDC está activo) |
| IO13 | 27  | USB JTAG D+       | — | USB nativo (no usar si USB CDC está activo) |
| IO15 | 28  | GPIO              | — | Pin de strapping |
| IO16 | 17  | GPIO              | — | |
| IO17 | 23  | UART0 TX          | FSPICS0 | Conectado al chip USB–UART (CH343) |
| IO18 | 22  | UART0 RX          | SDIO_CMD, FSPICS2 | Conectado al chip USB–UART (CH343) |
| IO19 | 30  | USB D−            | SDIO_CLK, FSPICS3 | USB nativo |
| IO20 | 31  | USB D+            | SDIO_DATA0, FSPICS4 | USB nativo |
| IO21 | 32  | GPIO / SPI CS     | SDIO_DATA1, FSPICS5 | |
| IO22 | 33  | GPIO / SPI MOSI   | SDIO_DATA2 | |
| IO23 | 34  | GPIO / SPI MISO   | SDIO_DATA3 | |

### Notas de uso

- ⚠️ **Pines de strapping** (IO0, IO4, IO5, IO8, IO9, IO15): el nivel lógico en el momento del reset determina el modo de arranque.
- 🔴 **IO8** está conectado al **LED WS2812 de la placa** en el DevKitC-1.
- 🟡 **IO4–IO7**: pines JTAG. Disponibles como GPIO cuando no se usa un depurador hardware.
- 🔵 **IO12–IO13, IO19–IO20**: pines del bus USB nativo del SoC. Reservados si se usa USB CDC/JTAG.
- ⚪ **IO17/IO18**: consola serie (`idf.py monitor`).
- 🔘 **IO9 (BOOT)**: durante el arranque determina el modo de flash. En ejecución, usado por `button.c`.

---

## Requisitos de compilación

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/) ≥ 6.2.0 con soporte ESP32-C6
- `esp-zigbee-lib` ≥ 2.0.0 — SDK Zigbee de alto nivel con API `ezb_*`
- `alarm_timer` — timers diferidos no bloqueantes (obtenido automáticamente desde el repositorio `esp-zigbee-sdk`)
- `led_strip` ≥ 3.0.0 — driver WS2812 vía RMT

```bash
idf.py reconfigure
```

> **Nota de migración**: si compilabas con `esp-zigbee-sdk` 1.x, borra el directorio `build/` y la caché de componentes antes de recompilar.

---

## Compilar y flashear

```bash
idf.py set-target esp32c6
idf.py build
idf.py flash monitor
```

---

## Configuración

Parámetros editables vía `Kconfig` en `main/Kconfig.projbuild`:

| Parámetro Kconfig | Descripción | Valor por defecto |
|-------------------|-------------|-------------------|
| `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK` | Canales primarios: 15, 20, 25 | `0x02108000` |
| `ESP_ZIGBEE_SECONDARY_CHANNEL_MASK` | Canales secundarios: 11–26 completo | `0x07FFF800` |

Constantes en `main/router.h`:

| Constante | Valor | Descripción |
|----------|-------|-------------|
| `ROUTER_BDB_INIT_RETRY_MS` | `2000` ms | Reintento de inicialización BDB |
| `ROUTER_STEERING_RETRY_MS` | `5000` ms | Reintento de network steering |
| `ZIGBEE_MAIN_TASK_STACK_SIZE` | `10240` bytes | Stack de la tarea principal Zigbee |
| `ROUTER_TX_POWER_LOW_DBM` | `8` dBm | Potencia RF normal |
| `ROUTER_TX_POWER_HIGH_DBM` | `20` dBm | Potencia RF boost (triple-tap) |

---

## Arquitectura del firmware

### Ficheros principales

| Fichero | Descripción |
|---------|-------------|
| `main/router.c` | Lógica principal: inicialización, máquina de estados Zigbee, LED de estado |
| `main/router.h` | Constantes de configuración, máscaras de canal, macros del stack |
| `main/button.c` | Gestos del botón BOOT (GPIO9): ISR + timers FreeRTOS, toggle TX, factory reset |
| `main/button.h` | API pública: solo `button_init()` |
| `main/idf_component.yml` | Dependencias de componentes IDF |
| `sdkconfig.defaults` | Configuración mínima ESP-IDF |
| `partitions.csv` | Tabla de particiones con `zb_storage` |

### Flujo de inicialización

```
app_main()
  ├── configure_led()                        — GPIO 8 (RMT)
  ├── button_init()                          — ISR GPIO9 + timers FreeRTOS
  ├── nvs_flash_init()
  ├── nvs_flash_init_partition("zb_storage")
  └── xTaskCreate(esp_zigbee_stack_main_task)
        ├── esp_zigbee_init()
        ├── esp_ieee802154_set_txpower(8 dBm)
        ├── ezb_bdb_set_primary_channel_set()
        ├── ezb_bdb_set_secondary_channel_set()
        ├── ezb_secur_set_global_link_key()
        ├── register_router_endpoint()        — EP 1, device 0x0008
        └── esp_zigbee_launch_mainloop()
```

### Máquina de estados (señales BDB)

| Señal | Acción | LED |
|-------|--------|-----|
| `EZB_ZDO_SIGNAL_SKIP_STARTUP` | Lanza `EZB_BDB_MODE_INITIALIZATION` | 🔴 Rojo |
| `EZB_BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + factory-new | Lanza `EZB_BDB_MODE_NETWORK_STEERING` | 🟡 Ámbar |
| `EZB_BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + rejoin | Device Announce inmediato | 🟢 Verde |
| `EZB_BDB_SIGNAL_STEERING` OK | Unido; Device Announce ×2 (3 s, 8 s) | 🟢 Verde |
| `EZB_BDB_SIGNAL_STEERING` `NO_NETWORK` | Reintento, alterna INIT/STEERING | 🟡 Ámbar |
| `EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS` activo | Ventana de emparejamiento abierta | 🔵 Azul |
| `EZB_ZDO_SIGNAL_LEAVE` | Eliminado de la red | 🔴 Rojo |

### Seguridad Zigbee

- Seguridad distribuida deshabilitada
- TC Link Key estándar HA (`ZigBeeAlliance09`)
- TCLK exchange obligatorio
- Install code policy habilitada

> ⚠️ El TC Link Key estándar es público. Para redes de producción considerar install codes completos.

---

## Botón BOOT (GPIO9)

| Gesto | Umbral | Acción | Feedback LED |
|-------|--------|--------|-------------|
| Triple toque | < 500 ms entre pulsaciones | Toggle TX: 8 dBm ↔ 20 dBm | 3× rojo brillante (boost) / 3× azul suave (normal) |
| Mantener 5 s | GPIO LOW 5 s | Factory reset + reboot | Magenta parpadeante → rojo → reboot |

---

## Potencia TX (IEEE 802.15.4)

| Modo | Valor | Cuándo usar |
|------|-------|-------------|
| Normal (defecto) | **8 dBm** | Operación diaria |
| Boost (triple-tap) | **20 dBm** | Nodo lejano / troubleshooting |

El modo boost es **volátil**: revierte a 8 dBm al reiniciar.

---

## LED de estado

### Colores de estado de red

| Color | R | G | B | Estado |
|-------|---|---|---|--------|
| 🔴 Rojo suave | 64 | 0 | 0 | Sin red / error / eliminado |
| 🟡 Ámbar | 30 | 7 | 0 | Buscando red / join rechazado |
| 🟢 Verde | 0 | 16 | 0 | Conectado |
| 🔵 Azul | 0 | 0 | 16 | Permit join activo |

### Colores de feedback de gestos

| Color | R | G | B | Significado |
|-------|---|---|---|-------------|
| 🔴 Rojo brillante | 255 | 0 | 0 | TX boost (20 dBm) |
| 🔵 Azul suave | 0 | 0 | 64 | TX normal (8 dBm) |
| 🟣 Magenta | 255 | 0 | 255 | Factory reset en curso |

---

## Endpoint Zigbee registrado

| Parámetro | Valor |
|-----------|-------|
| Endpoint ID | 1 |
| Perfil | HA (`0x0104`) |
| Device ID | Range Extender (`0x0008`) |
| Power Source | `SINGLE_PHASE_MAINS` |
| Clusters servidor | Basic (`ManufacturerName`, `ModelIdentifier`) |

---

## Tabla de particiones

Partición `zb_storage` dedicada al NVS del stack Zigbee. Garantiza que el dispositivo recuerde su red tras un ciclo de alimentación.

---

## Versiones

| Versión | Fecha | Resumen |
|---------|-------|---------|
| **v3.0.0** | 2026-06-17 | Migración a `esp-zigbee-lib` ≥ 2.0.0 (API `ezb_*`). Breaking change de compilación. |
| **v2.0.0** | 2026-06-16 | Botón BOOT: triple-tap toggle TX, hold 5 s factory reset. |
| **v1.0.0** | 2026-06-15 | Router Zigbee funcional, LED RGB, steering retry. |

Historial completo: [CHANGELOG.md](CHANGELOG.md)

---

## Resolución de problemas

| Síntoma | Causa probable | Acción |
|---------|----------------|--------|
| Error `ezb_*` undeclared | SDK 1.x instalado | Borrar `build/`, ejecutar `idf.py reconfigure` con SDK ≥ 2.0.0 |
| LED 🟡 ámbar constante | Coordinador sin permit join | Abrir permit join y verificar distancia |
| `TCLK_EX_FAILURE` | Política de seguridad incompatible | Verificar TC Link Key en el coordinador |
| `NOT_PERMITTED` | Permit join cerrado | Abrir permit join (60–254 s) |
| Reset por WDT | Stack insuficiente | Verificar `ZIGBEE_MAIN_TASK_STACK_SIZE` ≥ 10240 |
| Triple-tap sin respuesta | Pulsaciones lentas | < 500 ms entre pulsaciones |
| Factory reset no borra | Error en `nvs_flash_erase_partition` | Revisar log y nombre de partición |

---

## Documentación adicional

- 🇬🇧 [English README](Docs/LNG/EN/README.en.md)
- [CHANGELOG.md](CHANGELOG.md)
- [CONTRIBUTING.md](CONTRIBUTING.md)

---

## Licencia

Código fuente original bajo **CC0-1.0** (dominio público). Ver cabeceras SPDX en los ficheros fuente.

| Dependencia | Licencia |
|-------------|----------|
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache-2.0 |
| [esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk) | Apache-2.0 |
| [esp-zboss-lib](https://github.com/espressif/esp-zboss-lib) | [Licencia Espressif](https://github.com/espressif/esp-zboss-lib/blob/master/LICENSE) |

Para contribuir, ver [CONTRIBUTING.md](CONTRIBUTING.md).
