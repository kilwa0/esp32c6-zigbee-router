# Contributing to esp32c6-zigbee-router

Thank you for your interest in contributing. This document describes the workflow we follow to keep a clean history and traceable reviews.

> 🇪🇸 Versión en español: [Docs/LNG/ES/CONTRIBUTING.es.md](Docs/LNG/ES/CONTRIBUTING.es.md)

---

## Code of Conduct

This project is open and constructive. Any contribution is expected to be respectful, technically justified, and compatible with the project license (see below).

---

## License of Contributions

By submitting a contribution to this repository you agree that your code is published under the same terms as the project: **CC0-1.0** for original source code. Note that the project links against third-party dependencies under Apache-2.0 and other licenses (see the *License* section of the README); your contribution must be compatible with all of them.

---

## Workflow

### 1. Fork the repository

Fork the repository on GitHub. Always work from your fork, never directly on upstream.

```bash
# Clone your fork
git clone https://github.com/<your-username>/esp32c6-zigbee-router.git
cd esp32c6-zigbee-router

# Add upstream as a remote
git remote add upstream https://github.com/kilwa0/esp32c6-zigbee-router.git
```

### 2. Sync with upstream before starting

```bash
git fetch upstream
git checkout main
git merge upstream/main
```

### 3. Create a descriptively named branch

Use the prefix that matches the type of change:

| Prefix | When to use |
|--------|-------------|
| `feat/` | New feature |
| `fix/` | Bug fix |
| `docs/` | Documentation only |
| `refactor/` | Refactor without behaviour change |
| `chore/` | Maintenance tasks (deps, CI, etc.) |

```bash
git checkout -b feat/descriptive-name
```

### 4. Develop in atomic commits

Each commit must represent a coherent, buildable change. Follow the [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/) format:

```
<type>(<optional scope>): <short description in imperative mood>

<optional body: what changes and why, not how>

<optional footer: BREAKING CHANGE or issue refs>
```

Examples:

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

### 5. Open a Pull Request from your fork

From GitHub, open a PR from `<your-username>/esp32c6-zigbee-router:<your-branch>` targeting `kilwa0/esp32c6-zigbee-router:main` (or the relevant feature branch if your change builds on work in progress).

**The PR must include:**
- A title in Conventional Commits format
- A description covering: what changes, why, and how to test it
- If adding new functionality: update `CHANGELOG.md` under the `[Unreleased]` section

### 6. Review and merge

The maintainer will review the PR, may request changes, and will merge it when ready. Do not merge your own PR into upstream.

---

## Code Conventions

- **Language**: C (C17), ESP-IDF style.
- **Formatting**: 4 spaces, no tabs. Lines ≤ 100 characters.
- **Comments**: in English (source code is English, user-facing documentation is Spanish).
- **File headers**: keep the SPDX identifier `CC0-1.0` in new source files you own.
- **Clean build**: code must compile without warnings with `idf.py build` before opening a PR.

---

## Versioning

This project follows [Semantic Versioning](https://semver.org/). Before opening a PR that introduces behavioural changes, consult the *Versions* table in the README or the header of `CHANGELOG.md` to determine which version component to increment.

| Type of change | Component |
|---|---|
| New gesture, LED semantic change, Zigbee parameter change | **MAJOR** |
| New non-disruptive feature | **MINOR** |
| Pure bug fix | **PATCH** |

---

## Questions

Open an [issue](https://github.com/kilwa0/esp32c6-zigbee-router/issues) with the `question` label if you have doubts before starting development.
