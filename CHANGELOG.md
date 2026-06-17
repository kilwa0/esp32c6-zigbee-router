# Changelog

Todos los cambios notables de este proyecto se documentan en este fichero.

Formato basado en [Keep a Changelog](https://keepachangelog.com/es/1.1.0/).
Versionado según [Semantic Versioning](https://semver.org/lang/es/):

| Componente | Cuándo incrementar |
|---|---|
| **MAJOR** | Cambio de comportamiento observable externamente: gestos nuevos, semántica del LED, parámetros Zigbee, cambio de API de dependencias |
| **MINOR** | Nueva funcionalidad no disruptiva: nuevo gesto, nuevo endpoint, nueva métrica |
| **PATCH** | Bugfix puro sin cambio de comportamiento observable |

> El seguimiento de **PATCH** comienza a partir de v2.1.0. Las versiones anteriores no reportaban la versión al hub y los bugfixes no se versionaban de forma independiente.

---

## [Sin publicar]

### Pendiente
- Reporte de versión de firmware al hub Zigbee mediante el atributo `SWBuildID` del cluster Basic.

---

## [3.0.0] — 2026-06-17

### Cambiado (BREAKING)
- **Migración completa a `esp-zigbee-sdk` ≥ 2.x (API `ezb_*`)**: todos los símbolos del stack Zigbee han migrado del prefijo `esp_zb_*` al nuevo prefijo `ezb_*`. Los headers incluidos son ahora `esp_zigbee.h`, `ezbee/af.h`, `ezbee/secur.h`, `ezbee/zcl/cluster/basic_desc.h` y `ezbee/zdo/zdo_dev_srv_disc.h`. La dependencia en `idf_component.yml` pasa de `espressif/esp-zigbee-sdk` a `espressif/esp-zigbee-lib >=2.0.0`.
- La función de registro de endpoint reemplaza la API builder clásica (`esp_zb_cluster_list_*`, `esp_zb_ep_list_*`) por el nuevo flujo basado en descriptores: `ezb_af_create_device_desc()` → `ezb_af_create_endpoint_desc()` → `ezb_zcl_basic_create_cluster_desc()` → `ezb_af_device_desc_register()`.
- Las señales BDB y ZDO se leen con `ezb_app_signal_get_type()` y `ezb_app_signal_get_params()` en lugar de la macro `ESP_ZIGBEE_ERROR_CHECK`.
- Las funciones de canal, seguridad y configuración del router usan los nuevos símbolos: `ezb_bdb_set_primary_channel_set()`, `ezb_bdb_set_secondary_channel_set()`, `ezb_secur_set_global_link_key()`, `ezb_secur_set_tclk_exchange_required()`, `ezb_aps_secur_enable_distributed_security()`.
- `router.h`: macro `ESP_ZIGBEE_ROUTER_CONFIG()` actualizada con `ezb_nwk_device_type_t` (`EZB_NWK_DEVICE_TYPE_ROUTER`) y tipo `ezb_bdb_comm_mode_t` para los modos BDB.
- `idf_component.yml`: `alarm_timer` obtenido via `git` + `path` desde `espressif/esp-zigbee-sdk` (no publicado en el registro de componentes); `led_strip` fijado en `^3.0.0`.

### Corregido
- `EZB_ZCL_ATTR_BASIC_SW_BUILD_ID` → `EZB_ZCL_ATTR_BASIC_SW_BUILD_ID_ID` (nombre correcto del símbolo en SDK 2.x).
- `SET_LED_IF_AWAKE(LED_RED)` → valores RGB explícitos por triplet: el preprocesador C no expande argumentos de macro con comas en múltiples parámetros.

---

## [2.0.0] — 2026-06-16

### Añadido
- **Módulo `button.c` / `button.h`**: gestos del botón BOOT (GPIO9) mediante ISR + tres software timers FreeRTOS.
- **Triple-tap**: toggle de potencia TX entre 8 dBm (normal) y 20 dBm (boost). Feedback visual con 3× parpadeo rojo brillante (boost activado) o 3× parpadeo azul suave (normal restaurado). El modo boost es volátil y revierte en el siguiente reboot.
- **Hold 5 s**: factory reset destructivo. Borra la partición `zb_storage` con `nvs_flash_erase_partition()` directamente sobre el driver de flash (sin pasar por la capa NVS) y reinicia el dispositivo. Feedback visual con parpadeo magenta rápido → rojo fijo → reboot.
- Potencia TX inicial configurable mediante `ROUTER_TX_POWER_LOW_DBM` y `ROUTER_TX_POWER_HIGH_DBM` en `router.h`.
- Colores de feedback de gestos en la tabla del LED: rojo brillante (255,0,0), azul suave (0,0,64), magenta (255,0,255).

### Corregido
- **Factory reset no ejecutaba el borrado real**: la implementación anterior usaba `nvs_open()` sobre la partición Zigbee, que falla silenciosamente si la partición está no inicializada o corrupta. Reemplazado por `nvs_flash_erase_partition()` que opera a nivel de driver y funciona en cualquier estado.
- Eliminado el gesto de reboot por software (redundante con el botón RST físico del DevKitC-1); simplificado el módulo de botón a un único stage de 5 s.

### Cambiado
- `ZIGBEE_MAIN_TASK_STACK_SIZE` aumentado de 8192 a 10240 bytes.
- Flujo de inicialización: `button_init()` llamado antes de `nvs_flash_init()` para que el botón esté activo lo antes posible.

---

## [1.0.0] — 2026-06-15

### Añadido
- Router Zigbee puro sobre ESP32-C6 con ESP-IDF ≥ 6.2.0 y `esp-zigbee-sdk`.
- LED RGB WS2812 en GPIO8 vía RMT con cuatro estados de red: rojo (sin red), ámbar (buscando), verde (conectado), azul (permit join activo).
- Máquina de estados BDB completa: `INITIALIZATION` → `NETWORK_STEERING` con retry automático.
- Lógica de retry de steering con alternancia `STEERING` / `INITIALIZATION` controlada por `_Atomic bool` para seguridad entre tareas.
- Endpoint Zigbee HA: Range Extender (device `0x0008`), EP 1, cluster Basic con `ManufacturerName` y `ModelIdentifier`.
- Seguridad HA estándar: TC Link Key `ZigBeeAlliance09`, TCLK exchange obligatorio, seguridad distribuida deshabilitada.
- Tabla de particiones personalizada con partición `zb_storage` para NVS Zigbee persistente.
- Máscaras de canal configurables vía Kconfig: primaria (15, 20, 25) y secundaria (11–26 completo).
- Device Announce doble tras join exitoso (3 s y 8 s) para maximizar visibilidad en el coordinador.
- Wrapper de alto nivel `ezbee` sobre `esp-zigbee-sdk`.

[Sin publicar]: https://github.com/kilwa0/esp32c6-zigbee-router/compare/v3.0.0...HEAD
[3.0.0]: https://github.com/kilwa0/esp32c6-zigbee-router/compare/v2.0.0...v3.0.0
[2.0.0]: https://github.com/kilwa0/esp32c6-zigbee-router/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/kilwa0/esp32c6-zigbee-router/releases/tag/v1.0.0
