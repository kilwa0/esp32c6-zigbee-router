# Contribuir a esp32c6-zigbee-router

Gracias por tu interés en contribuir. Este documento describe el flujo de trabajo que seguimos para mantener el historial limpio y las revisiones trazables.

> 🇬🇧 English version: [CONTRIBUTING.md](../../../CONTRIBUTING.md)

---

## Código de conducta

Este proyecto es de naturaleza abierta y constructiva. Se espera que cualquier contribución sea respetuosa, técnicamente justificada y compatible con la licencia del proyecto (ver más abajo).

---

## Licencia de las contribuciones

Al enviar una contribución a este repositorio aceptas que tu código se publica bajo los mismos términos que el proyecto: **CC0-1.0** para el código fuente original. Ten en cuenta que el proyecto enlaza con dependencias de terceros bajo Apache-2.0 y otras licencias (ver sección *Licencia* del README); tu contribución debe ser compatible con ellas.

---

## Flujo de trabajo

### 1. Crea un fork

Haz un fork del repositorio desde GitHub. Trabaja siempre desde tu fork, nunca directamente sobre el upstream.

```bash
# Clonar tu fork
git clone https://github.com/<tu-usuario>/esp32c6-zigbee-router.git
cd esp32c6-zigbee-router

# Añadir el upstream como remoto
git remote add upstream https://github.com/kilwa0/esp32c6-zigbee-router.git
```

### 2. Sincroniza con upstream antes de empezar

```bash
git fetch upstream
git checkout main
git merge upstream/main
```

### 3. Crea una rama con nombre descriptivo

Usa el prefijo que corresponda al tipo de cambio:

| Prefijo | Cuándo usarlo |
|---------|---------------|
| `feat/` | Nueva funcionalidad |
| `fix/` | Corrección de bug |
| `docs/` | Solo documentación |
| `refactor/` | Refactor sin cambio de comportamiento |
| `chore/` | Tareas de mantenimiento (deps, CI, etc.) |

```bash
git checkout -b feat/nombre-descriptivo
```

### 4. Desarrolla en commits atómicos

Cada commit debe representar un cambio coherente y compilable. Sigue el formato [Conventional Commits](https://www.conventionalcommits.org/es/v1.0.0/):

```
<tipo>(<ámbito opcional>): <descripción corta en imperativo>

<cuerpo opcional: qué cambia y por qué, no cómo>

<footer opcional: BREAKING CHANGE o refs a issues>
```

Ejemplos:

```
feat(button): add triple-tap TX power toggle

Installs GPIO9 ISR and three FreeRTOS software timers to detect
triple-tap gesture. Toggles TX between 8 dBm and 20 dBm with LED
feedback. Boost mode is volatile and reverts on reboot.
```

```
fix(button): use nvs_flash_erase_partition for factory reset

nvs_open() fails silently on an uninitialized or corrupt partition,
which is exactly the state after a broken join. Replace with
nvs_flash_erase_partition() which operates at driver level and works
in any partition state.
```

```
docs: rewrite README in Spanish, document button gestures
```

### 5. Abre un Pull Request desde tu fork

Desde GitHub, abre un PR desde `<tu-usuario>/esp32c6-zigbee-router:<tu-rama>` hacia `kilwa0/esp32c6-zigbee-router:main` (o hacia la rama de feature correspondiente si tu cambio se basa en trabajo en curso).

**El PR debe incluir:**
- Título en formato Conventional Commits
- Descripción con: qué cambia, por qué y cómo probarlo
- Si añade funcionalidad nueva: actualizar `CHANGELOG.md` en la sección `[Sin publicar]`

### 6. Revisión y merge

El mantenedor revisará el PR, podrá pedir cambios y hará merge cuando esté listo. No hagas merge tú mismo de tu PR al upstream.

---

## Convenciones de código

- **Lenguaje**: C (C17), estilo ESP-IDF.
- **Formato**: 4 espacios, sin tabs. Líneas ≤ 100 caracteres.
- **Comentarios**: en inglés (el código es inglés, la documentación de usuario es español).
- **Cabeceras de fichero**: mantener el identificador SPDX `CC0-1.0` en ficheros nuevos propios.
- **Compilación limpia**: el código debe compilar sin warnings con `idf.py build` antes de abrir el PR.

---

## Versionado

Seguimos [Semantic Versioning](https://semver.org/lang/es/). Antes de abrir un PR que introduzca cambios de comportamiento, consulta la tabla de la sección *Versiones* del README o la cabecera del `CHANGELOG.md` para determinar qué componente de versión incrementar.

| Tipo de cambio | Componente |
|---|---|
| Gesto nuevo, cambio de semántica del LED, parámetro Zigbee | **MAJOR** |
| Nueva funcionalidad no disruptiva | **MINOR** |
| Bugfix puro | **PATCH** |

---

## Preguntas

Abre un [issue](https://github.com/kilwa0/esp32c6-zigbee-router/issues) con la etiqueta `question` si tienes dudas antes de empezar a desarrollar.
