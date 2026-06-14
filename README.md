# esp32c6-zigbee-router

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
| `ESP_ZIGBEE_PRIMARY_CHANNEL_MASK` | Máscara canales primarios (11–26) | `0x07FFF800` |
| `ESP_ZIGBEE_SECONDARY_CHANNEL_MASK` | Máscara canales secundarios | `0x07FFF800` |

Para personalizar el nombre de fabricante y modelo del dispositivo, editar `main/router.h`:

```c
#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"
#define ESP_MODEL_IDENTIFIER  "\x08""ESP32-C6"
```

> **Nota**: El prefijo `\xNN` es la longitud de la cadena en formato ZCL (Pascal string). Actualizar el byte de longitud si se cambia el texto.

---

## Arquitectura del firmware

### Ficheros principales

| Fichero | Descripción |
|---------|-------------|
| `main/router.c` | Lógica principal: inicialización, máquina de estados Zigbee, LED de estado |
| `main/router.h` | Constantes de configuración y macros de inicialización de la stack |
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
  └── xTaskCreate(esp_zigbee_stack_main_task, stack=8192)
        ├── esp_zigbee_init()
        ├── ezb_secur_set_global_link_key()   — TC Link Key estándar HA
        ├── ezb_secur_set_tclk_exchange_required(true)
        ├── register_router_endpoint()         — ZHA On/Off Switch, EP 1
        └── esp_zigbee_launch_mainloop()       — loop principal Zigbee
```

### Máquina de estados (señales BDB)

El router gestiona su ciclo de vida de red mediante el manejador `esp_zigbee_app_signal_handler`. Los estados principales son:

| Señal | Acción | LED |
|-------|--------|-----|
| `ZDO_SIGNAL_SKIP_STARTUP` | Lanza `BDB_MODE_INITIALIZATION` | Rojo |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` OK | Si factory-new → steering; si rejoin → Device Announce | Azul → Verde |
| `BDB_SIGNAL_DEVICE_FIRST_START` / `REBOOT` fallo | Reintento en 2 s vía alarm_timer | Rojo |
| `BDB_SIGNAL_STEERING` OK | Unido a red, envía Device Announce (×2: 3 s y 8 s) | Verde |
| `BDB_SIGNAL_STEERING` fallo | Reintento en 5 s, alterna INITIALIZATION/STEERING | Rojo |
| `NWK_SIGNAL_PERMIT_JOIN_STATUS` | Indica apertura/cierre de permit join en la PAN | Azul/Verde |
| `ZDO_SIGNAL_LEAVE` | Dispositivo expulsado de la red | Rojo |

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
| 🔴 Rojo | 16 | 0 | 0 | Sin red / error / reintentando |
| 🟢 Verde | 0 | 16 | 0 | Conectado a la red Zigbee |
| 🔵 Azul brillante | 0 | 0 | 255 | BDB init OK, pendiente de steering |
| 🔵 Azul tenue | 0 | 0 | 16 | Permit join activo en la PAN |
| ⚪ Blanco | 16 | 16 | 16 | Señal Zigbee desconocida |

---

## Endpoint Zigbee registrado

| Parámetro | Valor |
|-----------|-------|
| Endpoint ID | 1 (`ESP_ZIGBEE_HA_ON_OFF_SWITCH_EP_ID`) |
| Profile | HA (`0x0104`) |
| Device ID | On/Off Output (`EZB_ZHA_ON_OFF_OUTPUT_DEVICE_ID`) |
| Power Source (Basic cluster) | `SINGLE_PHASE_MAINS` (`0x01`) |
| Clusters servidor | Basic, Identify, On/Off |

---

## Tabla de particiones

Configurada en `partitions.csv` con partición dedicada `zb_storage` para el almacenamiento persistente de la stack Zigbee (claves de red, dirección, canal). Esto garantiza que el dispositivo recuerde su red tras un reinicio de alimentación sin necesitar re-emparejamiento.

---

## Solución de problemas frecuentes

| Síntoma | Causa probable | Acción |
|---------|---------------|--------|
| LED rojo parpadeante constante | Coordinador no en permit join | Abrir permit join en el coordinador |
| `TCLK_EX_FAILURE` en logs | Política de seguridad del coordinador incompatible | Verificar que el coordinador acepta la TC Link Key estándar |
| `NOT_PERMITTED` en logs | Permit join cerrado en el coordinador | Abrir permit join (60–254 s) |
| WDT reset en steering | Stack insuficiente en la tarea Zigbee | Verificar que `xTaskCreate` usa ≥ 8192 bytes |
| Dispositivo no visible en la interfaz del coordinador | `power_source` incorrecto en Basic cluster | Verificar que `power_source = SINGLE_PHASE_MAINS` |

---

## Licencia

CC0-1.0 — ver cabeceras de ficheros fuente.
