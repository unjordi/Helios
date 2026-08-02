---
name: sunshine-doc-sintesis
description: Síntesis de la documentación oficial de Sunshine (LizardByte, abuelo de Helios) — arquitectura, captura/encoders, config, plataformas, multi-cliente, limitaciones. Referencia del ecosistema Helios+Wolf+Selene.
metadata:
  type: reference
---

# Sunshine (LizardByte) — síntesis de la documentación oficial

> **Abuelo de Helios.** Linaje: **Sunshine** (LizardByte) → **Apollo** (ClassicOldSong) → **Helios** (unjordi).
> Fuente primaria de esta síntesis: la copia de docs que Helios/Apollo **conservó de Sunshine** en
> `/home/unjordi/code/HeliosSelene/Helios/docs/` (los `.md` siguen diciendo "Sunshine" y son
> textualmente los de upstream) + la doc oficial <https://docs.lizardbyte.dev/projects/sunshine/latest/>
> y el README de LizardByte <https://github.com/LizardByte/Sunshine>. Lo que Helios AÑADE encima está
> en `Helios/README.md` y en `Helios/docs/design/virtual-display-linux.md` (marcado abajo como "Helios/Apollo").

## 1. Qué ES Sunshine y su propósito

Sunshine es un **host de game-stream self-hosted, compatible con el protocolo Moonlight** — la
alternativa libre (GPLv3) a NVIDIA GameStream. Corre en tu PC (Windows/Linux/macOS), captura el
escritorio/juego, lo codifica por hardware con **baja latencia**, y lo sirve a clientes **Moonlight**
(y sus forks: Selene/Artemis/Moonlight-Qt/Android). Config y emparejamiento vía **web UI**. Es el
componente HOST; Moonlight/Selene es el CLIENTE. (`README.md`, `getting_started.md`)

## 2. Arquitectura

### 2.1 Pipeline general
`Captura de pantalla (GPU) → codificación HW (FFmpeg + API del vendor) → FEC/paquetización →
transporte UDP (protocolo Moonlight) → cliente`. Audio se captura por loopback del sink del sistema.
Input del cliente (teclado/ratón/gamepad/pen/touch) se **inyecta** en el host (gamepad virtual emulado).

### 2.2 Captura — POR PLATAFORMA (config `capture`, auto-selección en orden)
En Linux el orden de auto-selección del código es **NvFBC → Wayland(wlr-screencopy) → KMS → X11**
(`Helios/docs/design/virtual-display-linux.md` cita `src/platform/linux/misc.cpp` `display()`).
Métodos (`configuration.md#capture`):
- **`nvfbc`** (Linux/NVIDIA): NVIDIA Frame Buffer Capture, directo a memoria GPU, normalmente el más
  rápido en NVIDIA. **NO tiene soporte Wayland nativo, no funciona con XWayland.** Requiere CUDA.
- **`wlr`** (Linux/Wayland): captura para compositores basados en **wlroots** vía
  `wlr-screencopy-unstable-v1` (puede capturar displays virtuales, p.ej. en Hyprland).
- **`kms`** (Linux): captura DRM/KMS desde el kernel. **Requiere capability `cap_sys_admin`**
  (`sudo setcap cap_sys_admin+p $(readlink -f $(which sunshine))`). Es la vía para la mayoría de
  escritorios Wayland (KDE/GNOME). **Único backend que soporta captura HDR en Linux.**
- **`x11`** (Linux): vía XCB. El más lento y con más CPU — evitar si se puede.
- **`ddx`** (Windows): DirectX **Desktop Duplication API**, bien soportado. Solo captura desde la GPU
  que maneja el display conectado.
- **`wgc`** (Windows, beta): `Windows.Graphics.Capture`. **No compatible con el servicio de Sunshine.**
- macOS usa captura vía screen recording (con permiso del sistema).

### 2.3 Encoders (config `encoder`, auto: primero disponible)
- **`nvenc`** (NVIDIA) — presets P1..P7 (`nvenc_preset`), two-pass (`nvenc_twopass=quarter_res` def),
  spatial AQ, VBV increase, y flags Windows-only (`nvenc_realtime_hags`, `nvenc_latency_over_power`,
  `nvenc_opengl_vulkan_on_dxgi`), CAVLC vs CABAC.
- **`quicksync`** (Intel) — `qsv_preset`, `qsv_coder`, `qsv_slow_hevc`.
- **`amdvce`** (AMD AMF, Windows) — `amd_usage=ultralowlatency`, `amd_rc=vbr_latency`, VBAQ, HRD, etc.
- **`vaapi`** (Linux AMD/Intel) — VA-API; `vaapi_strict_rc_buffer`. (En Linux AMD/Intel el encode VAAPI
  puede ir en una GPU distinta a la de captura.)
- **VideoToolbox** (macOS) — `vt_coder`, `vt_software`, `vt_realtime`.
- **`software`** (CPU, libx264/x265) — `sw_preset=superfast`, `sw_tune=zerolatency`.
Códecs: **H.264, HEVC (Main/Main10 HDR), AV1 (8/10-bit)** — negociados con el cliente vía
`hevc_mode`/`av1_mode` (0 = anunciar según capacidad del encoder).

### 2.4 Pairing / seguridad
Emparejamiento por **PIN** (cliente pide, host mete el PIN en la web UI). TLS con **cert auto-firmado
RSA-2048** (`pkey`/`cert`; ojo: no todos los clientes Moonlight soportan ECDSA ni RSA≠2048).
Cifrado de stream configurable por LAN/WAN (`lan_encryption_mode`/`wan_encryption_mode`: 0/1/2). Web UI
en **https://localhost:47990**; acceso limitable por origen (`origin_web_ui_allowed`: pc/lan/wan).
NOTA de seguridad Helios: los puertos abiertos a Internet en el deploy de Jordi = **diseño
intencional** (acceso remoto), ver `security-findings-2026-06.local.md`.

### 2.5 Web UI + config
Toda la config se edita por la web UI (o el `sunshine.conf`). `apps.json` define las apps lanzables.
Config en `~/.config/sunshine` (Linux/macOS), `%ProgramFiles%\Sunshine\config` (Windows).
Categorías (`configuration.md`): **General** (locale, nombre, log level, `global_prep_cmd`, system tray),
**Input** (controller/gamepad tipo ds4/ds5/switch/x360/xone, keyboard, mouse, pen/touch, keybindings),
**Audio/Video** (audio_sink, virtual_sink, adapter_name, output_name, **familia `dd_*` de display device =
solo Windows** — resolución/refresh/HDR auto-match, remapping de modos, `max_bitrate`, `minimum_fps_target`),
**Network** (upnp, port base **47989**, encryption, ping_timeout), **Config Files**, y **Advanced**
(`fec_percentage`, `qp`, `min_threads`, `hevc_mode`, `av1_mode`, `capture`, `encoder`).

## 3. Plataformas y diferencias
- **Windows** (recomendado/mejor soportado): instalador; **Desktop Duplication API**; HDR **oficial**;
  virtual display nativo (SudoVDA en Apollo/Helios); toda la familia `dd_*`; corre como servicio.
- **Linux** (bien soportado, con matices): AppImage/deb/Flatpak/Arch/Fedora/Homebrew; captura KMS
  (necesita `cap_sys_admin`) / NvFBC / wlr / X11; VAAPI o NVENC; HDR **experimental**; servicio
  `systemctl --user`. Mesa a veces trae encode HW deshabilitado por temas legales (hay que recompilar).
- **macOS** (**experimental**): solo Homebrew; **gamepads NO funcionan**; solo captura de micrófono
  (para audio de sistema hace falta BlackHole/Soundflower); Command keys no se reenvían.

## 4. Multi-cliente / sesiones
- Sunshine upstream está pensado para **una sesión de escritorio a la vez**: "cuando se lanza una app,
  si ya había una corriendo, se termina" y "correr múltiples instancias no se aconseja"
  (`getting_started.md`). Puede servir a un cliente sobre el escritorio real (single-seat).
- Multi-instancia / multi-virtual-display es un **workaround** documentado en el wiki de Apollo (arrancar
  varias instancias), no multi-seat de primera clase.
- **Apollo/Helios añaden**: gestión de **permisos por-cliente**, modo input-only, clipboard sync,
  comandos on-connect/disconnect. El **primer** cliente emparejado obtiene permisos completos; los
  siguientes solo `View Streams`+`List Apps` (`Helios/README.md`).
- El **multi-usuario headless real** (varias sesiones aisladas simultáneas) NO lo cubre Sunshine/Apollo
  → por eso el ecosistema de Jordi mete **Wolf** (games-on-whales) como backend headless multi-sesión
  (ver `diseno-headless-multisesion.md`).

## 5. Protocolo Moonlight y compatibilidad
Sunshine **reimplementa el lado servidor del protocolo Moonlight** (control HTTP/HTTPS en el puerto
base 47989/47990 + RTSP/ENet + video/audio/control sobre **UDP**, FEC). Compatible con cualquier
cliente Moonlight (Moonlight-Qt/Android/iOS/embedded, y los forks Selene/Artemis). Host y cliente
comparten `moonlight-common-c`: **si el protocolo/handshake de capacidades cambia, host y cliente deben
avanzar ALINEADOS** (regla central del proyecto Helios/Selene). El handshake negocia códec, HDR,
resolución, gamepad, cifrado.

## 6. Limitaciones / caveats / lo experimental (lo que la doc ADMITE)
- **Wayland es el punto débil:** NvFBC **no soporta Wayland ni XWayland**; captura de la mayoría de
  escritorios Wayland **falla sin `cap_sys_admin` (KMS)** (`getting_started.md` lo marca como WARNING).
- **KMS en NVIDIA** puede dar **pantalla negra** si no se pone `nvidia_drm.modeset=1` en el kernel
  cmdline (`troubleshooting.md`). (Coincide con el gotcha del deploy de Jordi: la cap es por-archivo y
  hay que re-aplicarla cada rebuild.)
- **HDR en Linux = experimental:** solo por backend **KMS** (ni NvFBC ni X11), solo Intel/AMD vía VAAPI
  (HEVC Main10 / AV1 10-bit), y exige compositor con soporte HDR (**Gamescope o KDE Plasma 6**).
- **Mesa** deshabilita encode/decode HW por defecto (legal) → error `Function not implemented`, hay que
  recompilar Mesa con `-Dvideo-codecs=h264enc,h265enc`.
- **AMD**: latencias altas de encode con Mesa <24.2 (Sunshine ya setea `AMD_DEBUG=lowlatencyenc`).
- **macOS**: experimental, sin gamepads, sin audio de sistema nativo.
- **Windows**: `wgc` no va con el servicio; Desktop Duplication solo captura la GPU del display.
- **Virtual display en Linux/Wayland NO existe en upstream** — `virtual-display: true` es **no-op** en
  Linux y la capability ni se anuncia al cliente (`Helios/docs/design/virtual-display-linux.md` §2).
  El stopgap es un `gamescope` dedicado capturado por KMS (con efectos colaterales sobre el monitor
  físico). Esto es **precisamente la feature flagship que Helios persigue** (EDID-override vía debugfs,
  Apollo PR #1477; + hook `pre_probe_cmd` estilo Sunshine PR #4762).
- Red: bursts cada 16 ms (60fps) pueden desbordar buffers si el host es mucho más rápido que el enlace
  del cliente (packet loss); se mitiga con traffic-shaping o bajando la velocidad de la NIC.

## 7. Relevancia para Helios (qué HEREDA vía Apollo)
Helios hereda de Sunshine **todo el núcleo**: pipeline de captura/encode multiplataforma, los backends
(KMS/NvFBC/wlr/X11 en Linux), los encoders (NVENC/VAAPI/QSV/AMF/sw), el protocolo Moonlight server, la
web UI, el sistema de config (`sunshine.conf`/`apps.json`), pairing por PIN + TLS, y el binario que
**sigue llamándose `sunshine`** (build en `build/sunshine`; unit `apollo.service`). Apollo le sumó
permisos por-cliente, clipboard, comandos connect/disconnect, input-only y **virtual display Windows
(SudoVDA)**. Helios encima apunta al **virtual display nativo Linux/Wayland** (lo que Sunshine/Apollo
no tienen) + docs Linux-first. En la máquina de Jordi (CachyOS + NVIDIA Blackwell RTX 5070 Ti): captura
**KMS** (con `cap_sys_admin`, `nvidia_drm.modeset=1`) + encode **NVENC** — exactamente el camino que
esta doc marca como el soportado-pero-con-caveats en Linux/NVIDIA.

## Referencias
- `Helios/docs/getting_started.md` — instalación por plataforma, setup KMS, HDR, uso, shortcuts.
- `Helios/docs/configuration.md` — TODAS las categorías de config (3013 líneas): captura, encoders, dd_*.
- `Helios/docs/troubleshooting.md` — KMS-NVIDIA black screen, Mesa HW encode, AMD latency, red.
- `Helios/docs/design/virtual-display-linux.md` — mapa de arquitectura del código (captura/VD) + plan Helios.
- `Helios/README.md` — linaje Sunshine→Apollo→Helios y qué añade cada capa.
- Oficial: <https://docs.lizardbyte.dev/projects/sunshine/latest/> · <https://github.com/LizardByte/Sunshine>
