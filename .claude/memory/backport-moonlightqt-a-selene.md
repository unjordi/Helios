---
name: backport-moonlightqt-a-selene
description: Inventario de backports del abuelo moonlight-qt (app) y del submódulo moonlight-common-c hacia el cliente Selene; comparación de pin common-c Selene-vs-Helios-vs-fork y lista priorizada.
metadata:
  type: project
---

# Backport moonlight-qt → Selene (cliente)

Inventario de release-engineering (2026-07-25): qué arreglos del **abuelo moonlight-qt**
(moonlight-stream) y de su submódulo **moonlight-common-c** NO han llegado a **Selene**
(fork de unjordi de `wjbeckett/artemis`, que forkea moonlight-qt). Artemis está **MUERTO**
(último commit 2025-08-31), así que estos fixes hay que bajarlos del abuelo directo.

Relacionado: [[artemis-issues-forums]] · [[moonlight-doc-sintesis]] · [[forks-helios-selene]] · [[build-selene-macos]] · [[moonlight-issues-forums]]

## Cómo se obtuvo (remotos añadidos, read-only)
En `Selene/` se añadió el remoto abuelo **read-only** y se refrescó upstream:
```
git remote add moonlightqt https://github.com/moonlight-stream/moonlight-qt.git
git fetch moonlightqt --tags   # tip master 17d5b1a8 (2026-07-22) — master SÍ activo
git fetch upstream             # wjbeckett/artemis (muerto)
```
Ninguna otra mutación (no checkout/commit/submódulo). `develop` sigue en su sitio.

## Topología del fork (app-level)
- **merge-base** `Selene/develop` ↔ `moonlightqt/master` = **`1bf86f52`** (2025-07-04,
  "Deregister logging callbacks before destroying the logger"). Confirmado presente en develop.
- `moonlightqt/master` va **+450 commits** por delante de la merge-base (solo **11** son
  dependabot → ~439 son cambios reales; hay una REESCRITURA grande del renderer DRM/KMS/atomic).
- `Selene/develop` va **+316 commits** por delante de la merge-base (rebrand Artemis→Selene +
  fixes propios de Jordi). Selene está ~314 adelante / 2 detrás de Artemis.
- Conclusión: **NO mergear master entero** (450 commits, reescritura DRM que chocaría con los
  316 commits divergentes + el rebrand). **Cherry-pick selectivo.**

## Comparación de pin de moonlight-common-c (⚠️ COMPAT host↔cliente)
Los DOS forks montan la **misma fork de la lib: `ClassicOldSong/moonlight-common-c`** (NO la de
moonlight-stream). El pin **NO coincide**:

| Repo | pin common-c | fecha | mensaje |
|------|-------------|-------|---------|
| **Selene** (cliente) `moonlight-common-c/moonlight-common-c` | **`ad329b2`** | 2025-07-13 | Merge upstream/master |
| **Helios** (host) `third-party/moonlight-common-c` | **`c999436`** | 2025-09-01 | Add send empty payload method |

**NO MATCH.** `ad329b2` es ancestro directo de `c999436`: **Selene está 4 commits DETRÁS de
Helios** en la MISMA fork:
```
ab6e21f  Specify cmake max version to support cmake-4.0 (#112)
0975a86  Workaround bad synthesized IPv6 addresses (v4-only VPN, iOS)
5f22801  Fix CGN subnet mask
c999436  Add send empty payload method   ← toca el CONTROL channel
```
- **Riesgo de wire-break: BAJO.** Ninguno cambia el formato de paquete. `c999436` solo AÑADE
  una API cliente `LiSendEmptyPayload()` (payload `AA 55 AA 55` por `CTRL_CHANNEL_SERVERCTL`,
  reliable — workaround para sleeps de WiFi del cliente). Es aditivo, no rompe el handshake.
- **Pero drift real:** Helios expone/usa una API que la lib de Selene NO tiene, y llevan pins
  distintos. La norma del proyecto es que host y cliente hablen el MISMO protocolo → **alinear
  el pin de Selene HACIA ARRIBA a `c999436` para igualar a Helios.** Dificultad: **trivial**
  (bump de submódulo + rebuild). **Esta es la acción de compat #1.**
- **LTR / reference-frame-invalidation (#120):** NO está en el pin de Selene (`ad329b2`) NI en el
  de Helios (`c999436`) — `Limelight.h` de ambos no tiene refs LTR/LongTerm. Existe en el master
  de la lineage moonlight-stream, pero ClassicOldSong aún no lo mergeó a estos pins. El commit
  app `05ef938e` "Add support for LTR ACK control messages" DEPENDE de ese soporte en common-c.
  → **Cambio COORDINADO host+cliente diferido**: no urgente, aterriza cuando la fork de
  ClassicOldSong gane RFI/LTR y Helios+Selene suban el pin JUNTOS. Backlog.

## Backport priorizado — TOP 8 primero

### P0 · Alineación de protocolo / common-c (COMPAT, hacer PRIMERO)
1. **Bump submódulo common-c `ad329b2` → `c999436`** para igualar a Helios. Los 4 commits de
   arriba; solo `c999436` toca el control channel (aditivo). Riesgo wire: bajo. Dificultad:
   trivial. **Bloqueante conceptual de todo lo demás** (host↔cliente deben ir alineados).

### P1 · Decoder / HDR en Linux (dolor real de Jordi: CachyOS/KDE Wayland, NVIDIA Blackwell + iGPU AMD)
2. **`af03f57e`** Fix Vulkan decoder probing on Nvidia using KMSDRM · + **`94d47e95`** non-determinism
   during Vulkan decoder probing on KMSDRM → fiabilidad del probe de decoder NVIDIA (RTX 5070 Ti
   Blackwell). Riesgo de NO backportear: probes intermitentes/decoder que no arranca. Dif: media.
3. **`d17575d4`** Fix deadlock in Vulkan renderer using KMSDRM on **AMDGPU** · + **`51f86caa`** assert
   on exit KMSDRM AMDGPU → su iGPU es Radeon. Riesgo: deadlock/cuelgue del cliente. Dif: media.
4. **`53a7680a`** Fix incorrect autoselection of SW AV1 over HW H.264 for SDR content → sin esto,
   puede elegir decode por SOFTWARE (lento) en vez del HW. Riesgo: perf. Dif: baja.
5. **HDR-Linux cluster:** **`444c6ccf`** Remove experimental tag from HDR/AV1/YUV444 · **`561d8248`**
   Move HDR toggle into Basic Settings (+ camino libplacebo). HDR Vulkan+Wayland es el target de
   Jordi. Riesgo: HDR queda escondido/experimental. Dif: baja-media.
6. **`02a86167`** Disable separate decoder devices for non-Intel FL11.0 GPUs · + **`a0a4c1ea`** decoder
   texture binding by default con separate devices → corrección de "separate decoder device" en
   setups multi-GPU (NVIDIA + iGPU AMD, justo el de Jordi). Riesgo: fallo de decode/render con 2 GPUs. Dif: media.

### P2 · Crashes / robustez / seguridad
7. **`8795fb54`** Fix double-free in Vulkan renderer when an overlay is disabled · **`191cd32b`** Fix
   crash when skipping oversized displays. + leaks: `2549efc8` pthread_attr, `70d4f244` pending
   frame, `476414ea` overlay surface, `9af62220` common-c leak. Riesgo: crashes/leaks en runtime. Dif: baja.
8. **Bumps de deps con seguridad** (OpenSSL/FFmpeg/SDL3/dav1d/libplacebo): `c685021f`, `f2a512d3`,
   `2914ff67`, `e785be03` (OpenSSL 4.0). Higiene. Dif: baja pero ruidosa (tocan libs empaquetadas).

### Backlog (no-urgente)
- **LTR ACK `05ef938e`** → diferido, requiere common-c RFI/LTR (ni Selene ni Helios lo tienen). Coordinado.
- **Reescritura renderer DRM/KMS/atomic** (`d50ba063`, `745ac34b`, `7643cc92` DRM overlay
  composition, page-flipping, `88719cc8` FB_DAMAGE_CLIPS, etc.): relevante SOLO si Selene corre en
  modo **EGLFS/KMSDRM consola**; en el desktop KDE Wayland de Jordi (SDL Wayland/Vulkan) es
  secundario. Es un cluster grande y acoplado → backport en bloque o nada. Backlog.
- **macOS / Metal / libplacebo cluster** (`9cbba106` Metal default, `8c6b4220` libplacebo on
  MoltenVK, `ca7d61f5`/`e223bf9a`/`06bd8a73`/`a9ad0482` libplacebo mac, `7c93aaf6` Tab focus mac):
  solo si el cliente Mac sigue vivo como target. Ver [[build-selene-macos]]. Dif: media (toca deps aqt).

## Ya CUBIERTO en Selene (no backportear)
- **PR #64 SSL use-after-free** → Selene ya lo tiene: **`67c31d16`** "fix: use deep copy for SSL key
  to prevent use-after-free" (PR #1). Equivale al `1eb76bbd` del abuelo. ✔ cubierto (impl. propia).
- **Artemis #62 SDL2_ttf faltante** → Selene **`27c2e446`** bundle SDL2_ttf en portable Linux. ✔
- **Artemis #23 AV1 Flatpak roto** → Selene tiene fixes flatpak (`3b61e37d`, `64103304`, …). ✔
- **qt #1300 control-stream regression v6.0** → la merge-base (2025-07-04) es POSTERIOR a v6.0.0/v6.0.1,
  así que el fix ya viene heredado por debajo de la merge-base. ✔ (verificar si reaparece).

## Recomendación de estrategia
1. **Ahora:** bump common-c de Selene `ad329b2`→`c999436` (igualar a Helios), rebuild, y probar un
   stream Selene↔Helios. Única acción de compat urgente.
2. **Luego:** cherry-pick los ~7 commits P1/P2 de Vulkan/decoder/HDR/crash (viven casi todos en
   `app/streaming/video/**` y `app/streaming/**`, bajo acoplamiento con protocolo).
3. **Diferir** LTR/RFI hasta un bump coordinado de common-c en AMBOS forks.
4. **macOS** solo si el cliente Mac sigue siendo target.
- **Fricción a vigilar:** commits app del abuelo que llamen APIs `Li*` NUEVAS dependen de que la
  fork ClassicOldSong (no la de moonlight-stream) las exponga; algunos no caerán limpios sin subir
  antes el common-c. Por eso el orden: común-c primero, app después.

## Ejecución del backport (2026-07-25) — resultado real
Worktree aislado `/home/unjordi/code/HeliosSelene/Selene-wt-backport` (rama base develop).

**P0 · common-c LANDÓ** — rama `fix/align-common-c-with-helios`, **PR unjordi/Selene#15** (open, base develop).
Bump submódulo `ad329b2`→`c999436` (ancestro directo, 5 commits, lineal). ✅ **compila headless**
(qmake6/Qt6.11 + prefixes privados, binario `selene` 46MB). Iguala el pin de la lib de protocolo a Helios.
Gotcha `gh`: en fork, `gh pr create` apunta el base al PADRE upstream → usar `-R unjordi/Selene`.

**P1 · TODOS PARKED-CONFLICT** — los 5 grupos chocan con los deltas divergentes de Selene (los +316
commits de rework propio de Jordi en Vulkan/AV1/HDR/renderers + rebrand). Ninguno cae limpio; NO se
forzó ninguno (regla dura). Detalle:
- `af03f57e` (streamutils NVIDIA KMSDRM) → `createTestWindow()` NO existe en Selene (divergencia estructural del path de probe).
- `94d47e95` (plvk non-determinism) → choca con los bloques compat Vulkan/libplacebo propios de Selene en plvk.cpp.
- `53a7680a` (SW-AV1 vs HW-H264) → el bloque de deprioritización AV1 en session.cpp lo reescribió Selene (usa `getActualFpsForDecoderTest()`).
- `444c6ccf` + `561d8248` (un-hide/mover HDR) → **✅ LANDÓ como port MANUAL** (2026-07-25): rama `feat/unhide-hdr-av1-yuv444`, **PR unjordi/Selene#16** (open, base develop), worktree `Selene-wt-unhide`. Selene aún mostraba las 3 opciones como "(Experimental)" (su rework AV1/HDR era backend/detección, no las etiquetas UI). Portada la intención sobre el `SettingsView.qml` divergente: `AV1 (Experimental)`→`AV1`, `Enable HDR (Experimental)`→`Enable HDR` **movido a Basic Settings** (V-Sync+Frame pacing en un `Row`, igual que 561), `Enable YUV 4:4:4 (Experimental)`→`Enable YUV 4:4:4`. `Unlock bitrate limit (Experimental)` NO se tocó (no es de estos commits). **Traducciones:** lupdate6 resincronizaba **99 strings** acumuladas del rebrand (~22k líneas de churn no relacionado) → se dejó fuera; los 3 strings renombrados caen al **fallback source-text (inglés)** de Qt hasta un pase i18n separado (`chore(i18n): lupdate resync` recomendado aparte). Compila+linkea headless (binario `selene` 46MB). Gotchas del worktree: submódulos NO se auto-populan (`git submodule update --init --recursive --force` + nested `enet` de common-c a mano; `libs`/prebuilts quedó vacío pero no bloquea el link en Linux).
- `d17575d4` + `51f86caa` (AMDGPU KMSDRM) → masterhook_internal.c de Selene usa `SDL_SpinLock` (no el `pthread_mutex g_MasterLock` que parcha el fix); drm.cpp/drm.h también divergen.
- `02a86167` + `a0a4c1ea` (separate decoder devices) → d3d11va.cpp diverge en Selene; **además Windows-only** (no se compila en Linux headless → no verificable en esta caja de todos modos).

**Conclusión operativa:** el área streaming/video de Selene diverge tanto del abuelo que el cherry-pick
mecánico NO sirve para estos fixes; requieren **re-implementación manual sobre el código de Selene**
(portar la INTENCIÓN, no el diff). El un-hide de HDR (444/561) es el candidato más barato de re-hacer.
