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
| Framework | ESP-IDF ≥ 5.3.5 |
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
- 🔘 **IO9 (BOOT)**: durante el arranque determina el modo de flash (HIGH = boot normal). Una vez que el firmware está en ejecución, es un GPIO de propósito general utilizado por el módulo `button.c` para los gestos de control.

---

## Requisitos de compilación

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/) ≥ 5.3.5 con soporte ESP32-C6
- Componente `ezbee` (wrapper de alto nivel sobre `esp-zigbee-lib`)
- Componente `alarm_timer` (timers diferidos no bloqueantes)
- Componente `led_strip` (driver WS2812 vía RMT)

Instalar las dependencias declaradas en `main/idf_component.yml`:

```bash
idf.py reconfigure
```

---

## Compilar y flashear

```bash
# Establecer target
idf.py set-target esp32c6

# Compilar
idf.py build

# Flashear y monitorizar
idf.py flash monitor
```

---

## Configuración

El proyecto utiliza `sdkconfig.defaults` y `sdkconfig.defaults.esp32c6` con valores preconfigurados. Los parámetros editables vía `Kconfig` están en `main/Kconfig.projbuild`:

| Parámetro Kconfig | Descripción | Valor por defecto |
|-------------------|-------------|-------------------|
| `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK` | Canales primarios: 15, 20, 25 (mínimo solapamiento con Wi-Fi 2.4 GHz en la UE) | `0x02108000` |
| `ESP_ZIGBEE_SECONDARY_CHANNEL_MASK` | Canales secundarios: escaneo completo 11–26 (fallback) | `0x07FFF800` |

Para personalizar el nombre del fabricante y modelo del dispositivo, editar `main/router.h`:

```c
#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"
#define ESP_MODEL_IDENTIFIER  "\x08""ESP32-C6"
```

> **Nota**: El prefijo `\xNN` es la longitud de la cadena en formato ZCL (Pascal string). Actualizar el byte de longitud si se modifica el texto.

### Constantes de tiempo, stack y potencia RF

Definidas en `main/router.h`:

| Constante | Valor por defecto | Descripción |
|----------|-------------------|-------------|
| `ROUTER_BDB_INIT_RETRY_MS` | `2000` ms | Tiempo entre reintentos de inicialización BDB |
| `ROUTER_STEERING_RETRY_MS` | `5000` ms | Tiempo entre reintentos de network steering |
| `ROUTER_PERMIT_JOIN_DURATION_S` | `60` s | Duración de la ventana Permit Join abierta por doble-tap |
| `ROUTER_JOIN_OPEN_DURATION_S` | `255` s | Duración enviada al coordinador al abrir una ventana de join a nivel de red |
| `ROUTER_MAX_CHILDREN` | `6` | Número máximo de dispositivos hijo que acepta el router |
| `ZIGBEE_MAIN_TASK_STACK_SIZE` | `10240` bytes | Tamaño del stack de la tarea principal Zigbee |
| `ROUTER_TX_POWER_LOW_DBM` | `8` dBm | Potencia RF de trabajo normal |
| `ROUTER_TX_POWER_HIGH_DBM` | `20` dBm | Potencia RF en modo boost (triple-tap) |

---

## Actualizaciones de Firmware OTA

El firmware incluye una implementación de **ZCL OTA (Over-The-Air) Upgrade Client** que permite al router recibir y aplicar actualizaciones de firmware desde un coordinador o servidor Zigbee.

### Formato de imagen soportado

- **Imagen de Actualización ZCL OTA** (Especificación ZCL Revisión 23)
- Parser TLV en streaming personalizado (`ota_file_parser.c/h`) para análisis eficiente en memoria de bloques de datos de payload OTA

### Tabla de particiones

Se requiere una partición `ota_0` dedicada (**940 KB**) en `partitions.csv` para almacenar la imagen OTA entrante:

```csv
ota_0,    data,   fat,      0,   0x94000,
```

### Configuración

El cliente OTA espera imágenes de actualización con parámetros de identidad coincidentes. Editar `main/router.h`:

```c
#define ESP_OTA_MANUFACTURER_ID  0x1234   // ID del fabricante
#define ESP_OTA_IMAGE_TYPE       0x5678   // Tipo de imagen
```

> **Nota**: El ID del fabricante y el tipo de imagen deben coincidir con los valores utilizados por el servidor OTA/coordinador al generar la imagen de actualización.

### Componentes clave del código

| Componente | Descripción |
|------------|-------------|
| `ota_file_parser.c/h` | Parser TLV en streaming personalizado para bloques de datos de payload ZCL OTA. Crea un contexto de parser mediante `esp_zb_create_ota_file_parser()` y procesa datos vía `esp_zb_ota_file_parser_process()` |
| `router.c` | Registra el cluster OTA Upgrade Client y gestiona callbacks de progreso OTA vía `ota_upgrade_client_handle_ota_progress()` |
| Tamaño de bloque | **223 bytes** — tamaño máximo de bloque de datos por mensaje OTA |

### Estados de progreso OTA

El cliente OTA informa el progreso a través de los siguientes callbacks del cliente ZCL OTA Upgrade:

| Señal | Significado |
|-------|-------------|
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_START` | Actualización OTA iniciada |
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_RECEIVING` | Recibiendo bloques de datos de imagen |
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_CHECK` | Descarga completa; verificando imagen |
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_APPLY` | Aplicando imagen y cambiando partición de arranque |
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_FINISH` | Actualización finalizada; reiniciando |
| `EZB_ZCL_OTA_UPGRADE_PROGRESS_ABORT` | Actualización abortada; limpieza de estado |

### Requisitos de compilación

El cliente OTA requiere el componente `app_update` en `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "router.c" "ota_file_parser.c" "button.c" "silent_mode_zcl.c"
    INCLUDE_DIRS "."
    REQUIRES esp_zigbee_lib app_update
)
```

---

## Arquitectura del firmware

---

## Arquitectura del firmware

### Ficheros principales

| Fichero | Descripción |
|---------|-------------|
| `main/router.c` | Lógica principal: inicialización, máquina de estados Zigbee, LED de estado |
| `main/router.h` | Constantes de configuración, máscaras de canal y macros de inicialización del stack |
| `main/button.c` | Gestos del botón BOOT (GPIO9): ISR + timers FreeRTOS, modo noche, toggle TX, factory reset, permit-join |
| `main/button.h` | API pública del módulo button: `button_init()` y `button_is_night_mode()` |
| `main/Kconfig.projbuild` | Opciones de configuración exportadas al sistema de compilación |
| `main/idf_component.yml` | Dependencias de componentes IDF |
| `sdkconfig.defaults` | Configuración mínima ESP-IDF (flash 16 MB, Zigbee habilitado, log DEBUG) |
| `partitions.csv` | Tabla de particiones con partición `zb_storage` para NVS Zigbee |

### Flujo de inicialización

```
app_main()
  ├── configure_led()                        — inicializar LED RGB en GPIO 8 (RMT)
  ├── button_init()                          — instalar ISR GPIO9 y timers FreeRTOS
  ├── nvs_flash_init()                       — NVS general
  ├── nvs_flash_init_partition("zb_storage") — NVS Zigbee
  └── xTaskCreate(esp_zigbee_stack_main_task, stack=ZIGBEE_MAIN_TASK_STACK_SIZE)
        ├── esp_zigbee_init()
        ├── esp_ieee802154_set_txpower(ROUTER_TX_POWER_LOW_DBM)  — 8 dBm al arranque
        ├── ezb_aps_secur_enable_distributed_security(false)
        ├── ezb_secur_set_global_link_key()   — TC Link Key estándar HA
        ├── ezb_secur_set_tclk_exchange_required(true)
        ├── ezb_nwk_set_min_join_lqi(0)       — sin filtrado de LQI
        ├── ezb_bdb_set_primary_channel_set()
        ├── ezb_bdb_set_secondary_channel_set()
        ├── register_router_endpoint()        — Range Extender, EP 1, device 0x0008
        └── esp_zigbee_start() → esp_zigbee_launch_mainloop()
```

### Máquina de estados (señales BDB)

| Señal | Acción | LED |
|-------|--------|-----|
| `ZDO_SIGNAL_SKIP_STARTUP` | Lanza `BDB_MODE_INITIALIZATION` | 🔴 Rojo |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + factory-new | Lanza `BDB_MODE_NETWORK_STEERING` | 🟡 Ámbar |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK + rejoin | Device Announce inmediato | 🟢 Verde |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` fallo | Reintento tras `ROUTER_BDB_INIT_RETRY_MS` | 🔴 Rojo |
| `BDB_SIGNAL_STEERING` OK | Unido a la red, envía Device Announce (×2: 3 s y 8 s) | 🟢 Verde |
| `BDB_SIGNAL_STEERING` `NO_NETWORK` | Reintento tras `ROUTER_STEERING_RETRY_MS`, alterna INIT/STEERING | 🟡 Ámbar |
| `BDB_SIGNAL_STEERING` `NOT_PERMITTED` / `TARGET_FAILURE` | Reintento programado | 🟡 Ámbar |
| `BDB_SIGNAL_STEERING` `TCLK_EX_FAILURE` | Log de advertencia, reintento | — |
| `NWK_SIGNAL_PERMIT_JOIN_STATUS` activo | Ventana de emparejamiento abierta en el PAN (iniciada por el coordinador) | 🔵 Azul |
| `NWK_SIGNAL_PERMIT_JOIN_STATUS` cerrado | Ventana de emparejamiento cerrada | 🟢 Verde |
| `ZDO_SIGNAL_LEAVE` | Dispositivo eliminado de la red | 🔴 Rojo |

### Lógica de reintento de steering

Cuando el steering falla, el firmware alterna entre dos modos de reintento para maximizar las probabilidades de unirse a la red:

- **`EZB_BDB_MODE_NETWORK_STEERING`**: intento de join directo
- **`EZB_BDB_MODE_INITIALIZATION`**: reinicialización completa del stack antes del siguiente intento

La variable `retry_with_initialization` controla la alternancia. Un reintento solo se programa si `steering_retry_pending` es `false`, evitando acumulación de timers duplicados. Ambas variables son `_Atomic bool` para garantizar visibilidad entre la tarea del timer y el signal handler.

### Seguridad Zigbee

- **Seguridad distribuida**: deshabilitada (`ezb_aps_secur_enable_distributed_security(false)`)
- **Trust Center Link Key**: clave estándar ZigBee Alliance HA (`ZigBeeAlliance09`)
- **TCLK exchange**: obligatorio (`ezb_secur_set_tclk_exchange_required(true)`)
- **LQI mínimo para join**: 0 (sin filtrado, máxima compatibilidad de alcance)
- **Install code policy**: habilitada (`install_code_policy = true`) — los nuevos dispositivos deben presentar un install code válido antes de recibir una link key única del coordinador.

> ⚠️ El TC Link Key estándar es público y bien conocido. Para redes de producción con requisitos de seguridad elevados, considerar una política de install codes completa.

---

## Botón BOOT (GPIO9)

El botón BOOT del DevKitC-1 es un GPIO de propósito general una vez que el firmware está en ejecución. El módulo `button.c` instala una ISR en `ANYEDGE` y gestiona **cuatro** software timers FreeRTOS (`btn_tap`, `btn_hold`, `btn_blink`, `btn_pj`) para detectar los siguientes gestos:

| Gesto | Umbral | Acción | Feedback LED |
|-------|--------|--------|-------------|
| Toque simple | Ventana de 500 ms | Alterna modo noche del LED: silencia / restaura todas las actualizaciones de LED de estado Zigbee | LED apagado (modo noche ON) / vuelve a verde (modo noche OFF) |
| Doble toque | < 500 ms entre pulsaciones | Abre ventana Permit Join durante `ROUTER_PERMIT_JOIN_DURATION_S` (60 s). Un segundo doble-tap mientras la ventana está abierta la cierra inmediatamente. | Verde suave pulsante mientras la ventana está abierta |
| Triple toque | < 500 ms entre pulsaciones | Toggle potencia TX: 8 dBm ↔ 20 dBm | 3× rojo brillante (boost) / 3× azul suave (normal) |
| Mantener 5 s | GPIO en LOW durante 5 s | Factory reset (borrado NVS Zigbee) + reboot | Magenta parpadeante rápido → rojo fijo → reboot |

> **Nota sobre el modo noche**: solo se suprimen las actualizaciones de LED de transición de estado Zigbee. El feedback de gestos de `button.c` (destellos TX, parpadeo de factory reset, pulso de permit-join) **no** se suprime — el usuario lo ha activado explícitamente.

> **Nota sobre el reboot**: el DevKitC-1 dispone de un botón **RST** dedicado en la placa. El gesto de reboot por software no está implementado — usar el botón físico.

> **Nota sobre el factory reset**: el borrado se realiza con `nvs_flash_erase_partition()` directamente sobre el driver de flash. Esto garantiza el borrado correcto incluso si la partición está no inicializada o corrupta. El IEEE address (EUI-64) del chip **no cambia** — está grabado en efuses de fábrica.

> **Nota sobre Permit Join**: el doble-tap abre una ventana Permit Join local. Cualquier dispositivo que se una durante esta ventana utilizará el modelo de confianza HA estándar. La ventana se cierra automáticamente; no persiste tras un reboot.

### Silent mode / modo silencioso

La pulsación simple del botón BOOT alterna el modo silencioso del LED de estado. Desde la versión 6.0.0, este cambio no solo actúa localmente sobre el LED, sino que también sincroniza el estado del endpoint Zigbee asociado al modo silencioso mediante `silent_mode_zcl_sync()`, de forma que el cliente puede ver el nuevo estado cuando el cambio se origina en el botón hardware.

Estado por defecto al arrancar:
- El modo silencioso arranca desactivado (`night mode OFF`), por lo que el LED de estado está habilitado tras el arranque.

Identidad Zigbee:
- Modelo expuesto en Basic cluster: `ESP32-C6 Zigbee Router (DIY)`.
- Versión de firmware expuesta: `6.0.0`.

---

## Potencia TX (IEEE 802.15.4)

| Modo | Valor | Cuándo usar |
|------|-------|-------------|
| Normal (arranque por defecto) | **8 dBm** | Operación diaria en red doméstica. Balance óptimo rango/consumo/interferencia. |
| Boost (triple-tap) | **20 dBm** | Nodo lejano que no alcanza la red, troubleshooting puntual. Máximo hardware del ESP32-C6 / límite CE ETSI EN 300 328. |

El modo boost es **volátil**: revierte a 8 dBm en el siguiente reboot. El ciclo del triple-tap es: `8 dBm → 20 dBm → 8 dBm → …`

---

## LED de estado — Tabla de colores

### Colores de estado de red

| Color | R | G | B | Estado |
|-------|---|---|---|--------|
| 🔴 Rojo suave | 64 | 0 | 0 | Sin red / error crítico / dispositivo eliminado de la red |
| 🟡 Ámbar | 30 | 7 | 0 | Buscando red / join rechazado / dispositivo activo sin red |
| 🟢 Verde | 0 | 16 | 0 | Conectado a la red Zigbee |
| 🔵 Azul | 0 | 0 | 16 | Permit Join activo en el PAN (iniciado por el coordinador) |
| 🟢 Verde suave pulsante | 0 | 32 | 0 | Ventana Permit Join abierta localmente (doble-tap) |

### Colores de feedback de gestos

| Color | R | G | B | Significado |
|-------|---|---|---|-------------|
| 🔴 Rojo brillante | 255 | 0 | 0 | Confirmación TX boost (20 dBm activo) |
| 🔵 Azul suave | 0 | 0 | 64 | Confirmación TX normal (8 dBm activo) |
| 🟣 Magenta | 255 | 0 | 255 | Acción destructiva en curso (factory reset) |

**Semántica de colores:**
- 🔴 **Rojo** — error crítico o ausencia total de red / TX en modo boost
- 🟡 **Ámbar** — advertencia / en progreso: el dispositivo está activo y reintentando
- 🟢 **Verde** — estado nominal: el router está integrado en la red y enrutando tráfico
- 🔵 **Azul** — informativo: ventana de emparejamiento del PAN abierta (iniciada por el coordinador) / TX en modo normal
- 🟢 **Verde suave pulsante** — ventana Permit Join abierta localmente (doble-tap); pueden unirse nuevos dispositivos
- 🟣 **Magenta** — acción destructiva en progreso (parpadeo); no interrumpir

---

## Endpoint Zigbee registrado

| Parámetro | Valor |
|-----------|-------|
| Endpoint ID | 1 (`ESP_ZIGBEE_RANGE_EXTENDER_EP_ID`) |
| Perfil | HA (`EZB_AF_HA_PROFILE_ID` / `0x0104`) |
| Device ID | Range Extender (`0x0008`) |
| Power Source (cluster Basic) | `SINGLE_PHASE_MAINS` (`0x01`) |
| Clusters servidor | Basic (con `ManufacturerName`, `ModelIdentifier` y `SWBuildID`) |

### Atributo ZCL SWBuildID

| Atributo | ID | Tipo | Valor |
|----------|----|------|-------|
| `SWBuildID` | `0x4000` (`EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID`) | String | Versión del firmware (ej. `"v4.0.0"`) |

Este atributo es legible sobre Zigbee por el coordinador o cualquier hub. Permite identificar la versión del firmware de forma remota sin acceso físico al dispositivo.

> El endpoint registra únicamente el cluster Basic con los atributos de identificación obligatorios. No incluye clusters de aplicación (On/Off, etc.) ya que el router actúa exclusivamente como nodo de infraestructura.

---

## Tabla de particiones

Configurada en `partitions.csv` con una partición `zb_storage` dedicada al almacenamiento persistente del stack Zigbee (claves de red, dirección, canal). Esto garantiza que el dispositivo recuerde su red tras un ciclo de alimentación sin necesidad de re-emparejamiento.

---

## Versiones

Este proyecto sigue [Semantic Versioning](https://semver.org/lang/es/). El historial completo de cambios está en [CHANGELOG.md](../../../CHANGELOG.md).

| Versión | Fecha | Resumen |
|---------|-------|---------|
| **v4.0.0** | 2026-06-21 | Toque simple modo noche, doble-tap Permit Join, LED verde suave pulsante, atributo ZCL `SWBuildID`. |
| **v3.0.0** | 2026-06-17 | Migración completa del SDK Zigbee de `esp_zb_*` a `ezb_*` (esp-zigbee-lib 2.x). Sin cambio en runtime. |
| **v2.0.0** | 2026-06-16 | Botón BOOT: triple-tap toggle TX, hold 5 s factory reset. Corrección borrado NVS. |
| **v1.0.0** | 2026-06-15 | Router Zigbee funcional, LED RGB, steering retry con alternancia INIT/STEERING. |

---

## Resolución de problemas

| Síntoma | Causa probable | Acción |
|---------|----------------|--------|
| LED apagado, dispositivo operativo | Modo noche activo (toque simple) | Toque simple de nuevo para restaurar el LED |
| LED 🟡 ámbar constante | Coordinador sin permit join abierto o fuera de rango | Abrir permit join en el coordinador y verificar distancia |
| 🟡 ámbar alternando con 🔴 rojo | Ciclo STEERING → INITIALIZATION en progreso | Normal si el coordinador no está disponible; esperar o abrir permit join |
| `TCLK_EX_FAILURE` en logs | Política de seguridad del coordinador incompatible | Verificar que el coordinador acepta el TC Link Key estándar |
| `NOT_PERMITTED` en logs | Permit join cerrado en el coordinador | Abrir permit join (60–254 s) |
| Reset por WDT durante steering | Stack insuficiente en la tarea Zigbee | Verificar `ZIGBEE_MAIN_TASK_STACK_SIZE` ≥ 10240 bytes |
| Dispositivo no visible en la UI del coordinador | `power_source` incorrecto en cluster Basic | Verificar `power_source = SINGLE_PHASE_MAINS` |
| Router no escanea todos los canales | Máscara primaria demasiado restrictiva | Revisar `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK`; el fallback secundario escaneará 11–26 |
| Toque simple no activa modo noche | Pulsación demasiado lenta o contada como inicio de doble-tap | Una pulsación limpia dentro de la ventana de 500 ms |
| Doble-tap no abre Permit Join | Pulsaciones demasiado lentas o interrumpidas por detección de triple-tap | Mantener < 500 ms entre pulsaciones; exactamente 2 toques |
| Triple-tap no responde | Pulsaciones demasiado lentas | Mantener menos de 500 ms entre cada pulsación |
| Factory reset no borra la red | Error en `nvs_flash_erase_partition` | Revisar log serie — el error se registra con `ESP_LOGE`; comprobar nombre de partición en `partitions.csv` |
| TX power no cambia tras triple-tap | `esp_ieee802154_set_txpower` falla | Verificar en log: `set_txpower(%d) failed` — puede indicar conflicto con el stack Zigbee |

---

## Licencia

El código fuente original de este proyecto está publicado bajo **CC0-1.0** (dominio público). Ver cabeceras SPDX en los ficheros fuente.

Sin embargo, este firmware **enlaza y depende** de las siguientes bibliotecas de Espressif, que tienen sus propias licencias:

| Dependencia | Licencia | Notas |
|-------------|----------|-------|
| [ESP-IDF](https://github.com/espressif/esp-idf) | Apache-2.0 | Framework base |
| [esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk) | Apache-2.0 | SDK Zigbee de alto nivel |
| [esp-zboss-lib](https://github.com/espressif/esp-zboss-lib) | [Licencia Espressif](https://github.com/espressif/esp-zboss-lib/blob/master/LICENSE) | Stack ZBOSS en binario; licencia separada |

> ⚠️ Cualquier redistribución o producto derivado debe cumplir con los términos de **todas** las licencias anteriores, incluyendo los requisitos de atribución de Apache-2.0 y las condiciones específicas de `esp-zboss-lib`.

Para contribuir, ver [CONTRIBUTING.md](../../../CONTRIBUTING.md).
