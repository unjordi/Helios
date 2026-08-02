---
name: wolf-doc-sintesis
description: Síntesis EXHAUSTIVA de la documentación oficial de Wolf (games-on-whales) leída del repo local — arquitectura, multi-sesión, perfiles, lobbies, apps/Steam, GPU, API, limitaciones que la doc admite. Base de referencia para el ecosistema Helios+Wolf+Selene.
metadata:
  type: reference
---

# Wolf (games-on-whales) — síntesis de la documentación oficial

> Fuente: repo local `/home/unjordi/code/ajenos/wolf/docs/modules/**/*.adoc` + `README.md`.
> Leída completa el 2026-07-25. Citas al `.adoc` de origen entre paréntesis.
> Contexto de Jordi: Wolf `:stable` en CachyOS, NVIDIA RTX 5070 Ti (Blackwell) + iGPU AMD,
> método MANUAL de driver-volume (bug del container-toolkit), objetivo 2 usuarios Steam simultáneos.

---

## 1. Qué ES Wolf y qué hace

- **Servidor de streaming open-source (licencia MIT) para clientes Moonlight** que permite **compartir UN solo host entre múltiples clientes remotos** para jugar videojuegos (README.md, ROOT/index.adoc).
- No es de propósito general: si quieres una solución genérica de streaming, la propia doc te manda a **Sunshine** (README.md). Wolf es "una herramienta específica para una necesidad específica".
- Objetivos de diseño primarios (ROOT/index.adoc):
  1. **Multi-usuario**: varios usuarios streameando contenido DISTINTO compartiendo un mismo hardware.
  2. **Escritorios virtuales on-demand** a cualquier resolución/FPS **sin monitor físico ni dummy plug**.
  3. **Multi-GPU simultáneo** para trabajos distintos (ej: encodear en iGPU mientras se juega en la GPU dedicada).
  4. **Baja latencia** de video/audio con soporte pleno de gamepads.
  5. **Linux + Docker first**: correr juegos con bajos privilegios en contenedores (basado en el proyecto Games-On-Whales / GOW).
  6. **Hackeable**: casi todo (pipelines de encoding, GPU, detalles low-level de Docker/Podman) se edita en el `config.toml`.
- Casos de uso reales: cada usuario abre su propio Steam/Firefox/emuladores (Pegasus), co-op local vía "lobbies", máquina de juegos compartida en casa.

## 2. Arquitectura

### Modelo de contenedores
- **UN contenedor Wolf** que corre siempre; **levanta y baja contenedores de app adicionales bajo demanda** (quickstart.adoc: "Wolf runs as a single container, it'll spin up and down additional containers on-demand").
- El contenedor Wolf necesita acceso al **socket de Docker** (`/var/run/docker.sock`) — es literalmente quien crea/destruye los contenedores de app hablando con el Docker daemon del host (quickstart.adoc, code-structure.adoc `docker.hpp`).
- **Ciclo de vida de la app**: al abrir/cerrar la app desde Moonlight el contenedor de la app **se ELIMINA**. Todo lo que no esté en un mount se pierde → garantiza consistencia (apps/index.adoc). Estado persistente sólo vía bind-mounts (ver §4).

### Componentes internos (how-it-works.adoc)
- **Escritorio virtual**: micro-compositor Wayland propio, **`gst-wayland-display`** (basado en **Smithay**, escrito en Rust; expone un plugin GStreamer y una C API). Su gracia: **expone el framebuffer crudo** para alimentarlo directo al pipeline de encoding de GStreamer. No soporta XWayland → para apps que lo necesitan (Steam) se usa **Gamescope como cliente Wayland** que aporta XWayland aguas abajo.
- **Audio virtual**: PulseAudio como contenedor/servidor standalone + `libpulse` para crear sinks de audio virtuales on-demand (no necesita HW accel).
- **Input virtual**: librería **`inputtino`** que abstrae `uinput`/`uhid` para crear gamepads/mouse/teclado virtuales. Estos devices son visibles en el host y **pueden romper el aislamiento** → por eso las udev rules que los restringen a un grupo (`input`) y los mueven a otro `seat` (how-it-works.adoc + quickstart.adoc `_virtual_devices_support`).
- **Streaming**: GStreamer encodea video/audio; auto-detecta HW accel (CUDA/NVENC, QuickSync, VAAPI). Plugins propios `rtpmoonlightpay_video` / `rtpmoonlightpay_audio` que parten el bitstream en paquetes RTP Moonlight + FEC (Reed-Solomon) (how-it-works.adoc, dev/gstreamer.adoc).

### Sesión de streaming end-to-end (protocols/index.adoc + subpáginas)
6 protocolos, en orden:
1. **HTTP** (TCP 47989, sin cifrar): pairing — intercambio de info pública.
2. **HTTPS** (TCP 47984, cifrado, sólo clientes pareados): lista de apps, llaves de cifrado, disparar el inicio del stream.
3. **RTSP** (TCP 48010, sin cifrar): negocia puertos y settings de los 3 streams siguientes. Mensajes OPTIONS→DESCRIBE→SETUP→ANNOUNCE→PLAY (rtsp.adoc; parser PEG).
4. **Control sobre ENet** (UDP 47999, cifrado **AES-GCM 128**): inputs de usuario + info extra de stream. Fork custom de ENet con IPv4/IPv6. Seq# como IV. Tipos: START_A/B, INPUT_DATA, RUMBLE_DATA, MOTION_EVENT, RGB_LED, IDR_FRAME, HDR_MODE, TERMINATION, etc. (control-specs.adoc).
5. **Video sobre RTP** (UDP 47998/48100, sin cifrar): H.264 / HEVC / **AV1**.
6. **Audio sobre RTP** (UDP 48000/48200, cifrado **AES-CBC 128**): Opus.
- **Pairing** (http-pairing.adoc): 5 fases. Moonlight manda salt+cert cliente → Wolf pide PIN al usuario → deriva `AES_KEY = SHA256(SALT+PIN)` → intercambio de challenges firmados con los certs → `paired=1`. Fase 5 corre sobre HTTPS validando el cert del cliente. En la práctica: Moonlight muestra un PIN, Wolf loguea una URL `http://<ip>:47989/pin/#XXXX` donde metes el PIN (quickstart.adoc).

### Compositor Wayland (wayland.adoc — el "por qué")
- Alternativa a `ximagesrc`/X11 (que exige Xorg corriendo + monitor/dummy/EDID y **no escala con múltiples clientes**). Wayland usa **direct rendering**: cliente y compositor comparten buffer de video-memory; con **DRM + Mesa/EGL** se exportan handles de GPU **sin copiar a CPU**.
- **Zero-copy**: la sección vieja decía que se copiaba por host-memory por el lío de los *modifiers* de DMA buffers; hay un **[OUTDATED]** que admite que **ya se logró soporte completo de zero-copy** con DMA buffers en GStreamer (wayland.adoc:213-215). Ver caveat en §12.

## 3. Multi-sesión / multi-usuario

- Wolf está **diseñado para múltiples sesiones de streaming simultáneas**, cada una potencialmente con una app distinta (configuration.adoc `data_setup`).
- El modelo funcional del código (inmutable, sin globals, event-bus) es lo que "nos da el poder de soportar múltiples usuarios concurrentes sin esfuerzo" (code-structure.adoc).
- **Qué se aísla y cómo**:
  - **Un contenedor Docker por app/sesión** → aislamiento de proceso/FS entre usuarios.
  - **Escritorio Wayland virtual independiente** por app con `start_virtual_compositor`.
  - **Input aislado por contenedor**: los devices virtuales se `mknod`ean **sólo dentro del contenedor correcto** vía `--device-cgroup-rule` + cap `MKNOD` (nunca `--privileged`), y los eventos udev se emiten sólo a ese contenedor vía **fake-udev** dentro de su network namespace (fake-udev.adoc). Esto es lo que evita que el gamepad de un usuario aparezca en la sesión del otro.
  - **Data persistente aislada por perfil+app** (§4).
- **Pipeline GStreamer multi-cliente** (lobbies.adoc, diagrama): un `interpipe_sink` recibe el framebuffer crudo del compositor y se ramifica a un pipeline por cliente (`interpipe_src → escalado de resolución → encoding H264/HEVC/AV1 → UDP sink`). Cada cliente puede tener **su propia resolución/encoder**.

## 4. Perfiles (profiles) y estado persistente

- **Perfil = un usuario / caso de uso, cada uno con su propio set de apps** (configuration.adoc `_profiles`). Se listan en `profiles = [...]` del `config.toml`.
- Perfil especial **`moonlight-profile-id`**: define **qué apps ve directamente un cliente Moonlight** al conectarse. Por defecto trae 2 apps: **Wolf UI** (contenedor lanzador) y **Test ball** (pipeline dummy de GStreamer para verificar que todo jala). ← coincide con la config real de Jordi.
- Perfil de ejemplo **`user`** (name "User"): las apps "reales" (Firefox, Steam, ...) que se muestran DENTRO de Wolf UI. ← el segundo perfil de Jordi.
- Campos de un perfil (configuration.adoc `_profile_example`):
  - `id` (string único, uso interno), `name` (mostrado en Wolf UI), `icon_png_path` (box art), **`pin = [3,2,1,4]`** (opcional; si está, **Wolf UI pide ese PIN** para mostrar la lista de apps del perfil — protección por perfil, wolf-ui.adoc).
- **Estado persistente ligado al perfil** (configuration.adoc `data_setup`): Wolf crea automáticamente una carpeta por instancia de app en el host:
  `${HOST_APPS_STATE_FOLDER}/profile_data/${profile_id}/${app_title}` → se monta como `/home/retro` dentro del contenedor de la app (bind mount `-v .../profile_data/<id>/<title>:/home/retro`).
  - `HOST_APPS_STATE_FOLDER` por defecto `/etc/wolf`; Wolf **resuelve la ruta del host dinámicamente** aunque montes `/etc/wolf` en otro lado (ej. `-v /mnt/drive/wolf:/etc/wolf` → los datos van a `/mnt/drive/wolf/profile_data/...`).
  - `profile_id` = identificador del perfil; `app_title` = `title` de la app.
- **Ejemplo de perfil de configuration.adoc** (el default de dos apps):
  ```toml
  `profile_data/<id>`
  id = 'moonlight-profile-id'
      [[profiles.apps]]
      title = 'Wolf UI'
      start_virtual_compositor = true
      icon_png_path = "https://.../wolf_ui_icon.png"
          [profiles.apps.runner]
          type = 'docker'
          name = 'Wolf-UI'
          image = 'ghcr.io/games-on-whales/wolf-ui:main'
          env = ['GOW_REQUIRED_DEVICES=/dev/input/event* /dev/dri/* /dev/nvidia*',
                 'WOLF_SOCKET_PATH=/var/run/wolf/wolf.sock', 'WOLF_UI_AUTOUPDATE=False', 'LOGLEVEL=INFO']
          mounts = ['/var/run/wolf/wolf.sock:/var/run/wolf/wolf.sock']
          base_create_json = '''{"HostConfig":{"IpcMode":"host",
            "CapAdd":["NET_RAW","MKNOD","NET_ADMIN","SYS_ADMIN","SYS_NICE"],"Privileged":false,
            "DeviceCgroupRules":["c 13:* rmw","c 244:* rmw"]}}'''
      [[profiles.apps]]
      title = 'Test ball'
      start_audio_server = false
      start_virtual_compositor = false
          [profiles.apps.runner]
          type = 'process'
          run_cmd = "sh -c \"while :; do echo 'running...'; sleep 10; done\""
          [profiles.apps.audio]
          source = 'audiotestsrc wave=ticks is-live=true'
          [profiles.apps.video]
          source = 'videotestsrc pattern=ball flip=true is-live=true ! video/x-raw, framerate={fps}/1'
  ```
- El `config.toml` se lee **UNA vez al arranque** (o se crea si no existe) y **Wolf sólo lo modifica al parear un cliente nuevo** (configuration.adoc). Estructura raíz: `hostname`, `support_hevc`, `config_version=2`, `uuid`, `paired_clients=[]`, `profiles=[]`, `gstreamer={}`.

## 5. Lobbies

- **Lobby = varios usuarios conectados a la MISMA instancia de app** (co-op / misma sesión) (wolf-ui.adoc, faq.adoc, lobbies.adoc).
- Se crean **desde Wolf UI**: al lanzar una app puedes "crear un lobby" para que múltiples usuarios se conecten a la misma instancia; aparece como un lobby en la primera página bajo el selector de perfiles (wolf-ui.adoc).
- Habilitados por GStreamer: por ser todo plugins buffer-in→buffer-out, se puede **cambiar en tiempo real la fuente de video de una sesión** de un source a otro con muy poco código — eso es lo que hace posibles los lobbies (faq.adoc).
- La doc de implementación (`dev/lobbies.adoc`) está literalmente en **`WIP...`** (sólo el diagrama del pipeline multi-cliente). Ver §12.

## 6. Apps / runners

### Catálogo documentado
- **Wolf UI** (lanzador/gestor de perfiles y lobbies), **Test ball** (dummy), **Firefox**, **Steam**, **Pegasus** (frontend de emuladores: RetroArch, PCSX2, Xemu, RPCS3, CEMU, Dolphin), **Prism Launcher** (Minecraft). Imágenes en `ghcr.io/games-on-whales/*` (mismo set que el proyecto GOW). Apps "grises" en Wolf UI = no instaladas, se descargan (pull) desde la UI (wolf-ui.adoc, apps/index.adoc).
- Construir apps propias: clonar repo **GOW**, editar/crear imágenes en `images/`, build con `--build-arg BASE_APP_IMAGE=...`, apuntar `image` en el `config.toml`. Jerarquía `base → base-app → <app>` (apps/index.adoc).

### Runner spec (configuration.adoc `_app_runner`)
Dos tipos: **`process`** (corre un `run_cmd` en el propio host de Wolf) y **`docker`** (lo normal). Campos de una app:
- `title`, `icon_png_path` (URL / ruta relativa a `HOST_APPS_STATE_FOLDER` / ruta absoluta; PNG 200x266 ó 628x888).
- **`start_virtual_compositor`** (bool): `true` si la app necesita el compositor Wayland virtual (la propia doc admite "TODO: document this better").
- `start_audio_server` (bool), `app_state_folder`.
- `[profiles.apps.runner]` docker: `name`, `image`, `mounts=[]` (bind mounts extra, ej `/media/data/games:/games:rw`), `env=[]`, `devices=[]`, `ports=[]`, **`base_create_json`** (JSON crudo de la Docker Container-Create API — aquí van `HostConfig.IpcMode`, `CapAdd`, `Privileged:false`, `DeviceCgroupRules`, `Binds`).
- `[profiles.apps.video]` / `[profiles.apps.audio]`: override del pipeline GStreamer (variables `{fps}`, `{width}`, `{height}`, `{color_range}`, `{color_space}`).
- **Compositor de ventana dentro de la app**: env `RUN_SWAY=1` (default) o `RUN_GAMESCOPE=1`. Si ambos, gana Gamescope. Gamescope aporta XWayland pero tiene issues conocidos (§12).
- **Layout de teclado**: `XKB_DEFAULT_LAYOUT`/`XKB_DEFAULT_VARIANT`, o archivo `90-custom.conf` montado en `/etc/sway/config.d/` (sólo Sway, no Gamescope).
- **UID/GID**: apps corren como **1000:1000** por defecto; se cambia con `WOLF_DEFAULT_RUN_UID`/`_GID` (sólo afecta clientes NUEVOS) o per-cliente desde Wolf UI (quickstart.adoc).

### Steam a fondo (apps/steam.adoc)
- **Lanzar un juego Steam directo desde Moonlight**: copiar el bloque de Steam, cambiar `title` y añadir env `STEAM_STARTUP_FLAGS=steam://rungameid/<appid>` (appid desde steamdb.info).
- **Overlay de Steam NO funciona** en el contenedor headless → usar **MangoHud** para FPS/stats. Vulkan/Proton: MangoHud ya viene activado; OpenGL nativo: `mangohud %command%` por juego. Toggle in-game: `RightShift+F12` (mostrar/ocultar), `RightShift+F11` (posición).
- **Proton alternativo (ProtonGE)**: extraer el tarball en `/etc/wolf/profile_data/${profile_id}/WolfSteam/.steam/debian-installation/compatibilitytools.d/` (crear el dir si no existe; requiere haber arrancado el perfil al menos una vez), reiniciar Steam, elegir la versión en Compatibility.
- **Biblioteca compartida**: `mounts = ['/path/to/steamapps:/home/retro/.steam/debian-installation/steamapps:rw']`. **Debe ser owner 1000:1000 y permisos amplios**.
- **Gotcha de permisos en primer arranque de un usuario nuevo**: falla porque `/home/retro/.steam/ubuntu12_32/` y `.../steam-runtime` no tienen permisos. Fix: correr Steam una vez (crashea), luego `chown 1000:1000` + `chmod 777` — hay un script `mk_steam_dir.sh` en la doc que lo automatiza (usa `profile-data`, ojo con guion vs guion-bajo). **Relevante para el caso de 2 usuarios Steam de Jordi**: cada perfil nuevo necesita este ritual de primer arranque.

## 7. GPU (quickstart.adoc, configuration.adoc `_multiple_gpu`, environment-considerations.adoc, troubleshooting.adoc)

- **NVIDIA — dos métodos**:
  - **Container Toolkit**: `--gpus=all`, `NVIDIA_DRIVER_CAPABILITIES=all`, `NVIDIA_VISIBLE_DEVICES=all`. La doc lo marca **"isn't recommended at the moment, not as stable as the manual method"** (issue #152). ← alineado con el bug wolf#379 que Jordi evita.
  - **Manual (driver-volume)**: construir imagen `gow/nvidia-driver` con `NV_VERSION=$(cat /sys/module/nvidia/version)`, poblar volumen `nvidia-driver-vol`, pasar `NVIDIA_DRIVER_VOLUME_NAME` + montar `/usr/nvidia` + todos los `/dev/nvidia*`. **Downside admitido: hay que RE-crear el volumen cada vez que actualizas los drivers** (hay script en troubleshooting.adoc). ← este es el método de Jordi.
  - Requisitos comunes: driver `>= 530.30.02`, módulo `nvidia-drm` cargado con **`modeset=1`** (`cat /sys/module/nvidia_drm/parameters/modeset` → `Y`; si no, `nvidia-drm.modeset=1` en GRUB).
- **Multi-GPU**: `WOLF_RENDER_NODE` (default `/dev/dri/renderD128`) selecciona la GPU. Identificar cuál es cuál con `ls -l /sys/class/drm/renderD*/device/driver`.
- **Dos partes usan HW accel**: (a) *App render node* (crear escritorios Wayland + correr la app) y (b) *encoding de GStreamer* (H.264/HEVC). **Como Wolf usa zero-copy, conviene MANTENER ambos en la misma GPU** (configuration.adoc). ← para Jordi: compositor + encoding ambos en la NVIDIA (validado empíricamente que el compositor renderiza en NVIDIA).
- **Multi-GPU load-balancing = "planned for future releases"** (aún no existe). Ver §12.
- **Zero-copy**: soportado (DMA buffers), mejor latencia; refuerza tener render+encode en la misma tarjeta.
- WSL2 sólo mouse/teclado sin uinput/uhid; Proxmox LXC sólo privilegiado; Podman quadlets soportado.

## 8. Config (configuration.adoc)

- **Archivo TOML** (`WOLF_CFG_FILE`, default `/etc/wolf/cfg/config.toml`) + ENV vars.
- **ENV vars clave**: `WOLF_LOG_LEVEL` (INFO...TRACE), `WOLF_CFG_FILE`, `WOLF_PRIVATE_KEY_FILE`/`_CERT_FILE`, `XDG_RUNTIME_DIR` (default `/tmp/sockets`, sockets PulseAudio/video), `PULSE_SERVER` (si unset, Wolf arranca PulseAudio embebido; setéalo para usar PipeWire/Pulse externo), `WOLF_PULSE_IMAGE` (fallback sidecar), `WOLF_STOP_CONTAINER_ON_EXIT` (TRUE; a FALSE para no borrar contenedores al cerrar), `WOLF_WAYLAND_SOCKET_WAIT_TIMEOUT_MS` (5000), `WOLF_DOCKER_SOCKET` (**no soporta tcp "yet"**), `NVIDIA_DRIVER_VOLUME_NAME`, `HOST_APPS_STATE_FOLDER` (/etc/wolf), `WOLF_RENDER_NODE`, `WOLF_DOCKER_FAKE_UDEV_PATH`, `WOLF_DEFAULT_RUN_UID`/`_GID` (1000). Debug: `RUST_BACKTRACE`, `GST_DEBUG`.
- **Puertos** (todos configurables por ENV `WOLF_HTTP_PORT`, `WOLF_HTTPS_PORT`, `WOLF_CONTROL_PORT`, `WOLF_RTSP_SETUP_PORT`, `WOLF_VIDEO_PING_PORT`, `WOLF_AUDIO_PING_PORT`): 47984/tcp HTTPS, 47989/tcp HTTP, 47999/udp Control, 48010/tcp RTSP, 48100/udp Video, 48200/udp Audio.
- **GStreamer**: `gstreamer.video.hevc_encoders` / `h264_encoders` / `av1_encoders` — listas ordenadas; **el primer encoder que GStreamer logra inicializar se usa** (auto-selección por HW).
- **Overrides per-cliente pareado** (`[paired_clients.settings]`): `controllers_override=["PS","XBOX"]` (por slot), `motion_controller_override`, `mouse_acceleration`, `v_scroll_acceleration`, `h_scroll_acceleration`. Tipos de pad: `auto`/`xbox`/`nintendo`/`ps`. Auto-detección: clientes UNKNOWN con giroscopio → DualSense.
- Quickstart Docker requiere: `--network=host`, montar `/etc/wolf`, `/var/run/docker.sock`, `/dev/`, `/run/udev`, devices `/dev/dri /dev/uinput /dev/uhid`, `--device-cgroup-rule "c 13:* rmw"`. udev rules en `/etc/udev/rules.d/85-wolf-virtual-inputs.rules` para restringir devices virtuales al grupo `input`/`seat9`.

## 9. La API (dev/api.adoc)

- **REST API sólo por UNIX socket** (path = `WOLF_SOCKET_PATH`, típico `/var/run/wolf/wolf.sock`; montar el dir al host para acceder desde fuera del contenedor).
- Uso: `curl --unix-socket /var/run/wolf/wolf.sock http://localhost/api/v1/...`.
- **OpenAPI**: `GET /api/v1/openapi-schema` devuelve el spec completo (el `spec.json` embebido es la referencia real de endpoints; la doc lo renderiza con Scalar).
- Endpoints citados en docs: `GET /api/v1/openapi-schema`, `PATCH /api/v1/clients/<client_id>/settings` (ej. cambiar `motion_controller_override`). La API permite **parear clientes, ejecutar comandos arbitrarios, gestionar sesiones/apps**.
- **Exponerla por TCP es "highly dangerous"** — puede parear clientes y ejecutar comandos; si se hace, via reverse proxy nginx `proxy_pass http://unix:/var/run/wolf/wolf.sock` y asegurar bien. Ver §12.

## 10. Input (protocols/input-data.adoc, control-specs.adoc, fake-udev.adoc)

- Inputs viajan por el **control stream (ENet, cifrado)**. Tipos `INPUT_DATA`: `MOUSE_MOVE_REL/ABS`, `MOUSE_BUTTON_UP/DOWN`, `KEY_UP/DOWN`, `MOUSE_SCROLL/HSCROLL`, `TOUCH`, `PEN`, `CONTROLLER_MULTI/ARRIVAL/TOUCH/MOTION/BATTERY`, `HAPTICS`, `UTF8_TEXT`.
- **Gamepads**: `CONTROLLER_ARRIVAL` anuncia tipo (Unknown/XBOX/PS/Nintendo) y capabilities (analog triggers, rumble, trigger rumble, touchpad, accel, gyro, battery, RGB LED). `CONTROLLER_MULTI` lleva estado (con `active mask` para detectar desconexión). Eventos servidor→cliente: `RUMBLE_DATA`, `RUMBLE_TRIGGERS`, `MOTION_EVENT` (pide gyro/accel; off por defecto para ahorrar ancho de banda), `RGB_LED`. Muchas features son "new in Moonlight 5.0.0".
- **fake-udev** (fake-udev.adoc): el mecanismo clave del aislamiento de input. Wolf crea el device virtual (inputtino/uinput), lo `mknod`ea DENTRO del contenedor (via `--device-cgroup-rule` + `MKNOD`, sin `--privileged`), genera las entradas DB en `/run/udev/data/` y **emite el evento udev falso** (`NETLINK_KOBJECT_UEVENT`, grupo `GROUP_UDEV`, como root) **dentro del network namespace del contenedor** — así sólo esa app ve el hotplug, logrando hotplug real + aislamiento. Desconectar = revertir los pasos. Es un CLI (`src/fake-udev`) instalado en los contenedores.

## 11. Troubleshooting conocido (troubleshooting.adoc)

- **Pantalla negra + cursor en Moonlight**: normal en el primer arranque de una app (Wolf está bajando la imagen Docker + updates). Si persiste → ver logs con `WOLF_LOG_LEVEL=DEBUG`.
- **Errores Vulkan en la app**: casi siempre drivers. Checklist NVIDIA: volumen `nvidia-driver-vol` creado y su nombre en `NVIDIA_DRIVER_VOLUME_NAME`, módulo `nvidia-drm` cargado con `modeset=1`.
- **"Address already in use"**: otro proceso ocupa los puertos (¿Sunshine corriendo de fondo?). Lista de puertos requerida.
- **"Unable to recognise GPU vendor"**: multi-GPU o display virtual en VM → configurar `WOLF_RENDER_NODE`.
- **Intel `MFX_ERR_UNSUPPORTED`**: reinstalar linux-firmware + `i915.enable_guc=2`.
- **Permission error con uhid**: cargar módulo `uhid` en boot (`/etc/modules-load.d/uhid.conf`).
- **Re-crear el volumen NVIDIA tras actualizar drivers** (script incluido). ← recurrente para Jordi.
- **Problemas con AV1**: tarjetas como 7900XTX (y CPUs N150, UHD 730) fallan encodeando AV1 (se cae a 30 FPS). Fix: comentar `[[gstreamer.video.av1_encoders]]` en el toml.
- **Stutter regular cada 5 min en laptop Linux cliente**: es NetworkManager/wpa_supplicant corriendo `bgscan` cada 300s (AMD RZ616/MT7921K, Intel AX210). Diagnóstico con `iperf3`, fix con modo BSSID o ajustar `bgscan` via dispatcher.
- **Moonlight cliente**: la app Android oficial está abandonada; recomiendan el fork **ClassicOldSong/moonlight-android** (arregla features de controller) (environment-considerations.adoc). ← Nota: ClassicOldSong es el mismo upstream de Helios/Apollo.
- **Proton**: límite de núcleos; algunos juegos fallan con demasiados cores (limitar por launch options, no con `--cpus` de Docker que no oculta el count real).

## 12. LIMITACIONES / caveats que la PROPIA DOC admite  ⚠️ (clave)

- **Lobbies = WIP sin documentar**: `dev/lobbies.adoc` es literalmente `WIP...` (sólo el diagrama). La implementación de lobbies/co-op está a medio documentar.
- **`start_virtual_compositor` "TODO: document this better"** (configuration.adoc:285) — el flag central del compositor no está bien explicado.
- **NVIDIA Container Toolkit "isn't recommended at the moment, not as stable as the manual method"** (quickstart.adoc, issue #152). Por eso el método manual con driver-volume — que a su vez tiene el downside admitido de **re-crear el volumen en cada update de drivers**. ← exactamente el escenario de Jordi (Blackwell + método manual).
- **Multi-GPU load-balancing NO existe: "planned for future releases"** (configuration.adoc:508). Hoy render + encode deben ir en la misma GPU. El "iGPU encodea mientras GPU juega" del README es objetivo de diseño, no necesariamente automático hoy.
- **El compositor Wayland propio NO soporta XWayland** → apps X11 (Steam) dependen de Gamescope como workaround (how-it-works.adoc).
- **Gamescope tiene issues conocidos** (configuration.adoc:447): inestable con ciertas versiones de driver NVIDIA (issue #60) y **no soporta múltiples ventanas** (rompe la UI desktop de Steam multi-ventana). Sway es el default; usar Gamescope sólo si Sway falla.
- **Overlay de Steam NO funciona** en el contenedor headless (steam.adoc) → sólo MangoHud.
- **Contenedor de app se BORRA al cerrar** → todo lo no-montado se pierde; instalar software extra exige imagen custom (apps/index.adoc).
- **Estado de zero-copy contradictorio en la doc**: `wayland.adoc` tiene un bloque `[OUTDATED]` que dice que ya hay zero-copy completo, pero el cuerpo viejo debajo sigue describiendo la copia por host-memory y los líos de *modifiers* de DMA buffers entre plugins de GStreamer ("the chance of getting a good image out of the pipeline starting with a DMA Buffer is very small"). La doc reconoce que quedó desactualizada.
- **`WOLF_DOCKER_SOCKET` no soporta tcp** ("doesn't support tcp (yet)") — sólo socket unix local.
- **API sólo por unix socket; exponerla por TCP es "highly dangerous"** (parear clientes + ejecutar comandos arbitrarios). No hay auth propia — la seguridad la pones tú con el proxy.
- **WSL2 "hasn't been properly tested" (EXPERIMENTAL)** y sin uinput/uhid queda restringido a **sólo mouse+teclado** (sin gamepads).
- **Proxmox LXC: "only possible to run Wolf inside a privileged LXC"** (rompe el bajo-privilegio que Wolf busca).
- **Prism Launcher known issue**: cree estar en fullscreen y el puntero se ensucia; workaround manual (abrir terminal, `exit`).
- **AV1 roto en varias tarjetas/CPUs** (7900XTX, N150, UHD 730) → hay que deshabilitar los encoders AV1.
- **Runner `process`** corre en el propio contexto de Wolf (no aislado como `docker`) — sólo para pipelines dummy/host.
- **Wolf UI marcado `:experimental:`** (wolf-ui.adoc).
- Varios `TODO`/`WARNING` en los specs de protocolo (flowchart, significado de `modifiers`/`amount 2` en input) — protocolo documentado pero con huecos.

---

## Notas para el caso de Jordi (2 usuarios Steam, Blackwell, perfiles)
- La config real (2 perfiles: `moonlight-profile-id` con Wolf UI+Test ball, y `user` con Steam/Firefox) **es exactamente el layout del default de la doc** — no hay que inventar nada.
- **2 usuarios Steam simultáneos**: cada uno es un perfil con su propio `profile_data/<id>/WolfSteam` aislado; ojo con el ritual de permisos de primer arranque (`mk_steam_dir.sh`, chown 1000:1000) por CADA perfil nuevo.
- **Blackwell + manual driver-volume**: recordar re-crear `nvidia-driver-vol` en cada update de driver; método correcto según la doc (el toolkit es el no-recomendado). Mantener compositor+encoding en la NVIDIA (no hay load-balancing multi-GPU aún).
- Empíricamente ya validado por Jordi: 2 sesiones concurrentes estables con compositor renderizando en NVIDIA — consistente con lo que la doc promete para multi-sesión.
