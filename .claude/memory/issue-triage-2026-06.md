---
name: issue-triage-2026-06
description: Triage consolidado de los 4 trackers (Sunshine/Apollo host, Moonlight-qt/Artemis cliente) — qué heredamos, PRs huérfanos a rescatar, y mapa de los dolores de Jordi a issues reales
metadata:
  type: project
---

Triage de los issues abiertos de los 4 repos, hecho 2026-06-28 (4 agentes en paralelo). Ver [[forks-helios-selene]] para el plan.

## Conteos / salud
- **Sunshine** (grand-upstream host): 91 abiertos, 37 PRs. ACTIVO pero con cola crónica; Linux/Wayland/KDE es el área de más fricción y menos resuelta.
- **Apollo** (upstream host directo → Helios): **298 abiertos, 14 PRs**, sin labels/milestones/triage. Vivo en reportes, muerto en cierres → "abandonware". Arrastra issues numerados desde la era Sunshine (#221 de 2022).
- **Moonlight-qt** (grand-upstream cliente): **497 abiertos**, triage flojo (29% con 0 comentarios), muchos duplicados y soporte mezclado.
- **Artemis** (upstream cliente directo → Selene): 20 abiertos, 2 PRs. Muerto ~10 meses. NO es fork registrado en GitHub de Moonlight-qt (historia importada) → parches de upstream a mano.

## 🌟 HALLAZGO CLAVE: ya existe PR de display virtual Linux
- **Apollo PR #1477 "Add experimental virtual display support for Linux"** (abr 2026, SIN mergear) + issue central **#1161 "Virtual Display does not work on Linux"** (42 comentarios). Es EXACTAMENTE el dolor #1 de Jordi (lo que hackea con gamescope+KMS). **Candidato nº1 a heredar/terminar en Helios** en vez de escribir desde cero.

## PRs huérfanos a rescatar (trabajo gratis)
- **Helios (de Apollo):** #1477 (VD Linux 🏆), #1481 (use-after-free en pairing), #1514 (esperar output configurado), #1495/#1515 (VAAPI presets/CQP — útil AMD/Intel de Jordi), #1460 (audio waveformat), #1038 (Windows Graphics Capture in service, viejo).
- **Selene (de Artemis):** #64 (use-after-free en SSL key, ya arreglado en Moonlight-qt upstream — seguridad gratis), #57 (update setup-dev script, va con #48).

## Mapa: dolores de Jordi → issues reales
- **Display virtual Linux (host):** Apollo #1161/#2044/#1427/#1414 + PR #1477. Upstream Sunshine NO tiene issue de esto → territorio propio del fork.
- **HDR:** Apollo #3298 (HDR muy oscuro tras Plasma 6.2, 40c, = stack KDE de Jordi), #357/#887 (HDR+VD), Sunshine #3298. Cliente Moonlight-qt: patrón sistémico washed-out/oversaturado (#1306/#1444/#1454/#1505/#1508). Artemis #21 (HDR gris en Deck, EGLRenderer no soporta HDR).
- **Resolución dinámica dock/undock (cliente):** Moonlight-qt #784 (auto-switch ultrawide, 25c 🔥), #1678 (auto-detectar specs cliente), #1899/#1795 (per-host res), #924 (host no revierte resolución al salir). Diseño previo reutilizable.
- **Refrescos fraccionarios:** Moonlight-qt #1849 (feature request directo), #1147 (non-integer FPS). Artemis #65 (parsing locale 59.94 roto). Apollo #1417.
- **Persistencia de settings:** Artemis #66 (portable no guarda). Moonlight-qt #213 (perfiles), #1187/#869 (per-host).
- **Flicker HDMI @240Hz+HDR:** SIN issue en ningún upstream → hueco de demanda, diferenciador; documentarlo/abrirlo nosotros.

## Quick wins para señal de "fork vivo"
- Helios: #5234 (crash por locale), #5019 (log truncado en init), #1472 (nombre estable de VD), mergear PRs casi listos (#1481, #1495/#1515).
- Selene: #48 (renombrar moonlight-qt.pro→artemis.pro), arreglar flatpak con el manifest YA escrito en el hilo de #58 (cierra #23/#27/#53/#58 de un golpe), #62 (libSDL2_ttf en portable), #66.

## Quick-wins de Selene — progreso (2026-06-28)
- **#48** scripts `moonlight-qt.pro`→`artemis.pro` (setup-dev.sh, build-steamlink-app.sh, build-arch.bat) → **PR Selene #6**. Hecho, 0 refs restantes. Créditos doc a Moonlight-qt NO tocados.
- **#65** refrescos fraccionarios (59.94): fix locale-independiente en `app/gui/SettingsView.qml` (`DoubleValidator locale:"C"` + `parseFloat(replace(",","."))`). C++ ya era `double`. → **PR Selene #7**.
- **#62** libSDL2_ttf en portable Linux: fallback explícito + deps transitivas (freetype/harfbuzz/png...) en el job `build-linux-dev` de `dev-build.yml` → **PR Selene #8** (se auto-valida en su CI de Linux).
- **#66** settings portable NO persiste: causa raíz = `QDir::currentPath()` en vez de `QCoreApplication::applicationDirPath()` en `main.cpp:324` + `path.cpp` (modo portable); causa 2ª = `artemissettings.cpp` ignora la redirección de QSettings (ruta hardcodeada a AppConfigLocation). **Diff listo, NO abierto** — riesgo medio (C++ core, cambia rutas de config) + necesita validación en runtime → ESPERA OK de Jordi.

## Bugs críticos/regresiones a NO heredar ciegamente
Apollo crashes recientes: #5168 (segfault, 21c), #5217 (rompió enc NVIDIA viejas), #4966 (segfault VAAPI Intel Arc), #5316 (VAAPI crash AMD Polaris), #5209 ("Couldn't find monitor [0]" cuando el monitor se apaga por DPMS — relevante al ultrawide de Jordi).
