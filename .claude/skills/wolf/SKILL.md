---
name: wolf
description: Knowhow de Wolf (games-on-whales) — backend headless multi-sesión del ecosistema Helios+Wolf+Selene. Arquitectura (1 contenedor + apps on-demand, compositor Wayland, perfiles/profile_data, lobbies), Steam multi-usuario, GPU NVIDIA manual, API unix-socket, pain points (issues) y trucos de config. Carga al desplegar/operar/debuggear Wolf o wolf-deploy/.
---

# Wolf (games-on-whales) — destilado operativo

> Backend headless multi-usuario del ecosistema **Helios + Wolf + Selene** de Jordi. Este skill es el
> **destilado accionable**; el detalle exhaustivo vive en las memorias enlazadas al final. Deploy real
> en `wolf-deploy/` (compose custom), repo MIT clonado en `~/code/ajenos/wolf`.
> **Regla de método (aprendida a la mala):** para dudas de ARQUITECTURA lee PRIMERO la doc
> `~/code/ajenos/wolf/docs/modules/{user,dev}/pages/*.adoc` (how-it-works, configuration, wolf-ui,
> lobbies), no solo el código. Los issues EVOLUCIONAN — revalida los números antes de decidir.

## 1. Qué es y su rol
- **Servidor Moonlight PROPIO** (C++/Rust, MIT), **NO** es Sunshine/Apollo ni un wrapper. Selene
  (Moonlight Qt) se conecta a Wolf igual que a Helios: ambos hablan Moonlight.
- **Rol en el ecosistema:** cubre lo que Apollo/Sunshine (=Helios) NO hacen bien → **multi-usuario
  simultáneo con aislamiento docker-per-app REAL** y **desktops virtuales on-demand sin monitor
  físico ni dummy plug**. Reparto de roles: **Helios = host de máquina REAL/single**; **Wolf =
  headless multi-usuario**; **Selene = cliente** de ambos.
- Por eso `#1477` (display virtual en Helios) quedó SOLTADO — superseded por Wolf.
- **Criterio de éxito de Jordi:** Wolf SOLO justifica su lugar si logra **multi-sesión ESTABLE** (2+
  conexiones simultáneas sin crashear). "Que prenda" no basta — Helios ya cubre el single.

## 2. Arquitectura esencial
- **UN contenedor Wolf** que corre siempre y **levanta/baja contenedores de app hermanos on-demand**
  (uno por instancia de app) hablando con `/var/run/docker.sock`. `network_mode: host`.
- **Ciclo de vida:** al cerrar la app su contenedor **se ELIMINA** → todo lo no-montado se pierde.
  Estado persistente sólo por bind-mount (ver perfiles).
- **Compositor = `gst-wayland-display`** (micro-compositor Wayland en Rust/Smithay, plugin GStreamer +
  C API). Expone el framebuffer crudo directo al encode (zero-copy DRM/EGL). **NO soporta XWayland** →
  apps X11 (Steam) corren **Gamescope DENTRO del contenedor** como cliente Wayland que aporta XWayland.
- **Las 3 CAPAS (memoriza esto — es la clave del modelo per-usuario):**
  - **Sesión = la conexión.** N clientes conectados = N sesiones = N app-containers on-demand → de aquí
    sale la SIMULTANEIDAD. Un solo contenedor Wolf multiplexa todo (arquitectura correcta, no workaround).
  - **Perfil = catálogo de apps + ESTADO del usuario** (login/saves de Steam). Se comparte entre TODOS
    los dispositivos de esa persona. Se define en `[[profiles]]` del `config.toml`.
  - **Lobby = opcional, co-op**: 2+ personas a la MISMA instancia de app. NO es lo que Jordi quiere
    (él quiere sesiones aisladas, no co-op).
- **Aislamiento de input (diferencia de raíz con Helios):** mouse/teclado se inyectan
  PROGRAMÁTICAMENTE en el compositor (no crean `/dev/uinput`, no fugan a seat0/KDE). Sólo el **gamepad**
  usa `inputtino`/uinput global, mitigado con udev rules que Wolf shippea (grupo `input` + `seat9` de
  estacionamiento) + `fake-udev` (mknod dentro del contenedor + evento udev en su netns).
- **Protocolo Moonlight estándar:** HTTP(pairing)/HTTPS(applist)/RTSP/Control-ENet(cifrado
  AES-GCM)/Video-RTP(H264/HEVC/AV1)/Audio-RTP(Opus). Pairing muestra PIN → URL `http://<ip>:<http>/pin/#XXXX`.

## 3. Qué hace BIEN
- **Multi-sesión de verdad:** desktops Wayland on-demand, uno por sesión, aislados del host.
- **Aislamiento docker-per-app:** cada app en su contenedor con acceso SOLO a sus devices virtuales.
- **Encode HW auto** (CUDA/NVENC, QuickSync, VAAPI); pipeline GStreamer personalizable por config sin
  recompilar; zero-copy en NVIDIA.
- **Steam de primera clase** (bajo Gamescope/XWayland, ya sin `Privileged`, UI Steam Deck), input rico
  vía inputtino (Xbox/PS5/Switch), audio PulseAudio on-demand. **MIT.**

## 4. Qué NO hace / límites
- **Multi-GPU load-balancing NO existe** — "planned for future releases" (#173/#449). Hoy la GPU por app
  es MANUAL; render + encode deben ir en la MISMA GPU (zero-copy). El "iGPU encoda mientras GPU juega"
  del README es objetivo de diseño, no automático.
- **Wolf UI (perfiles + lobbies) = EXPERIMENTAL/beta** (wolf-ui #3), con crashes en NVIDIA.
- **NO XWayland nativo** (depende de Gamescope, que tiene sus propios issues).
- **Sin dedup de bibliotecas Steam** (#83/#69/#241): cada usuario = su PROPIA copia del juego → 2 Steam =
  2 instalaciones, 2 copias, 2 cuentas (Valve exige cuentas distintas). Planear disco.
- Sin HDR (#222), sin IPv6 (#136, sólo IPv4), sin K8s nativo (#82 → proyecto separado `fenrir`), WSL2 no
  funcional del todo, LXC sólo privilegiado.

## 5. ⚠️ Pain points / a medio cocer (con # de issue — revalida, cambian)
- **#265 — teardown zero-copy NVIDIA rompe OTRAS sesiones · EL RIESGO #1 de multi-sesión.** Cuando
  CUALQUIER sesión se cierra, el teardown EGL/CUDA del pipeline zero-copy tira los pipelines de las demás
  sesiones vivas (`EGL_NOT_INITIALIZED`). **Workaround de bolsillo: `WOLF_USE_ZERO_COPY=FALSE`** (pierde
  performance pero funciona). Fix "de verdad" se persigue en gst-wayland-display PR #36 (ciclo de vida
  del contexto CUDA). El deploy actual corre `TRUE` (zero-copy) porque valida bien; ten `FALSE` como as.
- **Wolf UI #11 — crash "invalid opcode"** (panic Rust `ud2`) a los 2-3 min de conectar; **#4 — fallos
  de arranque en NVIDIA** (socket Wayland/pipeline). El frontend de perfiles/lobbies NO es sólido aún.
- **#410 — Wolf no arranca tras update de drivers NVIDIA** → hay que **RECREAR el driver-volume** tras
  cada bump. (Método manual, ver §6.) Cada `pacman -Syu` que suba el driver lo obliga.
- **Input:** #455 (gamepads virtuales fugan al host desktop → udev), #285 (rumble muere tras
  reconectar), #267 (mapeos mal tras reconectar PS5 hot-plug), #81 (input Steam en unprivileged).
- **#360 — Steam crashea tras update** (webhelper con deps faltantes); **#152 — Steam Wayland "Broken
  pipe"** con container-toolkit + drivers 565.57+ (otra razón para el método manual).
- **#270 — no arranca en reboot** (`restart: unless-stopped`) con NVIDIA → restart manual. Ojo arranque
  desatendido.
- Contenedores huérfanos: PulseAudio y `Wolf-UI_<sid>` persisten tras kill abrupto (reaping imperfecto).

## 6. 🔧 Trucos de config / operación
- **Puertos +1000 (anti-colisión con Helios PROD).** Helios escucha 47989/47990/48010 en la misma caja.
  El deploy mueve TODO +1000 conservando offsets: `WOLF_HTTP_PORT=48989` `WOLF_HTTPS_PORT=48984`
  `WOLF_CONTROL_PORT=48999` `WOLF_RTSP_SETUP_PORT=49010` `WOLF_VIDEO_PING_PORT=49100`
  `WOLF_AUDIO_PING_PORT=49200`. En Selene tecleas SOLO el HTTP `192.168.1.250:48989`; descubre el resto
  del `<serverinfo>`/`sessionUrl0`.
- **GPU NVIDIA = MÉTODO MANUAL (driver-volume), NO container-toolkit.** El toolkit hace panic al
  compositor en esta caja (`Failed to create GsCUDABuf`, wolf#379/#405; A/B probado 2026-07-21). El
  manual monta el driver EXACTO del host en `/usr/nvidia`:
  ```bash
  NV=$(cat /sys/module/nvidia/version)
  curl -s https://raw.githubusercontent.com/games-on-whales/gow/master/images/nvidia-driver/Dockerfile \
    | docker build -t gow/nvidia-driver:latest -f - --build-arg NV_VERSION=$NV .
  docker create --name nvdrv --mount source=nvidia-driver-vol,destination=/usr/nvidia gow/nvidia-driver:latest sh
  docker rm nvdrv
  ```
  **Re-poblar el volumen tras CADA update de driver** (la versión debe cuadrar — #410).
- **`WOLF_RENDER_NODE=/dev/dri/renderD128`** = la RTX 5070 Ti (Blackwell). `renderD129` = iGPU AMD.
  Mantén compositor + encode en la MISMA GPU (no hay load-balancing).
- **Blackwell:** usar `:stable` (mergea PR#425, fix black-screen Blackwell). NO usar el viejo
  `:fix-nvidia-blackwell-zero-copy` (PR#424 descartado sin merge).
- **Ritual de Steam por perfil (o crashea).** El estado de Steam vive en
  `/etc/wolf/profile_data/<profile_id>/WolfSteam/` (POR PERFIL, se comparte entre dispositivos del
  usuario — NO en `app_state_folder`, que es config de INPUT del aparato). Primer arranque de un perfil
  nuevo falla por permisos de `.steam/ubuntu12_32`/`steam-runtime`: **correr Steam una vez (crashea) →
  `chown 1000:1000` + `chmod 777`** (apps corren como uid 1000). Hay `mk_steam_dir.sh` en la doc (ojo
  guion vs guion_bajo en `profile_data`). Biblioteca compartida por bind-mount debe ser owner 1000:1000.
- **API — sólo por unix socket** (`docker exec -i wolf python3` al socket, típico
  `/run/user/wolf/wolf.sock`; el mount de `/etc/wolf` la expone). Exponerla por TCP es "highly
  dangerous" (parea clientes + ejecuta comandos, sin auth propia).
  - Listar/inspeccionar: `GET /api/v1/{apps,profiles,clients,sessions,lobbies}`; spec completo en
    `GET /api/v1/openapi-schema`.
  - Lanzar app: `POST /api/v1/runners/start` `{session_id, runner}` — pero necesita un **session_id
    ACTIVO** + runner-spec = reimplementar Wolf UI. **Smoke-test mecánico sin input NO es práctico**; el
    launch de Steam es inherentemente interactivo (conectar → Wolf UI → Steam → login/2FA).
  - `POST /api/v1/apps/add` **descarta `start_audio_server`** (#465/#466) → apps creadas por API se
    quedan sin audio. Ojo si automatizas.
- **Perfiles de Jordi:** `[[profiles]]` en `/etc/wolf/cfg/config.toml`. Default trae 2:
  `id=moonlight-profile-id` (Wolf UI + Test ball, lo que ve Moonlight directo) e `id=user` (Steam,
  Firefox, RetroArch, Lutris, Pegasus…). Decisión (2026-07-25): perfiles **"unJordi"** y **"Liora"**,
  cada uno con Steam + su `profile_data/<id>/`, PIN opcional; se conectan simultáneos = 2 sesiones.
- **Steam NO es streameable directo** por Moonlight (`stream ... "Steam"` → "no encontrada"): sólo se ven
  las apps del perfil del cliente (Wolf UI + Test ball). Steam se lanza DESDE Wolf UI.
- **Housekeeping:** barrer huérfanos tras kill abrupto:
  `docker ps | grep '^Wolf-UI_' | xargs docker rm -f`.
- **Levantar/parar:** `docker compose -f wolf-deploy/docker-compose.yml up -d | down`. Debug:
  `WOLF_LOG_LEVEL=DEBUG`. Core dumps preservados en `/etc/wolf/cfg/backtrace.*.dump`.

## 7. Lo que YA validamos empíricamente (2026-07-24/25)
> Corrige el miedo histórico "Wolf crashea una y otra vez con 2 sesiones". El arnés fiel es **2 IPs
> distintas** (local `192.168.1.250` + MacBook `192.168.1.84` `Selene.app` por SSH), NO el demo
> single-host (que MIENTE: 2 clientes en el mismo host pelean los puertos UDP de RX → B no recibe).
- ✅ **Wolf es ESTABLE:** NO crasheó en múltiples corridas incl. **compositor** (`Wolf UI` ×2,
  wayland-1/wayland-2 concurrentes). `RestartCount=0`, `OOMKilled=false`, 0 core dumps nuevos.
- ✅ **Entrega video a 2 IPs simultáneas:** Mac decodificó 10 frames HEVC (VideoToolbox) del compositor,
  sostenido; local recibió y decodificó AV1/NVDEC. El "nunca estable con 2 sesiones + compositor" **NO
  se reproduce** — es el hallazgo que da vuelta al abandono histórico.
- ✅ **SOAK 60s Wolf UI ×2** (IPs distintas, ambos legs reales, local forzado a `--video-codec H.264`
  software para render en Xvfb): AMBAS sesiones sostuvieron los 60s, 2 contenedores `wolf-ui:main`
  concurrentes, `RC=0`, `EVP=0`, 0 dumps.
- ✅ **EVP storm = artefacto de MISMO-IP, no bug bloqueante.** La tormenta `EVP_DecryptFinal_ex failed`
  es del canal de CONTROL (ENet), NO del video. Causa raíz confirmada: 2 sesiones desde el MISMO IP →
  el demux de sesión de Wolf (RTSP casa por IP) usa llave equivocada → GCM falla. Tabla: 2 IPs distintas
  ×5 = **0 EVP**; mismo-IP = 125 EVP. **Para Jordi+Liora (2 máquinas = 2 IPs) NO aplica.** Bug latente
  upstream: `handle_openssl_error` (utils.cpp:7-10) no lanza → devuelve plaintext basura en vez de
  descartar.
- ⚠️ **Artefactos del ARNÉS headless (NO de Wolf):** local muere ~16s con `--video-codec auto` porque
  Wolf le negocia AV1→NVDEC-GPU pero `LIBGL_ALWAYS_SOFTWARE=1` sobre Xvfb no presenta el frame (un
  cliente REAL con display no tiene el problema). Contenedores `Wolf-UI_<sid>` huérfanos tras kill -9.
- **FALTA para bill-of-health 100%:** 2 clientes **REALES** sosteniendo video simultáneo (= Jordi+Liora,
  SU QA — hoy sólo hay 1 cliente real, el Mac). El plan QA-Steam (Steam por perfil → soak largo con 2)
  vive en [[forks-helios-selene]] y [[diseno-headless-multisesion]].

## 8. Referencias
- [[wolf-doc-sintesis]] — síntesis exhaustiva de la doc oficial (arquitectura, perfiles, GPU, API, límites que la doc admite).
- [[wolf-issues-forums]] — barrido de issues/PRs/foros con números y estado (fechado 2026-07-25).
- [[forks-helios-selene]] — §⏸️ BACKLOG + Verificaciones 3/4 (lo empírico), plan QA-Steam, razón real del abandono.
- [[diseno-headless-multisesion]] — dirección de diseño, aislamiento de input, por qué Path A (usar Wolf) vs adaptar Helios, encaje del broker.
- Deploy: `wolf-deploy/` (`docker-compose.yml`, `RUNBOOK.md`). Repo MIT: `~/code/ajenos/wolf` (doc en `docs/modules/`).
