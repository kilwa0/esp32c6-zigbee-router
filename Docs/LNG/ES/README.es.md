# esp32c6-zigbee-router

> 🇬🇧 English version: [README.md](../../../README.md)

Firmware ESP-IDF para el ESP32-C6 que implementa un **router Zigbee puro** con LED RGB de estado y gestos de control mediante el botón BOOT. Actúa como nodo intermediario en una red Zigbee HA (Home Automation), extendiendo el alcance de la red y reenrutando tráfico entre dispositivos finales y el coordinador.

---

## Hardware soportado

| Componente | Especificación |
|------------|---------------|
| SoC | ESP32-C6 (IEEE 802.15.4 nativo) |
| Flash | 16 MB |
| LED de estado | LED RGB direccionable (WS2812 o compatible) en GPIO 8 vía RMT |
| Framework | ESP-IDF ≥ 6.2.0 |
| Zigbee SDK | `esp-zigbee-lib` ≥ 2.0.0 (API `ezb_*`) |

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
|------|-----|--------------------|------------------------|-------|
| IO0  |  7  | GPIO | XTAL_32K_P, LP_GPIO0, ADC1_CH0 | Pin de strapping |
| IO1  |  8  | GPIO | XTAL_32K_N, LP_GPIO1, ADC1_CH1 | |
| IO2  | 12  | SPI FSPIQ | LP_GPIO2, ADC1_CH2 | |
| IO3  | 13  | GPIO | LP_GPIO3, ADC1_CH3 | |
| IO4  |  3  | JTAG MTMS / I2C SDA | LP_GPIO4, LP_UART_RXD, ADC1_CH4, FSPIHD | Pin de strapping |
| IO5  |  4  | JTAG MTDI / I2C SCL | LP_GPIO5, LP_UART_TXD, ADC1_CH5, FSPIWP | Pin de strapping |
| IO6  |  5  | JTAG MTCK | LP_GPIO6, LP_I2C_SDA, ADC1_CH6, FSPICLK | |
| IO7  |  6  | JTAG MTDO | LP_GPIO7, LP_I2C_SCL, FSPID | |
| **IO8** | **9** | **LED WS2812 (este fw)** | GPIO | **Pin de strapping — usado por este firmware** |
| **IO9** | 24 | **Botón BOOT** | ADC2_CH1 | Pin de strapping — libre en runtime |
| IO10 | 10  | GPIO | — | |
| IO11 | 11  | GPIO / SPI Flash | — | Puede estar reservado para flash interna |
| IO12 | 26  | USB JTAG D− | — | USB nativo (no usar si USB CDC activo) |
| IO13 | 27  | USB JTAG D+ | — | USB nativo (no usar si USB CDC activo) |
| IO15 | 28  | GPIO | — | Pin de strapping |
| IO16 | 17  | GPIO | — | |
| IO17 | 23  | UART0 TX | FSPICS0 | Conectado al chip USB–UART (CH343) |
| IO18 | 22  | UART0 RX | SDIO_CMD, FSPICS2 | Conectado al chip USB–UART (CH343) |
| IO19 | 30  | USB D− | SDIO_CLK, FSPICS3 | USB nativo |
| IO20 | 31  | USB D+ | SDIO_DATA0, FSPICS4 | USB nativo |
| IO21 | 32  | GPIO / SPI CS | SDIO_DATA1, FSPICS5 | |
| IO22 | 33  | GPIO / SPI MOSI | SDIO_DATA2 | |
| IO23 | 34  | GPIO / SPI MISO | SDIO_DATA3 | |

### Notas de uso

- ⚠️ **Pines de strapping** (IO0, IO4, IO5, IO8, IO9, IO15): el nivel lógico en el momento del reset determina el modo de arranque.
- 🔴 **IO8** está conectado al **LED WS2812 de la placa** en el DevKitC-1 y es el pin utilizado por este firmware para el LED de estado.
- 🟡 **IO4–IO7**: pines JTAG. Disponibles como GPIO cuando no se usa un depurador hardware.
- 🔵 **IO12–IO13, IO19–IO20**: pines del bus USB nativo del SoC. Reservados si se usa USB CDC/JTAG.
- ⚪ **IO17/IO18**: conectados internamente al chip USB–UART (CH343). Son los pines de la consola serie (`idf.py monitor`).
- 🟢 **IO11**: puede estar reservado para flash interna en variantes OSPI. Consultar el esquemático de la placa específica.
- 🔘 **IO9 (BOOT)**: durante el arranque determina el modo de flash (HIGH = boot normal). Una vez que el firmware está en ejecución, es un GPIO de propósito ge