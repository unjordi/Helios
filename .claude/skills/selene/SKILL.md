---
name: selene
description: Knowhow del CLIENTE Selene (fork de unjordi de wjbeckett/artemis, que forkea moonlight-qt) — qué añade Artemis sobre Moonlight-Qt (clipboard, OTP pairing, quick menu, fractional refresh, virtual display control), builds (macOS vía aqt, Linux headless con prefixes privados), el fix del segfault de `list`, y pain points (upstream Artemis MUERTO → bajar fixes de moonlight-qt). Carga al tocar Selene/ o el cliente de streaming.
---

# Selene — el cliente del ecosistema Helios + Wolf + Selene

Skill de trabajo del **cliente**. Destila la investigación de Artemis (el codebase que Selene ES) +
lo propio/empírico de Selene. Cárgalo al tocar `Selene/`, el binario `selene`, o cualquier trabajo
del cliente de streaming.

## 1. Qué es y linaje

**moonlight-qt → Artemis → Selene.** Cliente Qt6 de game streaming, cross-platform (Win x64/ARM64,
macOS universal, Linux, Steam Deck). Cadena de forks:
- **`moonlight-stream/moonlight-qt`** (el ABUELO, **activo**) = el core: decode HW, H.264/HEVC/AV1,
  HDR, 7.1, multitouch, gamepad ×16, renderers (DRM/PlVk/D3D11VA/VTMetal/EGL).
- **`wjbeckett/artemis`** (el PADRE, **MUERTO** — último commit 2025-08-31) = rebrand de moonlight-qt +
  el "delta Artemis" para hablar con hosts **Apollo** (porta a desktop las features que ya existían en
  el Artemis de Android de ClassicOldSong).
- **`unjordi/Selene`** = nuestro fork, rebrandeado y **revivido** (Jordi es ahora quien lo mantiene).

Habla el **protocolo Moonlight/GameStream** + las extensiones de Apollo. Se conecta a **Helios** (host,
máquina real) y a **Wolf** (backend headless multi-sesión) — ambos hablan Moonlight. Rama base `develop`.
GPL-3.0. Submódulo compartido **`moonlight-common-c`** (el mismo que Helios) → host y cliente evolucionan
ALINEADOS o se rompe la compatibilidad de protocolo.

## 2. Delta de Artemis sobre Moonlight-Qt (qué AÑADE)

El core de decode/render es de moonlight-qt; Artemis suma, sobre todo para hablar con **Apollo/Helios**:
- **Clipboard Sync** bidireccional — HTTP `actions/clipboard?type=text`; auto-sync al iniciar/reanudar
  stream y al perder foco; anti-loop por hash SHA-256; límite 1 MB. **Apollo-only.**
- **Server Commands** — ejecutar comandos custom en el host; requiere permiso `server_cmd`. **Apollo-only.**
- **OTP Pairing** — pairing por one-time-password: extiende el PIN estándar con `&otpauth=`, hash
  **`SHA256(pin + salt + passphrase)`**, PIN de 4 dígitos. **Apollo-only.**
- **Quick Menu** — overlay in-stream (teclado `Ctrl+Alt+Shift+\`; gamepad `Select+L1+R1+Y`).
- **Fractional Refresh Rate** — refresh custom client-side (90/120 Hz…). ⚠️ ver #65 (la UI no acepta decimales).
- **Resolution Scaling** — escalado de resolución client-side (rendimiento).
- **Virtual Display Control** — elegir si usar el display virtual del host.
- **UUID-Based App Launching** — con **fallback automático** a IDs legacy si no hay UUID.
- **Permission Viewing** — ver flags server-side (`clipboard_set/read`, `file_upload/download`, `server_cmd`).
- **Rebranding + protocolo `art://`** (Apollo Android). **Detección propia AV1/HDR** (no en moonlight-qt):
  fallback `AV1_MAIN10 → AV1_MAIN8`, override `FORCE_AV1_SUPPORT=1`, y el fix de renderers que reclamaban
  HDR sin implementar `setHdrMode()` (PlVk/D3D11VA). Ver `Selene/AV1_DETECTION_ANALYSIS.md`.

**Componentes nuevos** (por si tocas código): `app/backend/clipboardmanager.*`, `servercommandmanager.*`,
`otppairingmanager.*`, `app/settings/artemissettings.*` (QSettings); managers como QObject expuestos a QML.

⚠️ **Casi todo el delta marcado "Apollo-only" queda INERTE contra Sunshine puro / GameStream** — el host
no expone esos endpoints. Aplica contra Apollo/**Helios**. **Wolf** habla Moonlight base (no documentado
por Artemis) → el streaming base funciona; las extensiones Apollo hay que validarlas caso por caso.

## 3. Builds

El binario CLI headless (`pair`/`list`/`stream`) es **construcción de Selene, NO una feature de Artemis**
(Artemis se distribuye solo como app GUI Qt; no documenta un CLI tipo moonlight-embedded).

### macOS (Apple Silicon) — Qt vía **aqt**, NUNCA brew
El `qt` de Homebrew está roto (keg-only meta-paquete: `qmake6` symlink roto, `Could not find qmake spec
'macx-clang'`). Usar Qt limpio con **aqt** (misma versión que el CI, 6.8.3):
```bash
python3 -m pip install --user aqtinstall
python3 -m aqt install-qt mac desktop 6.8.3 clang_64 -m qtmultimedia --outputdir ~/Qt
```
**AGL**: Apple lo quitó del SDK, pero Qt 6.8.3 lo referencia en los `.prl` INTERNOS de cada framework
(`lib/Qt*.framework/Versions/A/Resources/*.prl`) → `ld: framework 'AGL' not found`. Un
`QMAKE_LIBS_OPENGL -= -framework AGL` NO basta. Fix: `bash scripts/macos-strip-agl.sh ~/Qt/6.8.3/macos`
(el CI lo corre tras "Setup Qt"). Compilar:
```bash
export PATH=~/Qt/6.8.3/macos/bin:$PATH
qmake ~/code/HeliosSelene/Selene/artemis.pro -spec macx-clang CONFIG+=release && make -j$(sysctl -n hw.ncpu)
```
Para un `.app` que diga "Selene" compila `develop` (rebrand mergeado ahí). Builds de dev salen "damaged"
por quarantine → `xattr -cr Selene.app`.

### Linux headless (CachyOS) — **prefixes PRIVADOS**, el sistema no trae las deps
La caja usa **sdl2-compat (SDL2 sobre SDL3)** y **NO tiene `SDL2_ttf`-dev ni `vulkan-headers`** del
sistema (hay SDL3_ttf, no SDL2_ttf). El build e2e usa prefixes privados en `~/.cache/selene-e2e/`:
```bash
BUILD=<dir-FUERA-del-repo>   # p.ej. el scratchpad
export PKG_CONFIG_PATH=/home/unjordi/.cache/selene-e2e/sdlttf-prefix/lib/pkgconfig:$PKG_CONFIG_PATH
export CPATH=/home/unjordi/.cache/selene-e2e/vkh/include:$CPATH
( cd "$BUILD" && qmake6 /home/unjordi/code/HeliosSelene/Selene/artemis.pro \
    CONFIG+=release CONFIG+=disable-wayland CONFIG+=disable-libdrm CONFIG+=disable-cuda \
  && make -j$(nproc) )   # binario: $BUILD/app/artemis (rename a 'selene' vive en develop, cosmético)
```
Las 2 deps y por qué: **SDL2_ttf** (`sdlttf-prefix/`, el `.pro` pide `PKGCONFIG += SDL2_ttf`) — gotcha:
el `.pc` puede traer `prefix=` STALE y puede FALTAR el header (`SDL2/SDL_ttf.h`, bajarlo de la release
que case con la `.so`). **Vulkan headers** (`vkh/include/`, libplacebo hace `#include <vulkan/vulkan.h>`)
→ van por `CPATH`. **Toolchain:** qmake6 = Qt 6.11.1, gcc 16.1.1. Selene SÍ compila con gcc-16 (a
diferencia de Helios, que exige gcc-14); los warnings `[[nodiscard]]` de `QFile::open` son benignos (no hay `-Werror`).

## 4. ⚠️ Pain points / a medio cocer (upstream MUERTO)

**`wjbeckett/artemis` está de facto MUERTO** — último commit **2025-08-31** (~11 meses). El maintainer
declara intención de seguir pero no commitea, no cierra issues, no mergea PRs (un PR de seguridad lleva
meses sin tocar). **REGLA: Selene NO espera fixes de Artemis; baja fixes del ABUELO `moonlight-qt`
(activo) por el submódulo/rebase — `git fetch upstream` desde moonlight-qt, no solo desde artemis.**

Backlog de bugs abiertos en Artemis (ninguno con fix mergeado upstream):
- **PR #64 — SSL key use-after-free** (memory-safety; el autor dice que replica un fix ya en moonlight-qt).
  **YA backporteado en Selene = nuestro PR #1.** ✅
- **#23 — AV1 decode roto en build Flatpak** (Steam Deck/SteamOS/CachyOS/Nobara/Bazzite): `bwrap: Can't
  mkdir /app/lib/GL: Read-only file system` + `ldconfig failed 256` = restricción del sandbox Flatpak.
  El portable funcionaba (sin HDR); Flatpak nunca. El hilo más vivo (20 comentarios), sin fix.
- **#62 — falta `SDL2_ttf`** (no arranca en Linux Mint; dep no bundleada). Eco DIRECTO de nuestra receta
  headless, que también tuvo que traer SDL2_ttf por prefix privado.
- **#55 / #60 — crashes en Windows**: #55 crash al activar YUV 4:4:4 (solo funciona con Sunshine); #60
  crashes aleatorios en Win11 (RTX 3060 Mobile).
- **#65 — fractional refresh que la UI no acepta** (decimales tipo 59.94) — la feature estrella de Fase 2 a medio cocer.
- **#51 — clipboard file-sync roto** (la sync de archivos no jala pese a estar activada). Fase 1 incompleta.
- Otros: #66 (portable no guarda settings), #61 (no compila en rk3588), #67 (per-host profiles, pedido).
- **Nunca aterrizó** (Fase 4 "planned"): orden custom de apps e **input-only mode** (stream de input sin video).

## 5. 🔧 Trucos / gotchas

**Runtime headless** (correr el binario sin display):
```bash
export LD_LIBRARY_PATH=/home/unjordi/.cache/selene-e2e/sdlttf-prefix/lib   # resuelve libSDL2_ttf
export QT_QPA_PLATFORM=offscreen           # o Xvfb (el demo levanta Xvfb :121/:122)
export VK_ICD_FILENAMES=/nonexistent/none.json LIBGL_ALWAYS_SOFTWARE=1     # neutraliza Vulkan NVIDIA
selene list <host:puerto>      # o pair / stream
```
- **Codec CLI exige el string EXACTO**: `H.264` / `HEVC` / `AV1` / `auto` — `h264` da "Invalid video-codec choice".
- **Con `--video-codec auto`, Wolf negocia AV1 → NVDEC** (decode en GPU); en un arnés headless-software
  el frame NO se presenta (render software sin GPU) → la sesión muere ~16s con `-101` "lack of a successful
  video frame". **Es artefacto del arnés, NO de Wolf.** Para arnés headless sano: fuerza `--video-codec
  H.264` (software) o deja render por GPU con display real. Un cliente REAL con display (el Mac) no sufre esto.
- **Pairing por cert-CN en LAN (gotcha de interop):** en la misma LAN, Selene descubre el host por IP
  (`192.168.1.250`), pero el cert de Helios está firmado para `unjordi.pisa.mx` → CN no cuadra con la IP →
  el pairing falla en "stage #4" (host marca success, cliente no completa). Selene hace **cert-PINNING**
  (guarda el cert al primer pairing), así que una vez emparejado ya no re-valida CN; el tropiezo es SOLO
  el PRIMER pairing LAN contra un cert de CN público.
- **Colisión mDNS con 2 hosts en la misma caja:** si Helios (`:47989`) y Wolf (`:48989`) corren juntos,
  el descubrimiento mDNS se confunde → conéctate por **IP:puerto explícito** (`192.168.1.250:48989`), no por mDNS.
- **Autoajuste de resolución (lado cliente):** el host iguala su modo al que pide el cliente vía
  `SUNSHINE_CLIENT_WIDTH/HEIGHT`, aplicado con `wlr-randr`/`kscreen-doctor`. Frágil en KDE Wayland
  (= frustración #2 del roadmap, pero es del lado HOST/Helios). Ver [[deploy-streaming-y-resolucion]].

## 6. Lo que YA validamos

- ✅ **Segfault de `selene list` ARREGLADO (PR unjordi/Selene#14 → develop):** el CLI fugaba el
  `ComputerManager` → race de `~QSettings` en el `DelayedFlushThread` al salir. **Fix = parentar el
  ComputerManager al launcher.** Desbloqueó el arnés headless local.
- ✅ **Selene ↔ Wolf funciona:** entrega video a 2 IPs distintas simultáneas (Jordi+Liora son 2 máquinas =
  2 IPs). El Mac (cliente real, M4 Pro) **decodifica HEVC por VideoToolbox** sostenido; el local decodifica
  AV1 vía NVDEC. Soak 60s con `Wolf UI` ×2 e H.264 software sostuvo ambas sesiones sin crash.
- ✅ **Selene → Helios normal funciona** (streaming base). El pairing headless está probado
  (`QT_QPA_PLATFORM=offscreen`), sujeto al gotcha cert-CN de arriba.
- ✅ **Rebrand a "Selene" completo** en develop (README, íconos; el TARGET del binario se renombra ahí —
  en ramas viejas tipo `chore/ci-triage` el binario aún se llama `artemis`, cosmético).
- ⚠️ **Bug i18n cosmético** en el CLI de pairing: la string "Introduce el PIN" no sustituye `%1`
  (`QString::arg: Argument missing`). El pairing funciona; fix en `app/cli/pair.cpp` / i18n `es`.

## 7. Referencias

- [[artemis-doc-sintesis]] — síntesis de la doc de Artemis (delta, arquitectura, estado).
- [[artemis-issues-forums]] — barrido GitHub/foros (bugs abiertos, estado de vida, PRs a portar).
- [[build-selene-macos]] — receta build nativo Mac (aqt + strip AGL).
- [[build-selene-linux-headless]] — receta build headless Linux (prefixes privados).
- [[forks-helios-selene]] — plan de los forks, workflow git, decisión de producto (Helios+Wolf+Selene).
- [[deploy-streaming-y-resolucion]] — deploy en vivo, autoajuste de resolución, gotcha cert-CN.
- Skill `moonlight` — el protocolo (cuando exista); skills hermanas `helios` (host) y `wolf` (backend headless).
