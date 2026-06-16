# Changelog

Todos los cambios notables de este proyecto se documentan en este fichero.

Formato basado en [Keep a Changelog](https://keepachangelog.com/es/1.1.0/).
Versionado según [Semantic Versioning](https://semver.org/lang/es/):

| Componente | Cuándo incrementar |
|---|---|
| **MAJOR** | Cambio de comportamiento observable externamente: gestos nuevos, semántica del LED, parámetros Zigbee |
| **MINOR** | Nueva funcionalidad no disruptiva: nuevo gesto, nuevo endpoint, nueva métrica |
| **PATCH** | Bugfix puro sin cambio de comportamiento observable |

> El seguimiento de **PATCH** comienza a partir de v2.1.0. Las versiones anteriores no reportaban la versión al hub y los bugfixes no se versionaban de forma independiente.

---

## [Sin publicar]

### Añadido
- Reporte de versión de firmware al hub Zigbee mediante el atributo `SWBuildID` del cluster Basic (pendiente de implementación).

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

[Sin publicar]: https://github.com/kilwa0/esp32c6-zigbee-router/compare/v2.0.0...HEAD
[2.0.0]: https://github.com/kilwa0/esp32c6-zigbee-router/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/kilwa0/esp32c6-zigbee-router/releases/tag/v1.0.0
