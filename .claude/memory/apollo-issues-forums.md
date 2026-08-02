---
name: apollo-issues-forums
description: Barrido GitHub/foros de Apollo (ClassicOldSong/Apollo, padre de Helios) — features maduras, qué no hace, y bugs abiertos / a medio cocer. Fechado 2026-07-25; los issues evolucionan.
metadata:
  type: reference
---

# Apollo (ClassicOldSong/Apollo) — barrido de issues + foros

> Fechado **2026-07-25**. Apollo es el fork de Sunshine que Jordi forkeó a "Helios". Snapshot vivo:
> los números de issue evolucionan. **Repo:** https://github.com/ClassicOldSong/Apollo
> Estado del repo hoy: **~10.3k estrellas, 322 issues abiertos**, `updated` hoy (sigue recibiendo
> commits e issues, aunque **sin release publicada desde hace ~10 meses** — ver §4).

## 0. Señal de vida del proyecto (contexto que enmarca todo lo demás)
- **Última Release PUBLICADA:** `v0.4.6` estable (2025-07-13) y `v0.4.7-alpha.1` (2025-08-12). Existe un
  **tag `v0.4.8` (2025-09-26) SIN Release** (sin binarios/changelog publicados). O sea: casi un año sin
  release formal. Esto disparó el issue **#1512 "Is Apollo abandonware?"** (ver §4).
- **PERO el repo NO está muerto:** commits en `master` hasta **may-2026** (últimos: fixes VAAPI/AMF,
  `fix(wayland): set frame timestamps for wlgrab captures` #1430, backport de seguridad GHSA #1496), y el
  maintainer responde issues a diario. Diagnóstico honesto: **congelado en features, en modo mantenimiento
  ligero, mientras el maintainer construye un reemplazo de bajo nivel** (§4).
- **PRECISIÓN 2026-07-26 (medida en el repo, no inferida):** HEAD de Apollo = `adc5c5a0` (**2026-05-21**),
  y **Helios/develop ya tiene el 100% de Apollo** (`develop..upstream/master` = 0 commits). Lo que Apollo
  **dejó de hacer no es commitear, sino SINCRONIZAR con Sunshine**: su último merge del abuelo fue
  `10fd290b` del **2025-09-27**. Por eso la deuda de Helios es con **Sunshine (561 commits)**, no con Apollo.
  No repitas el atajo "Apollo congelado ~10 meses" — es impreciso y llevó a mal-priorizar. Ver
  [[backport-sunshine-a-helios]].

## 1. Qué HACE bien / features maduras (por qué se usa Apollo en vez de Sunshine)
Fuente: README, Discussion #412 ("Differences between Apollo and Sunshine"), foros.
- **Virtual Display integrado con auto-resolución/framerate/HDR** (Windows, vía driver **SudoVDA**): crea un
  monitor virtual al arrancar el stream que calza resolución/aspect/refresh del cliente y lo destruye al
  salir. Es *la* feature estrella y la razón nº1 de migrar desde Sunshine. **Identidad fija por cliente**
  (Windows recuerda la config de display por dispositivo).
- **Sistema de permisos granular por cliente**: el primer cliente pareado obtiene control total; los
  siguientes quedan limitados (ver/listar apps) hasta que se les concede permiso. Sunshine no tenía esto.
- **Input-only mode** (cliente que solo manda input, no ve), útil para couch co-op / multi-usuario.
- **Config y pairing por cliente**, sync de portapapeles host↔cliente, comandos auto pause/resume del juego
  al conectar/desconectar el cliente.
- Reputación de foro: "el fork de Sunshine que arregla la resolución sin scripts". Cliente hermano
  **Artemis** (Moonlight Noir) — hoy Android; otras plataformas "vendrán después" (#937).

## 2. Qué NO hace / fuera de scope
- **Virtual Display NO existe en Linux** — es Windows-only (depende de SudoVDA, un driver de Windows). En
  Linux está "planned" desde siempre; el intento real es el **PR #1477** (§3), aún experimental/sin mergear.
- **HDR:** el maintainer lo desaconseja en general ("depende del cliente, no del host"); en Linux/virtual
  display es tierra de nadie.
- No es multi-usuario headless de verdad (varias sesiones aisladas simultáneas) — ese hueco es justo lo que
  Jordi cubre con **Wolf** en el ecosistema Helios+Wolf+Selene. Apollo = 1 host, 1 escritorio real.
- Base de código = Sunshine ("spaghetti mixed with cement", palabras del maintainer): no espera refactors
  grandes; features nuevas entran como PRs de terceros.

## 3. ⭐ A MEDIO COCER / bugs abiertos / dolor conocido (LA SECCIÓN CLAVE)

### Virtual Display en Linux / Wayland (lo más relevante para Helios)
- **PR #1477 — "Add experimental virtual display support for Linux"** (open, mergeable, 7 commits, activo
  2026-07-24). Crea un display virtual on-the-fly vía **EDID overrides por debugfs**: arma un EDID válido
  según lo que pide el cliente (con hash del client-ID embebido para que el compositor recuerde settings) y
  lo fuerza sobre un conector GPU no usado. **Experimental, sin mergear** — es el candidato a rescatar/portar
  en Helios. https://github.com/ClassicOldSong/Apollo/pull/1477
- **#1161 — "Virtual Display does not work on Linux"** (open, updated 2026-07-25). El síntoma canónico:
  SudoVDA no existe en Linux → poner "always create virtual display" no hace nada. KDE Plasma/KWin Wayland,
  AMD. https://github.com/ClassicOldSong/Apollo/issues/1161
- **#1543 — "Linux (AUR build, CachyOS con Wayland/Plasma KDE) Wrong Monitor / Display"** (open, 2026-07-16).
  **⚠️ ESTE ES EL SETUP EXACTO DE JORDI.** Sin virtual display en Linux, Apollo streamea el **monitor
  equivocado** (agarra un monitor lateral/vertical y lo manda de lado) y **no hay setting** para elegir qué
  display capturar, ni en server ni en cliente. Dolor directo a resolver en Helios.
  https://github.com/ClassicOldSong/Apollo/issues/1543
- **#1492** (closed) — pedía soporte de virtual display en Linux vía **EVDI** (alternativa al enfoque
  debugfs/EDID del #1477). Contexto de diseño para Helios.
- Audio en Linux: **#1422 "Audio crackling when streaming from arch linux"** (open).

### NVIDIA Blackwell / RTX 50 (GPU de Jordi = RTX 5070 Ti)
- **#1162 — "Video decoder failed to initialize – NVENC errors" en RTX 5070 Ti** (open, updated 2026-07-16).
  **⚠️ MISMA GPU QUE JORDI.** Stream falla a inicializar en **Artemis** con NVENC en 5070 Ti, mientras
  **Moonlight funciona con el mismo setup** → apunta a un bug del lado Artemis/negociación, no del encoder.
  https://github.com/ClassicOldSong/Apollo/issues/1162
- **#1534 — "DLSS 4.5 Frame Generation frames not captured in stream"** (open): los frames generados por
  FG no entran a la captura.
- **#1536 — Bluescreen (code 133) "nvlddmkm.sys"** al dejar el laptop idle con Apollo (open).
- La búsqueda `Blackwell/RTX 50xx` da 115 hits — el grueso son NVENC/"Slow Connection"/frame drops, no un
  único bug Blackwell aislado. Blackwell específico se manifiesta sobre todo como **#1162**.

### Virtual Display (Windows) — está lejos de pulido, aun donde "funciona"
- **#1532 — "Virtual Display creation failed, or cannot get created display name in time!"** (open,
  2026-07-21): en laptops Optimus (Intel iGPU + NVIDIA dGPU) SudoVDA falla a crear el display y cae al panel
  físico. https://github.com/ClassicOldSong/Apollo/issues/1532
- **#1544 — "dwm.exe leaks dedicated VRAM (~0.5 MB por redraw) mientras SudoVDA está attach"** (open,
  2026-07-21): **fuga de VRAM sin siquiera streamear** — ~100 MB/min de uso normal de escritorio, dwm llegó
  a 6+ GB; desactivar SudoVDA la corta. https://github.com/ClassicOldSong/Apollo/issues/1544
- **#1461** virtual display hitching; **#1429** el cursor del mouse desaparece en cliente con virtual
  display; **#357** Auto HDR no se activa con virtual display; **#440** limitar resolución máx del VD.

### Crashes / detección de GPU / encoders (Windows, pero patrón heredable)
- **#1147** (top por comentarios, 54) — HDR roto + **no detecta GPU si Windows.Capture beta + servicio**;
  el reporter pide "portable sin servicio" como Sunshine. El maintainer "no arregla bugs de la base"
  (queja recurrente en foro).
- **#1446 — Crash loop ACCESS_VIOLATION en dxgi.dll** por un hook a `NtGdiDdDDIGetCachedHybridQueryValue`
  en Win11 24H2 (RTX 4070 Ti): el servicio reinicia sunshine.exe cada ~6 s indefinidamente.
- **#934 / #1173** GPU no detectada / NVENC cae a software encode ~10-20 FPS; **#892** Error 503 init de
  video; **#825** Apollo usa 100% GPU en idle; **#1061** WGC capturado a 40 FPS máx.

### Red / latencia / audio (el long tail más ruidoso del tracker)
- Familia **"Slow Connection" / freezes / stutter cada 1-3 s**: #608, #1019, #787, #1523, #577, #580,
  #1410 (stutter con mouse, fino con control), #767 (peores frametimes que Sunshine). Es el cluster de
  quejas más grande y difuso.
- Audio: **#1100** (stutter cada segundo en iPhone 17 Pro), #1053, #1428 (PR mic passthrough).

### Input / gamepad / permisos
- **#1412** inputs "atascados" como pressed al quitar permisos de input a un cliente (bug del sistema de
  permisos, la feature estrella de Apollo). **#827** no detecta control en Lies of P. **#1432** conflicto
  con el adaptador Xbox wireless. **#1355** navegación de control hipersensible en menús (key_repeat_freq).

### Seguridad / pairing
- **#1546 — "About Security issues GHSA-6p7j-5v8v-w45h and GHSA-ph75-mgxh-mv57"** (open): hay **2 GHSA
  advisories** contra Apollo; el fix de una (`ph75-mgxh-mv57`) se backporteó en commit #1496 (may-2026).
  Relacionar con el hallazgo propio de Jordi (auth-bypass CVE-2026-32253 en `security-findings-2026-06.local.md`).
- **#1481** (PR) use-after-free en `clientpairingsecret` que crashea el flujo de pairing. **#636** pide
  PIN de un solo uso por conexión. **#1331** no conecta con ningún cliente (Moonlight ni Artemis).

## 4. Roadmap / dirección del maintainer (ClassicOldSong / "Yukino Song")
Citas textuales del maintainer en **#1512** (2026-05 / 2026-06) — clave para la estrategia de Helios:
- *"Apollo itself is **feature complete** at this stage and the original code from Sunshine is a whole mess
  to deal with, I don't want to waste time on it."*
- *"Apollo isn't abandoned... I'm working on something **really low level which Apollo will be rewritten
  completely built on** against."*
- *"1. The original code base is spaghetti mixed with cement. 2. Continuing on this pile is wasting time.
  3. **Planes need runways to take off, I'm building the airport.**"*

**Lectura para Helios:** Apollo está en **congelación deliberada** — el maintainer NO va a invertir en la
base Sunshine; apuesta todo a una **reescritura de bajo nivel** (sin nombre público ni fecha en 2026-07).
Consecuencias: (a) el virtual display de Linux (#1477) y demás mejoras dependen de PRs de terceros, no del
core; (b) Helios NO debería esperar que upstream Apollo resuelva Blackwell/Wayland — ese trabajo lo hace
Jordi o nadie; (c) el ecosistema **Helios+Wolf+Selene** (Wolf para headless multi-usuario) es la apuesta
correcta justo porque Apollo no va hacia ahí.

## 5. Relación con Sunshine (LizardByte) — divergencia y flujo de PRs
- Apollo = fork de **Sunshine**; comparte el protocolo Moonlight y buena parte del árbol. Sunshine **sí**
  sigue publicando releases; Apollo NO (§0) → la brecha se ensancha en fixes de plataforma.
- Apollo **absorbe** de terceros/Sunshine sobre todo fixes de encoder Linux: VAAPI (rate control, CQP en
  Intel xe, presets por códec #1515), AMD AMF (#1539 PR encoder AMF nativo, #1435), Wayland frame timestamps
  (#1430, ya en master). Los backports de seguridad GHSA también bajan (#1496).
- Apollo **añade** lo que Sunshine no tiene río abajo: SudoVDA virtual display, permisos por cliente,
  input-only, sync de clipboard. Ese delta es el "valor Apollo" y lo que Helios hereda.
- Práctico: para seguridad, seguir haciendo `git fetch upstream` desde **Apollo** (no directo de Sunshine),
  porque Apollo ya reconcilió su spaghetti; pero para fixes de encoder Linux vale mirar también Sunshine
  y los PRs abiertos de Apollo listados arriba.

## Fuentes accesibles / no accesibles
- **Accesible:** GitHub REST/Search API (issues, PRs, commits, tags, comentarios) — base de casi todo esto.
  README y Discussion #412 vía WebFetch.
- **Parcial:** las búsquedas de Reddit (r/MoonlightStreaming) vía WebSearch devolvieron sobre todo enlaces a
  GitHub y páginas de release, no threads concretos de Reddit con quejas fechadas — Reddit no rankeó bien
  desde acá. Lo de foros aquí está corroborado contra GitHub, no citado de Reddit directamente.
