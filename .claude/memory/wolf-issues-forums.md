---
name: wolf-issues-forums
description: Barrido de GitHub issues/PRs/discussions + foros sobre Wolf (games-on-whales) — qué hace/no hace, cómo, y qué está a medio cocer (bugs abiertos), con foco en multi-sesión, perfiles/lobbies, NVIDIA Blackwell, Steam. Fechado; los issues cambian.
metadata:
  type: reference
---

# Wolf (games-on-whales) — barrido de issues/PRs/discussions + foros

> **Fecha del barrido: 2026-07-25.** Los issues/PRs EVOLUCIONAN — cada número lleva su estado tal
> como estaba este día; revalida antes de tomar decisiones. Fuentes: API pública y páginas web de
> `games-on-whales/wolf`, `wolf-ui`, `gst-wayland-display`, `fenrir`, docs oficiales y Lemmy.
> **NO accesible:** el Discord de GOW (no indexado en web); Reddit no arrojó hilos concretos de Wolf
> (la búsqueda devolvió juegos homónimos). El roadmap oficial `/dev/roadmap.html` devuelve **404**
> (no existe como página) — la "dirección" se infiere de PRs/issues/discussions, no de un roadmap formal.

---

## 1. Qué HACE bien / maduro

- **Streaming Moonlight multi-sesión de verdad**: crea desktops virtuales Wayland **on-demand**, uno
  por sesión, totalmente aislados del host. Puedes jugar en el monitor físico del host mientras otra
  persona tiene su sesión en otro dispositivo. Es su razón de ser y funciona.
- **Aislamiento docker-per-app**: cada app corre en su contenedor con acceso SOLO a los dispositivos
  virtuales creados para ella. Imágenes pre-horneadas en `games-on-whales/gow` (Steam, Firefox, etc.).
- **Pipeline de encode HW**: GStreamer con soporte automático CUDA / QuickSync / VAAPI; pipeline
  personalizable por config (sin recompilar). Zero-copy en NVIDIA (ver §3 y §4).
- **Input rico vía `inputtino`**: emula gamepads (Xbox, PS5/DualSense, Switch Pro, Joy-Con), teclado,
  ratón, uinput/uhid. Rumble y hot-plug soportados (con bugs, §4).
- **Audio** vía PulseAudio standalone + libpulse (virtual sink on-demand). Trivial, sin HW accel.
- **Steam como app de primera clase**: corre bajo Gamescope (XWayland), ya NO requiere `Privileged`,
  soporta la UI Steam Deck. Multi-cliente.
- **Sentimiento comunitario (Lemmy)**: "impresionante", "muy prometedor", Steam Deck como cliente muy
  valorado, deploy docker accesible. Pero "rough around the edges" reconocido por los propios usuarios.

## 2. Qué NO hace / no soporta (fuera de scope o rechazado)

- **Kubernetes nativo — NO** (issue **#82**, 35 comentarios, enhancement/help-wanted). Requeriría
  rediseño grande. La respuesta oficial es el proyecto SEPARADO **`fenrir`** ("controla instancias
  distribuidas de Wolf en K8S") — inmaduro/experimental, no es Wolf-core.
- **IPv6 — NO** (issue **#136**, bug/enhancement): Wolf solo escucha en IPv4; pedido, sin implementar.
- **HDR — NO** (issue **#222**, 15 comentarios): sistema headless reporta "no HDR support" aunque el
  cliente tenga pantalla HDR. Pedido, abierto.
- **Bibliotecas de juego compartidas / dedup entre usuarios — NO (aún)** (issues **#83** 34 comentarios,
  **#69**, **#241**): con multi-usuario, **cada usuario instala su PROPIA copia del juego**. Se propone
  usar overlays de Docker; sigue en discusión, no implementado. ← relevante para Jordi (2 Steam = 2 copias).
- **LXC nativo**: discutido como probablemente **fuera de scope** (discussion #229); la gente lo mete en
  LXC/Proxmox por su cuenta (#457) pero no es soportado oficialmente.
- **WSL2**: limitaciones de render GPU documentadas, no funcional del todo (#129, discussion #448).
- **Apps del host / desktop nativo como app**: pedido (discussions #232, #354), no soportado directo.

## 3. CÓMO lo hace (implementación, de docs + issues)

- **Compositor por sesión**: `gst-wayland-display`, micro-compositor Wayland en **Rust/Smithay**. Expone
  el framebuffer crudo directo al pipeline de encode. Para apps que necesitan XWayland (Steam) corre
  **Gamescope** como cliente Wayland dentro de la sesión.
- **GPU**: GStreamer encoda; en NVIDIA hay **pipeline zero-copy CUDA** (`waylanddisplaysrc !
  video/x-raw(memory:CUDAMemory) ! nvh265enc`) implementado en gst-wayland-display **PR #20** (merged
  2025-10-22, resuelve wolf#179). Selección explícita de GPU en multi-GPU dentro de ese pipeline.
- **Input**: `inputtino` abstrae uinput/uhid. **Gotcha de diseño**: los devices virtuales SON visibles
  en el host y "pueden romper el aislamiento del host" → se mitiga con reglas **udev** que reasignan los
  devices a otro *seat* y restringen acceso por grupo. (De ahí los bugs #81, PR #455.)
- **Audio**: PulseAudio container + virtual sink por libpulse.
- **Protocolo**: plugins GStreamer propios para RTP, split de paquetes y **FEC**, para cumplir el
  protocolo Moonlight. Pairing HTTP/HTTPS estándar Moonlight.
- **Wolf UI** (frontend): app especial que Moonlight lanza; muestra **perfiles** (con PIN opcional) y
  **lobbies** (varios usuarios a la MISMA instancia = co-op). Hotkey para volver a Wolf UI
  (Ctrl+Alt+Shift+W o START+UP+RB en gamepad). Arquitectura de lobby "client-independent".

## 4. ⚠️ A MEDIO COCER / bugs abiertos / dolor conocido (LA SECCIÓN CLAVE)

### 4a. Multi-sesión / multi-usuario simultáneo
- **#265 — "Multiple users from a single IP unable to connect"** · ABIERTO, 26 comentarios · **MUY
  RELEVANTE PARA JORDI.** Root cause confirmado por el maintainer: cuando **cualquier** sesión se cierra,
  el **teardown EGL** del pipeline zero-copy NVIDIA **rompe los pipelines de las OTRAS sesiones vivas**
  (`EGL_NOT_INITIALIZED`, `gluploadelement` no maneja buffers DMA-DRM). Es un bug NVIDIA del zero-copy,
  no del "mismo IP" en sí (el IP compartido solo lo dispara/expone). **Workaround oficial:
  `WOLF_USE_ZERO_COPY=FALSE`** (desactiva zero-copy → funciona pero pierde performance). Fix "propio"
  se persigue en gst-wayland-display (los PRs de ciclo de vida de contexto, ver 4c). → Jordi: aunque sus
  2 usuarios estén en máquinas/IPs distintas, el **teardown de una sesión afectando a la otra** puede
  morderle igual; tener `WOLF_USE_ZERO_COPY=FALSE` como as bajo la manga.
- **discussion #401 — "Multiple Sessions Single Profile"** · ABIERTO, sin respuesta del maintainer.
  ¿Un mismo perfil desde 2 dispositivos a la vez? Sin respuesta oficial; el usuario propone copiar el
  perfil. **No hay guía clara todavía.**
- **#173 / #449 — load-balancing multi-GPU** · ABIERTO: hoy la asignación de GPU por app es MANUAL; no
  hay balanceo automático entre GPUs. #449 pide además probe de capacidades y switching dinámico.
- **#233 — encoders NVIDIA no usados con múltiples GPUs NVIDIA** · CERRADO (nov-2025) por el reporter;
  fallo era CUDA context no válido con varias GPUs (caía a x264 software). Menos aplica al setup de Jordi
  (1 NVIDIA + iGPU AMD) pero ojo si algún día mete 2ª NVIDIA.

### 4b. Perfiles / lobbies (Wolf UI) — BETA, con crashes
- **wolf-ui #11 — "wolf-ui crashes with 'invalid opcode'"** · ABIERTO · **crash serio**: panic de Rust
  (instrucción `ud2`) que tumba wolf-ui **a los 2-3 min** de conectar por Moonlight, y deja el sistema
  inestable (kernel page faults después). ← el frontend de perfiles/lobbies NO es sólido aún.
- **wolf-ui #4 — "Issues when running wolf-ui on Nvidia"** · ABIERTO: fallos de arranque del contenedor,
  errores de montaje del socket Wayland y negociación de pipeline GStreamer **en hardware NVIDIA**.
- **wolf-ui #3 (pinned) — "Beta Feedback"**: confirma que perfiles + lobbies (single y co-op) están en
  **fase beta**, recolectando feedback. **#2 Todo List**: per-app settings y `wayland_render_node` aún
  pendientes. → Conclusión: **lobbies/perfiles son EXPERIMENTALES**; útiles para probar, no "listos".

### 4c. NVIDIA — Blackwell / RTX 50xx (encode, zero-copy)
- **#425 (PR) — "bump gst-wayland-display to fix Blackwell black screen"** · **MERGED 2026-06-08**.
  Root cause: la derivación de formato desde DRM fourcc solo manejaba YUV → buffers **AR24 (BGRA)** con
  layout equivocado → pantalla negra en zero-copy. Fix: bump a commit `c49af96`
  (`gst_video_dma_drm_fourcc_to_format`). **Probado en RTX 5080, driver 610.43.02, CUDA 13**, NVENC OK.
  (Esto es lo que Jordi ya tiene por `:stable`.) Relacionado: discussion **#324 "Insisting on using AR24"**.
- **gst-wayland-display PRs activos (el trabajo NVIDIA vivo, jun-2026):**
  - **#36 (ABIERTO) — "take own ref on GstCudaContext to fix double-unref at teardown"**: arregla el
    ciclo de vida del contexto CUDA en el teardown ← **directamente el mecanismo detrás de #265**.
  - **#39 (cerrado) — "dmabuftocuda for Nvidia NV12 encode"**, **#40 — negociar modifier NV12 del
    encoder**, **#37 (abierto) — Vulkan NV12 + VA/Vulkan encode**, **#41 (merged) — doc de la patch
    DPB de vkh264enc**. Todo esto es la migración del path NVIDIA hacia **NV12/Vulkan** más robusto.
- **wolf PR #450 (DRAFT) — "wolf:vulkan image (native Vulkan NV12 encode) + switch a Fedora"**: dirección
  a futuro — encode H.264 NV12 nativo por Vulkan y **cambio de base Ubuntu 22.04 → Fedora** (para Mesa/
  libs más nuevas, ver #330). Aún draft.
- **#124 — "Nvidia: container toolkit → EGL exception"** · ABIERTO (label `cannot reproduce`):
  `Failed to create EGLDisplay: NotInitialized`; falta `libnvidia-egl-gbm` / versiones incompatibles con
  Ubuntu 22.04. **Workaround = método MANUAL de driver volume** (el que Jordi YA usa). Docs ahora
  recomiendan el manual. → refuerza por qué el toolkit se evita.
- **#410 — Wolf no arranca tras actualizar drivers NVIDIA** · el fix es **RECREAR el driver volume**
  tras cada bump de driver (RTX 3080, pero aplica general). ← recordatorio operativo para Jordi: cada
  `pacman -Syu` que suba el driver NVIDIA obliga a regenerar el volumen manual del driver.
- **#270 — "wolf fails to start at system restart (restart: unless-stopped)"** · ABIERTO, label nvidia:
  crashea al bootear con drivers NVIDIA, requiere restart manual. Ojo para arranque desatendido.
- **#330 — "Outdated Mesa in GOW images fails with newer HW"** · ABIERTO: Mesa viejo de las imágenes GOW
  da errores VPE en AMD reciente (afecta a la iGPU AMD de Jordi si la usara para encode/compositor).

### 4d. Steam en Wolf
- **#152 — "[Steam only] Wayland events: Broken pipe"** · ABIERTO, 23 comentarios, bug · **RELEVANTE.**
  EGL falla con **NVIDIA container toolkit + drivers nuevos (565.57+)**: `eglInitialize() failed`,
  `failed to read Wayland events: Broken pipe`, steamwebhelper crashea. **NVIDIA-específico.**
  Workaround: **driver volume MANUAL** (Jordi ok), o downgrade de driver, o Gamescope en vez de Sway
  (ayuda poco). El maintainer admite que el toolkit es "too hit-and-miss".
- **#360 — "Steam crashing after update"** · ABIERTO: el contenedor Steam falla tras update; webhelper
  con dependencias faltantes. Riesgo latente cuando Valve actualiza.
- **#39 (histórico) — problemas Steam** · en su día se arreglaron muchos (quitar Privileged, UI Deck,
  multi-cliente) vía @Drakulix. Base de por qué hoy Steam funciona.
- **#241 / #83 / #69** (ya en §2): cada usuario = su propia instalación de Steam/juego; sin dedup.
  Valve impone límites a instancias separadas de Steam (cuentas distintas; multiplayer del mismo juego
  exige que cada cuenta lo posea). ← **planificación para Jordi + pareja: 2 libs, 2 copias, 2 cuentas.**
- **#463 — "CEMU (Pegasus) can't be set up"** (no-Steam pero app): menú de settings no cierra, cambios
  no persisten.

### 4e. Input (gamepads / teclado / mouse) y audio
- **PR #455 (ABIERTO) — "stop Wolf virtual controllers leaking into the host desktop"**: los gamepads
  virtuales se filtran al escritorio del host; fix vía reglas udev. ← el gotcha de aislamiento del §3.
- **#81 — "Fix Steam input in unprivileged containers"** · ABIERTO, 21 comentarios: los controllers
  virtuales de Steam necesitan relay de eventos **udev** para funcionar en contenedores no privilegiados.
- **#285 — "No rumble after reconnecting"** · ABIERTO, bug: el rumble muere tras desconectar/reconectar
  un control sin reiniciar contenedores.
- **#267 — "Controller reconnected with wrong mappings (Steam hot-plugging)"** · ABIERTO: PS5 se detecta
  distinto tras reconectar → mapeos mal.
- **#171 — "Controller controlled Mouse is invisible"** · ABIERTO, bug: el puntero movido por stick no
  se dibuja.
- **PR #464 — DualSense battery siempre lee "low" en Steam** (fix de enum). **PR #445** — unificar
  controllers vía factory de inputtino (Xbox/PS5/Switch Pro/Joy-Con). **PR #459** — teclas modificadoras
  (Shift/Ctrl) se sueltan al instante con VK codes side-specific. **#395** — layout de teclado se
  resetea al presionar modificador con múltiples layouts XKB. **#46** — emular botón GUIDE.
- **#437 — Pen/tablet (stylus) input no llega a las apps** · ABIERTO: el compositor no expone el
  protocolo tablet de Wayland.
- **PR #466 / #465 — la API `POST /api/v1/apps/add` descarta `start_audio_server`** → apps creadas por
  API se quedan **sin audio**. Fix en review. ← ojo si Jordi automatiza apps por API.

### 4f. Crashes / cleanup / contenedores huérfanos / apps
- **PulseAudio container persiste tras apagar Wolf** (Lemmy + observado): contenedor de audio huérfano.
- **#357 — cambios de PR #347 rompieron los tests de las bindings .NET** · ABIERTO.
- **#248 — "DOOM Dark Ages launches to black screen"** (21 comentarios) y **#226 "Black screen but app
  is running"**: pantallas negras app-específicas (más allá del bug Blackwell ya arreglado).
- **discussion #256 — pairing timeout demasiado corto** para Moonlight; **#324 AR24**; **#205 DRM device
  error con NVIDIA**. Errores de config con mensajes poco descriptivos (queja recurrente en Lemmy).

## 5. Roadmap / dirección (inferida — NO hay roadmap formal; `/dev/roadmap.html` = 404)

- **Encode NVIDIA robusto sin el bug de teardown**: migración a **NV12 / Vulkan** en gst-wayland-display
  (PRs #36/#37/#39/#40) + imagen **`wolf:vulkan`** y base **Fedora** (wolf PR #450). Esto apunta a
  cerrar #265 y a Mesa/libs modernas (#330). Es el frente técnico más activo hoy.
- **Wolf UI (perfiles + lobbies co-op)**: en **beta activa** (wolf-ui #3), camino a ser el entrypoint
  estándar; faltan estabilidad en NVIDIA (#4), no crashear (#11), per-app settings y `wayland_render_node`.
- **Fenrir**: orquestación multi-instancia de Wolf en K8S (respuesta a #82) — separado, incipiente.
- **inputtino unificado** (PR #445) y saneo de fugas de input al host (PR #455).
- **Pendientes de comunidad recurrentes**: dedup de bibliotecas Steam (#83/#69), HDR (#222), IPv6 (#136),
  multi-GPU load-balancing (#173/#449). Ninguno con fecha.

## 6. Comparación rápida con alternativas (solo lo que emergió)

- **vs Sunshine/Apollo (lo de Jordi: Helios)**: Wolf es headless-first y **multi-usuario simultáneo con
  aislamiento docker-per-app REAL** — justo lo que Apollo/Sunshine NO cubren bien (por eso el ecosistema
  Helios+Wolf+Selene del proyecto). El costo: Wolf NO comparte bibliotecas de juego entre usuarios (cada
  quien su copia) y su capa de perfiles/lobbies (Wolf UI) es beta con crashes en NVIDIA.
- No apareció comparación sustantiva con Neko u otros en las fuentes barridas.

---

## TL;DR accionable para el caso de Jordi (2 usuarios Steam simultáneos, RTX 5070 Ti Blackwell)

1. **Blackwell H264/pantalla-negra: ya resuelto** en `:stable` (PR #425, probado en RTX 5080). Bien.
2. **El riesgo #1 de 2 sesiones a la vez es #265**: el teardown EGL/CUDA zero-copy de UNA sesión puede
   romper la OTRA. Aunque sean 2 máquinas/IPs distintas, ten a mano **`WOLF_USE_ZERO_COPY=FALSE`**.
   El fix "de verdad" vive en gst-wayland-display PR #36 (ciclo de vida del contexto CUDA) — vigilar.
3. **Método MANUAL de driver volume = correcto** (confirmado por #124, #152, #410): el container-toolkit
   da EGL/Broken-pipe en NVIDIA. Y **recrea el volumen tras cada bump de driver** (#410).
4. **Perfiles/lobbies (Wolf UI) son BETA** y crashean en NVIDIA (wolf-ui #4, #11). Para 2 usuarios con
   sus Steam separados, más seguro **2 apps/sesiones independientes** que confiar en lobbies co-op hoy.
5. **2 Steam = 2 instalaciones/copias/cuentas** (sin dedup: #83/#69/#241). Planear disco y cuentas.
6. **Input**: revisar fuga de gamepads virtuales al host (PR #455) y rumble/hot-plug con bugs (#285/#267).
