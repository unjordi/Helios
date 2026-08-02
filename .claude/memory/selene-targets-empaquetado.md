---
name: selene-targets-empaquetado
description: Decisión de empaquetado de Selene por dispositivo de prueba — Steam Deck (AppImage x86_64) y Retroid Pocket 6 / ArmadaOS (AppImage aarch64, compilado on-device); por qué NO Flatpak ni rpm.
metadata:
  type: project
---

# Empaquetado de Selene para dispositivos de prueba de Jordi (decidido 2026-07-26)

Jordi quiere probar el cliente **Selene** en dos handhelds además de sus builds normales:
**Steam Deck** (x86_64, SteamOS) y **Retroid Pocket 6 (RP6)** con **ArmadaOS** (clon de SteamOS
para ARM, basado en Fedora, **atómico/inmutable** — confirmado por Jordi 2026-07-26).

**Perfiles completos de ambos aparatos** (acceso SSH, HW, gotchas) viven en el cerebro de Juegos:
`$JUEGOS/.claude/memory/deck-perfil.md` y `rp6-perfil.md` (`ssh deck` → 192.168.1.96 DHCP user `deck`;
`ssh rp6` → armada@192.168.1.135). **Corrección clave 2026-07-26:** el RP6 es **Snapdragon 8 Gen 2
(SM8550, `kalama`), GPU Adreno 740 + Turnip** — **NO es RK3588**, así que el issue **#61 (rk3588 no
compila) NO aplica**. La mina real del RP6 es el **decode de video por HW en Adreno/Turnip** (a medias
en Linux) → realista caer a H.264 software; HDR seguramente no (igual que el portable en #23).

## Decisión: AppImage en ambos (NO Flatpak, NO rpm)
- **Steam Deck (x86_64)** → **AppImage x86_64**. NO el binario de Arch/pacman: SteamOS tiene `/usr`
  inmutable, `/` minúsculo (~1.3 G libres), y **sudo NOPASSWD roto** (los updates atómicos borran el
  sudoers) → no se puede meter toolchain ni construir EN el Deck. Además **medido 2026-07-26: Cachy
  glibc 2.43 > Deck glibc 2.41** → un AppImage compilado nativo en Cachy REVIENTA en el Deck
  (`GLIBC_2.42 not found`; el AppImage bundlea Qt/libs pero NO glibc). **Por eso se construye en un
  contenedor Ubuntu 22.04 (glibc 2.35 < 2.41 → portable)** con la receta de deps del CI + Qt 6.8.3 vía
  aqt + `scripts/build-appimage.sh` (infra de moonlight-qt). Se `scp` a `/home` del Deck (ahí SÍ hay
  espacio, 253 G), `chmod +x`, correr en Modo Escritorio; opcional "Add to Steam" para Modo Gaming.
- **RP6 / ArmadaOS (aarch64, atómico)** → **AppImage aarch64**, compilado **en el propio aparato**
  dentro de un contenedor mutable (`distrobox`/`toolbox`), luego envuelto en AppImage para que corra
  en el rootfs inmutable sin contenedor. (El RP6 NO saca video por USB-C bajo Armada — es su propia
  pantalla la que hace de cliente.)

## ✅ CI VERDE — PR unjordi/Selene#17 (2026-07-26): AppImage x86_64 + aarch64
Rama `feat/ci-appimage-linux`. Ambos jobs pasan y suben artifact (verificado: x86_64 72M ELF x86-64,
aarch64 76M ELF ARM aarch64). **Merge pendiente de Jordi** (clasificador no deja que Claude mergee sus PRs).
- **x86_64 (`build-appimage-dev`, revivido):** costó 3 iteraciones. El job armaba el AppDir A MANO
  (mkdir/cp binario+iconos+.desktop) → **linuxdeployqt build 107 muere tras "Found icons" → "AppDir not
  prepared correctly"** (+ refs stale a `usr/bin/artemis` pre-rebrand). **FIX: usar `scripts/build-appimage.sh`**
  (upstream) que hace `make install` a un layout FHS que linuxdeployqt sí acepta + `linuxdeployqt -qmldir
  -appimage` en un shot. Deshabilita Wayland a propósito (EGL del host, no romper el Deck). = LECCIÓN:
  no armar el AppDir a mano; usar el script del repo.
- **aarch64 (`build-appimage-arm64-dev`, nuevo):** pasó A LA PRIMERA (4 min). Runner `ubuntu-22.04-arm`,
  Qt de apt, `linuxdeploy` + `linuxdeploy-plugin-qt` aarch64 (no hay linuxdeployqt ARM64). Sin from-source.
- Ambos encadenados a `create-dev-release` (needs) → se adjuntan a releases de develop.
- **PENDIENTE:** merge (Jordi) · bajar a los aparatos (Deck: reemplazar el AppImage local por el de CI que
  trae AV1/HDR con FFmpeg de fuente; RP6: 1er AppImage aarch64, smoke-test on-device) · QA visual de Jordi.

## CI (dev-build.yml): antes NO producía AppImage útil de Linux (hueco cerrado por PR#17)
El CI solo sube artifacts de **Windows x64 y Windows ARM64**; el job de Linux es **solo "compile
sanity"** en `ubuntu-22.04` (glibc 2.35, Qt 6.8.3 vía aqt) — compila pero NO empaqueta ni sube nada.
**No hay AppImage de Linux que bajar.** → follow-up durable: **agregar un job de AppImage Linux al CI**
(x86_64 ya; aarch64 vía runner arm64 después) para no depender de builds locales a mano. La receta de
deps del CI (apt) es justo la que se reusa en el contenedor de arriba.

**Por qué NO Flatpak:** issue **#23** (heredado de Artemis, SIN fix) — el build Flatpak rompe el
**decode AV1** justo en distros-consola/inmutables (Steam Deck / SteamOS / Bazzite / Nobara / CachyOS)
con `bwrap: Can't mkdir /app/lib/GL: Read-only file system`. El portable/AppImage sí funciona ahí
(aunque **sin HDR**). ArmadaOS cae en la misma categoría → Flatpak es la PEOR opción para este caso.

**Por qué NO rpm:** ArmadaOS es atómico → `rpm-ostree` con layering (reboot, frágil) y aun así habría
que bundlear Qt6 aarch64. AppImage lo esquiva. (Un rpm nativo solo tendría sentido en un Fedora mutable.)

## Minas específicas de ARM (a vigilar en la RP6)
- **~~#61 "no compila en rk3588"~~ NO APLICA:** el RP6 es SM8550/Adreno, no RK3588 (confirmado 2026-07-26).
- **Decode HW en Adreno 740/Turnip:** Qualcomm NO usa VAAPI (usa Venus/V4L2) y el decode acelerado
  bajo Linux/Turnip está a medias → realista que la 1ª prueba caiga a **H.264 por software** (ok a
  resolución modesta; HDR seguramente no, igual que el portable en #23).
- **AppImage aarch64:** NO hay `linuxdeployqt` oficial ARM64 → usar `linuxdeploy` +
  `linuxdeploy-plugin-qt` (sí tienen build aarch64), o empaquetar a mano.

## ✅ RECETA QUE FUNCIONÓ — AppImage x86_64 para el Deck (2026-07-26)
Construido en contenedor Ubuntu 22.04 sobre Cachy. Scripts en el scratchpad de la sesión
(`.../scratchpad/appimage/{Dockerfile,build.sh,build-pkg.sh,driver.sh}`). Salida:
**`Selene-0.6.7-x86_64.AppImage` (106 MB)**. Copiado a `deck:/home/deck/`, **smoke-test OK**
(arranca offscreen, Qt inicializa, cero `GLIBC not found`/`.so` faltante; el Deck tiene FUSE2+3).
Gotchas que costaron iteraciones (para el CI job):
1. **Qt 6.8+ arch = `linux_gcc_64`**, NO `gcc_64` (Qt cambió a nombres con prefijo de OS; el dir en
   disco sí sigue siendo `gcc_64`). aqt con `gcc_64` → "packages ['qt_base','qtmultimedia'] not found".
2. **Deps del CI (apt) NO bastan para linuxdeployqt** — hay que sumar las libs runtime del plugin XCB
   de Qt: `libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0
   libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 libxcb-util1 libxcb-glx0 libxcb-shm0`
   (si no, `libxkbcommon-x11.so.0 => not found` en `libqxcb.so`).
3. **Borrar los drivers SQL fantasma de Qt** antes de linuxdeployqt: Selene NO usa QtSql, pero Qt 6.8
   trae `libqsqlmimer/mysql/psql/odbc.so` cuyas libs runtime (libmimerapi/libmysqlclient) NO están →
   linuxdeployqt aborta. Fix: `find .../plugins/sqldrivers -name 'libqsql*.so' ! -name 'libqsqlite.so' -delete`.
4. **rsync de la copia desechable: excludes ANCLADOS con `/`** (`/build`, NO `build-*`) — sin ancla,
   `build-*` también borra `scripts/build-appimage.sh` (rsync matchea basename a cualquier nivel).
5. linuxdeployqt AppImage sin FUSE en Docker → `APPIMAGE_EXTRACT_AND_RUN=1`.
6. El `.desktop` sigue siendo `com.artemis_desktop.Artemis.desktop` pero el binario ya es `selene`
   (rebrand en develop) → el AppImage sale `Selene-<ver>-x86_64.AppImage`. Ícono aún `artemis.svg` (cosmético).
→ **Esta receta ES lo que debe ir al CI job de AppImage Linux** (x86_64 ahora; aarch64 después).

## Flujo RP6 (atómico → compilar en contenedor)
1. **Fase A (¿corre en aarch64?):** `distrobox create` Fedora mutable en la RP6 (comparte /home+/dev),
   `dnf install` deps (Qt6-devel, SDL2/SDL2_ttf-devel, ffmpeg-devel, openssl, opus, vulkan-headers…),
   clonar Selene `develop` + submódulo `moonlight-common-c` en pin **`c999436`**, `qmake6 && make`,
   correr desde el distrobox para validar decode+stream.
2. **Fase B (correr en host limpio):** envolver en AppImage aarch64 (ojo linuxdeploy arriba) → corre
   en el rootfs inmutable sin contenedor, meter al launcher.

Relacionado: [[build-selene-linux-headless]] (receta Linux + prefixes privados), [[build-selene-macos]],
[[artemis-issues-forums]] (#23, #61), skill `selene`.

## Nota QA (2026-07-26): la AppImage es XWayland-only + no se lanza por SSH
La AppImage bundlea SOLO el plugin de plataforma Qt **`xcb`** (NO qwayland) — es intencional de upstream
(build-appimage.sh deshabilita Wayland; corre vía XWayland como la Moonlight oficial en el Deck). Deps del
plugin xcb TODAS bundleadas (incl. `libxcb-cursor.so.0`, verificado). Lanzarla **por SSH NO abre ventana**
(offscreen sí; wayland sale en silencio; xcb por SSH no agarra display X) → el smoke-test "arranca en el Deck"
era SOLO offscreen. El launch windowed real = doble-clic en la sesión de escritorio del usuario (o Konsole).

## 💡 DECIDIDO (2026-07-26): Selene en Wayland NATIVO en los handhelds
> Ítem vivo en [[estado-proyecto]] #20. Aquí solo la decisión y el porqué.
Jordi NO quiere a futuro el trade-off XWayland de la AppImage ("suena a tradeoff que no quiero"). Meta:
Selene corriendo Wayland nativo en Deck/RP6, no vía XWayland. El obstáculo es que la AppImage upstream
DESACTIVA Wayland (choque `libwayland-client` bundleada vieja vs EGL del host). Opciones a evaluar:
1. **AppImage Wayland-enabled que EXCLUYE `libwayland-client`/`libwayland-egl`** (usar las del host, no
   bundlearlas) — es como los AppImage modernos meten Wayland sin el choque de EGL. Camino más probable.
   linuxdeploy tiene flags de exclude-library; probar en una rama de CI.
2. **Flatpak con Wayland** — bloqueado hoy por #23 (AV1 roto en distros-consola); revisitar SI #23 se arregla.
3. **Instalación nativa** que linkee el Wayland del sistema — difícil: Deck/RP6 son inmutables (por eso
   fuimos a AppImage). Vía distrobox con passthrough Wayland sería la variante.
Prioridad: baja/media (para stream fullscreen XWayland es casi invisible), pero es deuda a limpiar.
