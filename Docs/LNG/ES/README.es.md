# esp32c6-zigbee-router

> 🇬🇧 English version: [README.md](README.md)

Firmware ESP-IDF para ESP32-C6 que implementa un **router Zigbee puro** con LED RGB de estado. Actúa como nodo intermediario en una red Zigbee HA (Home Automation), extendiendo el alcance de la red y re-enrutando tráfico entre dispositivos finales y el coordinador.

---

## Hardware soportado

| Componente | Especificación |
|------------|---------------|
| SoC | ESP32-C6 (IEEE 802.15.4 nativo) |
| Flash | 16 MB |
| LED estado | LED RGB addressable (WS2812 o similar) en GPIO 8 vía RMT |
| Framework | ESP-IDF ≥ 6.2.0 |

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
| IO0  |  7  | GPIO               | XTAL_32K_P, LP_GPIO0, ADC1_CH0 | Strapping pin |
| IO1  |  8  | GPIO               | XTAL_32K_N, LP_GPIO1, ADC1_CH1 | |
| IO2  | 12  | SPI FSPIQ          | LP_GPIO2, ADC1_CH2 | |
| IO3  | 13  | GPIO               | LP_GPIO3, ADC1_CH3 | |
| IO4  |  3  | JTAG MTMS / I2C SDA| LP_GPIO4, LP_UART_RXD, ADC1_CH4, FSPIHD | Strapping pin |
| IO5  |  4  | JTAG MTDI / I2C SCL| LP_GPIO5, LP_UART_TXD, ADC1_CH5, FSPIWP | Strapping pin |
| IO6  |  5  | JTAG MTCK          | LP_GPIO6, LP_I2C_SDA, ADC1_CH6, FSPICLK | |
| IO7  |  6  | JTAG MTDO          | LP_GPIO7, LP_I2C_SCL, FSPID | |
| **IO8**  |  **9**  | **LED WS2812 (este fw)** | GPIO | **Strapping pin — usado por este firmware** |
| IO9  | 24  | BOOT button        | ADC2_CH1 | Strapping pin — botón BOOT |
| IO10 | 10  | GPIO               | — | |
| IO11 | 11  | GPIO / SPI Flash   | — | Puede estar reservado para flash interna |
| IO12 | 26  | USB JTAG D−        | — | USB nativo (no usar si se usa USB CDC) |
| IO13 | 27  | USB JTAG D+        | — | USB nativo (no usar si se usa USB CDC) |
| IO15 | 28  | GPIO               | — | Strapping pin |
| IO16 | 17  | GPIO               | — | |
| IO17 | 23  | UART0 TX           | FSPICS0 | Conectado al chip USB–UART (CH343) |
| IO18 | 22  | UART0 RX           | SDIO_CMD, FSPICS2 | Conectado al chip USB–UART (CH343) |
| IO19 | 30  | USB D−             | SDIO_CLK, FSPICS3 | USB nativo |
| IO20 | 31  | USB D+             | SDIO_DATA0, FSPICS4 | USB nativo |
| IO21 | 32  | GPIO / SPI CS      | SDIO_DATA1, FSPICS5 | |
| IO22 | 33  | GPIO / SPI MOSI    | SDIO_DATA2 | |
| IO23 | 34  | GPIO / SPI MISO    | SDIO_DATA3 | |

### Notas de uso

- ⚠️ **Strapping pins** (IO0, IO4, IO5, IO8, IO9, IO15): el nivel en el momento del reset determina el modo de arranque. Dejar sin conectar o con resistencia pull-up/pull-down adecuada.
- 🔴 **IO8** está conectado al **LED WS2812 onboard** en el DevKitC-1 y es el pin utilizado por este firmware para el LED de estado.
- 🟡 **IO4–IO7**: pines JTAG. Disponibles para GPIO si no se usa el depurador por hardware.
- 🔵 **IO12–IO13, IO19–IO20**: pines del bus USB nativo del SoC. Reservados si se usa USB CDC/JTAG.
- ⚪ **IO17/IO18**: conectados internamente al chip USB–UART (CH343). Son los pines de la consola serie (`idf.py monitor`).
- 🟢 **IO11**: puede estar reservado para la flash interna en variantes con OSPI. Comprobar el esquemático de la placa concreta.

---

## Requisitos de construcción

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/) ≥ 6.2.0 con soporte para ESP32-C6
- Componente `ezbee` (wrapper de alto nivel sobre `esp-zigbee-sdk`)
- Componente `alarm_timer` (timers diferidos sin bloqueo)
- Componente `led_strip` (driver WS2812 vía RMT)

Instalar dependencias declaradas en `main/idf_component.yml`:

```bash
idf.py reconfigure
```

---

## Compilar y flashear

```bash
# Configurar target
idf.py set-target esp32c6

# Compilar
idf.py build

# Flashear y monitorizar
idf.py flash monitor
```

---

## Configuración

El proyecto utiliza `sdkconfig.defaults` y `sdkconfig.defaults.esp32c6` con valores preconfigurados. Los parámetros editables mediante `Kconfig` se encuentran en `main/Kconfig.projbuild`:

| Parámetro Kconfig | Descripción | Valor por defecto |
|-------------------|-------------|-------------------|
| `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK` | Canales primarios: 15, 20, 25 (mínimo solapamiento con WiFi 2.4 GHz en EU) | `0x02108000` |
| `ESP_ZIGBEE_SECONDARY_CHANNEL_MASK` | Canales secundarios: scan completo 11–26 (fallback) | `0x07FFF800` |

Para personalizar el nombre de fabricante y modelo del dispositivo, editar `main/router.h`:

```c
#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"
#define ESP_MODEL_IDENTIFIER  "\x08""ESP32-C6"
```

> **Nota**: El prefijo `\xNN` es la longitud de la cadena en formato ZCL (Pascal string). Actualizar el byte de longitud si se cambia el texto.

### Constantes de temporización y stack

Definidas en `main/router.h`, sin necesidad de recompilar si se ajustan vía `Kconfig`:

| Constante | Valor por defecto | Descripción |
|-----------|-------------------|-------------|
| `ROUTER_BDB_INIT_RETRY_MS` | `2000` ms | Tiempo entre reintentos de BDB initialization |
| `ROUTER_STEERING_RETRY_MS` | `5000` ms | Tiempo entre reintentos de network steering |
| `ZIGBEE_MAIN_TASK_STACK_SIZE` | `8192` bytes | Stack de la tarea principal Zigbee |

---

## Arquitectura del firmware

### Ficheros principales

| Fichero | Descripción |
|---------|-------------|
| `main/router.c` | Lógica principal: inicialización, máquina de estados Zigbee, LED de estado |
| `main/router.h` | Constantes de configuración, máscaras de canal y macros de inicialización de la stack |
| `main/Kconfig.projbuild` | Opciones de configuración exportadas al sistema de build |
| `main/idf_component.yml` | Dependencias de componentes IDF |
| `sdkconfig.defaults` | Configuración mínima de ESP-IDF (flash 16 MB, Zigbee habilitado, log DEBUG) |
| `partitions.csv` | Tabla de particiones personalizada con partición `zb_storage` para NVS Zigbee |

### Flujo de inicialización

```
app_main()
  ├── configure_led()           — inicializa LED RGB en GPIO 8 (RMT)
  ├── nvs_flash_init()          — NVS general
  ├── nvs_flash_init_partition("zb_storage")  — NVS Zigbee
  └── xTaskCreate(esp_zigbee_stack_main_task, stack=ZIGBEE_MAIN_TASK_STACK_SIZE)
        ├── esp_zigbee_init()
        ├── ezb_bdb_set_primary_channel_set(ESP_ZIGBEE_PRIMARY_CHANNEL_MASK)
        ├── ezb_bdb_set_secondary_channel_set(ESP_ZIGBEE_SECONDARY_CHANNEL_MASK)
        ├── ezb_secur_set_global_link_key()   — TC Link Key estándar HA
        ├── ezb_secur_set_tclk_exchange_required(true)
        ├── register_router_endpoint()         — Range Extender, EP 1, device 0x0008
        └── esp_zigbee_launch_mainloop()       — loop principal Zigbee
```

### Máquina de estados (señales BDB)

El router gestiona su ciclo de vida de red mediante el manejador `esp_zigbee_app_signal_handler`. Los estados principales son:

| Señal | Acción | LED |
|-------|--------|-----|
| `ZDO_SIGNAL_SKIP_STARTUP` | Lanza `BDB_MODE_INITIALIZATION` | 🔴 Rojo |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + factory-new | Lanza `BDB_MODE_NETWORK_STEERING` | 🟡 Ámbar |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + rejoin | Device Announce inmediato | 🟢 Verde |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` fallo | Reintento en `ROUTER_BDB_INIT_RETRY_MS` | 🔴 Rojo |
| `BDB_SIGNAL_STEERING` OK | Unido a red, envía Device Announce (×2: 3 s y 8 s) | 🟢 Verde |
| `BDB_SIGNAL_STEERING` `NO_NETWORK` | Reintento en `ROUTER_STEERING_RETRY_MS`, alterna INIT/STEERING | 🟡 Ámbar |
| `BDB_SIGNAL_STEERING` `NOT_PERMITTED` / `TARGET_FAILURE` | Reintento programado | 🟡 Ámbar |
| `BDB_SIGNAL_STEERING` `TCLK_EX_FAILURE` | Log de advertencia, reintento | — |
| `NWK_SIGNAL_PERMIT_JOIN_STATUS` activo | Permit join abierto en la PAN | 🔵 Azul |
| `NWK_SIGNAL_PERMIT_JOIN_STATUS` cerrado | Permit join cerrado | 🟢 Verde |
| `ZDO_SIGNAL_LEAVE` | Dispositivo expulsado de la red | 🔴 Rojo |
| Señal desconocida | Log informativo | ⚪ Blanco |

### Lógica de reintentos de steering

Cuando el steering falla, el firmware alterna entre dos modos de reintento para maximizar las posibilidades de unirse a la red:

- **`EZB_BDB_MODE_NETWORK_STEERING`**: intento de join directo
- **`EZB_BDB_MODE_INITIALIZATION`**: re-inicialización completa de la stack antes del siguiente steering

La variable `retry_with_initialization` controla la alternancia. Un retry sólo se programa si `steering_retry_pending` es `false`, evitando acumulación de timers duplicados. Ambas variables son `_Atomic bool` para garantizar visibilidad entre la tarea del timer y el signal handler.

### Seguridad Zigbee

- **Distributed security**: desactivado (`ezb_aps_secur_enable_distributed_security(false)`)
- **Trust Center Link Key**: clave estándar ZigBee Alliance HA (`ZigBeeAlliance09`)
- **TCLK exchange**: obligatorio (`ezb_secur_set_tclk_exchange_required(true)`)
- **LQI mínimo para join**: 0 (sin filtrado, máxima compatibilidad de rango)

> ⚠️ La TC Link Key estándar es pública y conocida. Para redes de producción con requisitos de seguridad elevados, considerar una instalación con clave de instalación personalizada (`install_code_policy = true`).

---

## LED de estado — tabla de colores

| Color | R | G | B | Estado |
|-------|---|---|---|--------|
| 🔴 Rojo | 16 | 0 | 0 | Sin red / error crítico / dispositivo expulsado de la red |
| 🟡 Ámbar | 30 | 7 | 0 | Buscando red / join rechazado / dispositivo vivo sin red |
| 🟢 Verde | 0 | 16 | 0 | Conectado a la red Zigbee |
| 🔵 Azul | 0 | 0 | 16 | Permit join activo en la PAN |
| ⚪ Blanco | 16 | 16 | 16 | Señal Zigbee desconocida (diagnóstico) |

**Semántica de colores:**
- 🔴 **Rojo** — error crítico o ausencia total de red (dispositivo no operativo)
- 🟡 **Ámbar** — advertencia / en proceso: el dispositivo está vivo y reintentando activamente
- 🟢 **Verde** — estado nominal: el router está integrado en la red y enrutando tráfico
- 🔵 **Azul** — informativo: la ventana de emparejamiento de la PAN está abierta
- ⚪ **Blanco** — diagnóstico: señal Zigbee no reconocida por el firmware

---

## Endpoint Zigbee registrado

| Parámetro | Valor |
|-----------|-------|
| Endpoint ID | 1 (`ESP_ZIGBEE_RANGE_EXTENDER_EP_ID`) |
| Profile | HA (`EZB_AF_HA_PROFILE_ID` / `0x0104`) |
| Device ID | Range Extender (`0x0008`) |
| Power Source (Basic cluster) | `SINGLE_PHASE_MAINS` (`0x01`) |
| Clusters servidor | Basic (con `ManufacturerName` y `ModelIdentifier`) |

> El endpoint registra únicamente el cluster Basic con los atributos de identificación obligatorios. No incluye clusters de aplicación (On/Off, etc.) ya que el router actúa exclusivamente como nodo de infraestructura.

---

## Tabla de particiones

Configurada en `partitions.csv` con partición dedicada `zb_storage` para el almacenamiento persistente de la stack Zigbee (claves de red, dirección, canal). Esto garantiza que el dispositivo recuerde su red tras un reinicio de alimentación sin necesitar re-emparejamiento.

---

## Solución de problemas frecuentes

| Síntoma | Causa probable | Acción |
|---------|---------------|--------|
| LED 🟡 ámbar constante | Coordinador no en permit join o fuera de rango | Abrir permit join en el coordinador y verificar distancia |
| LED 🟡 ámbar alternando con 🔴 rojo | Ciclo STEERING → INITIALIZATION en curso | Normal si el coordinador no está disponible; esperar o abrir permit join |
| `TCLK_EX_FAILURE` en logs | Política de seguridad del coordinador incompatible | Verificar que el coordinador acepta la TC Link Key estándar |
| `NOT_PERMITTED` en logs | Permit join cerrado en el coordinador | Abrir permit join (60–254 s) |
| WDT reset en steering | Stack insuficiente en la tarea Zigbee | Verificar que `ZIGBEE_MAIN_TASK_STACK_SIZE` ≥ 8192 bytes |
| Dispositivo no visible en la interfaz del coordinador | `power_source` incorrecto en Basic cluster | Verificar que `power_source = SINGLE_PHASE_MAINS` |
| Router no escanea todos los canales | Máscara primaria demasiado restrictiva | Verificar `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK`; el fallback secundario escaneará 11–26 |

---

## Licencia

CC0-1.0 — ver cabeceras de ficheros fuente.
