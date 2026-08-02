---
name: sunshine
description: Knowhow de Sunshine (LizardByte) — el host base del que Helios desciende (Sunshine→Apollo→Helios). Arquitectura de captura/encode (NvFBC/Wayland/KMS/X11 → NVENC/VAAPI), config, plataformas, por qué es single-seat (multi-user NO es objetivo → por eso Wolf), y sus pain points en Wayland/NVIDIA/Blackwell que Helios hereda. Carga al debuggear captura/encode/config del host o al rebasear Helios contra Sunshine.
---

# Sunshine (LizardByte) — el host base que Helios hereda

> **Linaje:** **Sunshine** (LizardByte, GPLv3) → **Apollo** (ClassicOldSong) → **Helios** (unjordi).
> Todo lo que Sunshine hace bien o mal río arriba, Helios lo HEREDA salvo que Apollo/Helios lo hayan
> tocado. Este skill destila el knowhow de la BASE; el fork y el protocolo tienen sus propios skills
> (ver §7). Release de referencia del barrido: **v2026.516.143833** (2026-05-16).

## 1. Qué es y su rol

Host de **game-stream self-hosted** compatible con el **protocolo Moonlight** — la alternativa libre a
NVIDIA GameStream. Corre en tu PC (Windows/Linux/macOS), **captura** el escritorio/juego, lo **codifica
por hardware** con baja latencia, y lo sirve a clientes Moonlight (y forks: Selene/Artemis/Moonlight-Qt/
Android). Es el componente HOST; Moonlight/Selene es el CLIENTE. Config y pairing por **web UI**.

- **Es el abuelo de Helios.** Helios hereda TODO el núcleo: pipeline captura/encode, backends, encoders,
  protocolo Moonlight server, web UI, config (`sunshine.conf`/`apps.json`), pairing PIN+TLS.
- **El binario de Helios TODAVÍA se llama `sunshine`** (build en `build/sunshine`, p.ej.
  `sunshine-0.0.0.<hash>`; la unit systemd en la máquina de Jordi es `apollo.service`, del paquete AUR).
  Al debuggear el host de Helios buscas procesos/logs/binarios llamados `sunshine`, no "helios".

## 2. Arquitectura captura → encode

**Pipeline:** `captura de pantalla (GPU) → encode HW (FFmpeg + API del vendor) → FEC/paquetización →
transporte UDP (protocolo Moonlight) → cliente`. Audio por loopback del sink del sistema. El input del
cliente (teclado/ratón/gamepad/pen/touch) se **inyecta** en el host (gamepad virtual emulado).

### Captura — auto-selección Linux EN ORDEN: `NvFBC → Wayland(wlr) → KMS → X11`
(código: `src/platform/linux/misc.cpp` `display()`). Config `capture`.
- **`nvfbc`** (NVIDIA): directo a memoria GPU, normalmente el más rápido en NVIDIA. Requiere CUDA.
  **NO soporta Wayland ni XWayland** → en el escritorio Wayland de Jordi NO es la vía.
- **`wlr`** (Wayland/wlroots): `wlr-screencopy-unstable-v1`; puede capturar displays virtuales (Hyprland).
  KDE/KWin **no es wlroots** → esta vía no aplica en el stack de Jordi (usa portal XDG o KMS).
- **`kms`** (Linux/DRM-KMS): **la vía para la mayoría de escritorios Wayland (KDE/GNOME)** y **único
  backend con HDR en Linux**. **Requiere `cap_sys_admin`**:
  `sudo setcap cap_sys_admin+p $(readlink -f $(which sunshine))`. La cap es **por-archivo** → cada
  rebuild (el binario lleva hash de versión) hay que **re-aplicarla** o el KMS monitor list sale vacío
  → "probing failed" → todos los encoders fallan.
- **`x11`** (XCB): el más lento y con más CPU; evitar.
- **Wayland tb vía XDG Desktop Portals** (`portal::dbus_t`, GNOME/KDE); v2026.516 metió captura
  event-driven + timestamp por "ready timestamp" de Wayland para el portal.
- Windows: **`ddx`** (Desktop Duplication API, sólido; solo captura la GPU del display) / **`wgc`** (beta,
  NO va con el servicio). macOS: screen recording con permiso del sistema.

### Encoders — auto: el primero disponible. Config `encoder`.
- **`nvenc`** (NVIDIA) — presets P1..P7, `nvenc_twopass=quarter_res`, spatial AQ, CABAC/CAVLC, flags
  Windows-only. **Es lo que encoda el deploy en vivo de Jordi** (NVENC en la RTX 5070 Ti, KMS captura).
- **`quicksync`** (Intel) · **`amdvce`** (AMD AMF, Windows) · **`vaapi`** (Linux AMD/Intel; el encode
  puede ir en GPU distinta a la de captura) · **VideoToolbox** (macOS) · **`software`** (libx264/x265,
  fallback CPU — a donde cae todo cuando CUDA/NVENC fallan).
- **Códecs:** H.264, **HEVC (Main / Main10 HDR)**, **AV1 (8/10-bit)**. Negociados con el cliente
  (`hevc_mode`/`av1_mode`, 0 = anunciar según capacidad). Fallback automático de códec (AV1→HEVC→H.264).
- **FEC** (`fec_percentage`) + transporte **UDP** del protocolo Moonlight (control HTTP/HTTPS 47989/47990
  + RTSP/ENet + video/audio/control UDP). Bursts cada 16 ms (60fps) pueden desbordar buffers si el host
  es mucho más rápido que el enlace del cliente → packet loss (mitigar con traffic-shaping).

## 3. Config

Todo se edita por **web UI** (`https://localhost:47990`) o el `sunshine.conf`. `apps.json` define apps
lanzables. Ruta: `~/.config/sunshine` (Linux/macOS), `%ProgramFiles%\Sunshine\config` (Windows). El
`configuration.md` son **3013 líneas**; categorías:
- **General** — locale, nombre, log level, `global_prep_cmd`, system tray.
- **Input** — tipo de gamepad (ds4/ds5/switch/x360/xone), keyboard, mouse, pen/touch, keybindings.
- **Audio/Video** — `audio_sink`, `virtual_sink`, `adapter_name`, `output_name`, `max_bitrate`,
  `minimum_fps_target`; la **familia `dd_*` (display device) es SOLO Windows** (resolución/refresh/HDR
  auto-match, remapping de modos).
- **Network** — `upnp`, puerto base **47989**, encryption (`lan_encryption_mode`/`wan_encryption_mode`
  0/1/2), `ping_timeout`.
- **Advanced** — `fec_percentage`, `qp`, `min_threads`, `hevc_mode`, `av1_mode`, **`capture`**, **`encoder`**.

**Pairing / seguridad:** por **PIN** (cliente lo pide, host lo mete en la web UI). TLS con cert
auto-firmado **RSA-2048** (ojo: no todo cliente Moonlight soporta ECDSA ni RSA≠2048). Acceso a la web UI
limitable por origen (`origin_web_ui_allowed`: pc/lan/wan). En el deploy de Jordi los puertos abiertos a
Internet son **diseño intencional** (acceso remoto), no falla — pero el auth debe estar sano (ver §6).

## 4. Multi-sesión — SINGLE-SEAT (por qué existe Wolf)

- Sunshine expone **UNA sesión de escritorio a la vez**. "Cuando se lanza una app, si ya había una
  corriendo, se termina" y "**correr múltiples instancias no se aconseja**" (`getting_started.md`).
- **Multi-usuario aislado NO es objetivo de diseño.** Los devices de una sesión pisan la otra. La
  respuesta canónica de LizardByte para multi-user aislado es **games-on-whales / Wolf** o contenedores
  (discussions #262, #770) — exactamente la decisión ya tomada en `wolf-deploy/`.
- **#3865 "Second client inherits 120 FPS from first client" → cerrado *not planned*.** No hay pipelines
  de encoding independientes por cliente; el 2º hereda FPS/settings del 1º. Multi-cliente con settings
  distintos está **fuera de scope**.
- **Apollo/Helios SÍ añaden** (encima de Sunshine): permisos por-cliente (el 1º emparejado = permisos
  completos; los demás solo `View Streams`+`List Apps`), modo input-only, clipboard sync, comandos
  on-connect/disconnect. Pero eso NO es multi-seat real.
- **Reparto del ecosistema:** Helios/Sunshine = 1 sesión de escritorio; **Wolf** = multi-usuario headless
  aislado (ver `diseno-headless-multisesion.md`).

## 5. ⚠️ Pain points Wayland/NVIDIA/Blackwell (lo que Helios hereda)

El stack de Jordi (**CachyOS + KDE/KWin Wayland + NVIDIA RTX 5070 Ti Blackwell + iGPU AMD**, captura KMS,
encode NVENC) es *exactamente* el terreno donde Sunshine es más frágil. Helios NO arregla esto por ser
fork — hay que vigilarlo/parcharlo.

### Wayland + KWin (captura)
- **#4884 "XWayland (KWin Wayland) stuttering" — ABIERTO.** En **CachyOS + KDE Plasma/KWin Wayland** la
  mayoría de juegos bajo XWayland stutterean fuerte al streamear (probado con Cyberpunk 2077). **Afecta
  TANTO al portal XDG como a KMS.** Sospecha: cambios de KWin post-6.5/6.6. Workaround:
  `PROTON_ENABLE_WAYLAND=1` (juego nativo Wayland) lo elimina en casi todos, con glitches en algunos.
  **Mismo stack que Jordi → alto riesgo de heredarlo.**
- **#3189 tearing/VSync en KDE/GNOME Wayland**, **#3203 monitores rotados mal con KMS** (45 comentarios,
  el más comentado), **#3298 HDR muy oscuro tras Plasma 6.2** — todos del combo KDE/KWin Wayland.
- **NvFBC no soporta Wayland/XWayland** (por eso KMS es la vía). **#2472**: NvFBC usa flag NOWAIT → hasta
  16 ms de frame viejo (latencia extra) en la ruta X11/NvFBC.

### NVIDIA / Blackwell (RTX 50) / NVENC
- **KMS + NVIDIA = pantalla negra** si no está `nvidia_drm.modeset=1` en el kernel cmdline
  (`troubleshooting.md`).
- **CUDA no carga → NVENC cae a software.** Patrón común en logs: `Cannot load libcuda.so.1` →
  `Could not dynamically load CUDA` → `Failed to create a CUDA device: Operation not permitted` → cae a
  `libx264`. En Blackwell se enreda con los **módulos de kernel ABIERTOS** (obligatorios en RTX 50):
  apps no compatibles con el open kernel module fallan CUDA aunque ffmpeg encode bien en la misma máquina.
- **#4567 "GPU performance drop tied to stream framerate" — ABIERTO** (Plasma 6 Wayland, RTX 5090
  Blackwell, driver 580). La utilización de GPU **baja** al subir el framerate (70% a 144fps vs 99% sin
  streamear); ocurre con KMS **y** NvFBC, con NVENC **y** software; reproducido en NixOS y Bazzite (no es
  config de distro). Sin causa raíz. Blackwell + Wayland + cualquier captura = degradación sin explicar.
- **#2250 "KMS + Headless + Nvidia: Unknown Monitor connector type [Meta]" — ABIERTO** (42 comentarios,
  *help wanted*). El monitor virtual de GNOME headless se reporta con connector "Meta" que el KMS de
  Sunshine no reconoce → falla la captura (workaround: X11). **Es la pared "headless + NVIDIA + KMS" que
  motivó irse a Wolf.**
- **HDR en Linux = experimental:** solo backend KMS, solo Intel/AMD vía VAAPI (HEVC Main10 / AV1 10-bit),
  exige compositor con soporte HDR (Gamescope o KDE Plasma 6).

### 🔴 Regresiones de encoder en la release que Helios QUIERE (tensión con el CVE)
La v2026.516.143833 trae el fix de **CVE-2026-32253** (por el que Helios NO revierte al binario AUR) pero
esa misma base arrastra regresiones de captura/NVENC:
- **#5147 "capture frame rate significantly lower than in-game" — ABIERTO** (regresión de v2026.516).
- **#5217 "v2026.516.143833 broke hardware enc on older nvidia cards" — ABIERTO**
  (`NvEncOpenEncodeSessionEx() failed: NV_ENC_ERR_INVALID_VERSION` → cae a software; PR #5451 relacionado).
- **Implicación dura para el rebase:** al bumpear Helios sobre upstream reciente por el CVE, **auditar la
  ruta NVENC/captura, NO solo compilar verde.** Un build verde ≠ NVENC sano. Verificar en el journal que
  encoda por hardware (h264/hevc/av1) y no cayó a `libx264`.

## 6. Seguridad / CVEs

- **CVE-2026-32253 — Auth bypass (crítico).** El callback custom de verificación OpenSSL en
  `src/crypto.cpp` trata ciertos errores como éxito → certs no confiables acceden a endpoints HTTPS
  protegidos. Afecta TODO `< v2026.516.143833`; fix en esa versión. **Es el CVE que Helios tiene
  codificado como test rojo y por el que NO se revierte al binario AUR** (ver `security-findings-2026-06.local`).
- **CVE-2025-53095 — CSRF en la web UI (CVSS 9.7, crítico).** La web UI no protegía contra CSRF → command
  injection como Administrator. Fix en v2025.628.4510.
- **CVE-2025-54081 — Unquoted Service Path (Windows).** Ejecución local como SYSTEM. Solo Windows,
  irrelevante para el deploy Linux de Jordi.

## 7. Referencias

- Investigación fuente: [[sunshine-doc-sintesis]] (docs oficiales digeridas) · [[sunshine-issues-forums]]
  (barrido GitHub/foros, 2026-07-25, con los #issue).
- Contexto del fork: [[forks-helios-selene]] · [[security-findings-2026-06.local]] (el CVE) ·
  [[diseno-headless-multisesion]] (por qué Wolf).
- Skills hermanos: **`helios`** (el fork y lo que AÑADE sobre Sunshine) · **`moonlight`** (el protocolo
  compartido host↔cliente — cambia alineado o se rompe la compatibilidad).
- Docs upstream conservadas en Helios: `Helios/docs/{getting_started,configuration,troubleshooting}.md` ·
  `Helios/docs/design/virtual-display-linux.md`. Oficial: docs.lizardbyte.dev · github.com/LizardByte/Sunshine.
