---
name: backport-sunshine-a-helios
description: Inventario de backports Sunshine→Helios — lo congelado NO es Apollo (vivo hasta 2026-05-21) sino su SYNC con Sunshine (última 2025-09-27); Sunshine es la fuente real de fixes. Topología del fork, merge-base, candidatos priorizados y la DECISIÓN de cierre de brecha (merge completo, 2026-07-26).
metadata:
  type: project
---

# Backport Sunshine → Helios · inventario priorizado (2026-07-25)

Linaje: **Sunshine (LizardByte) → Apollo (ClassicOldSong) → Helios (unjordi)**.

> ⚠️ **CORRECCIÓN 2026-07-26 (medida, no inferida): "Apollo congelado" era FALSO/impreciso.**
> Apollo **NO está muerto**: su HEAD es `adc5c5a0` del **2026-05-21** (hace ~2 meses), e incluye un
> **backport de seguridad** (GHSA-ph75-mgxh-mv57), fixes de VAAPI/AMF (`c71ea0a8`, `2aa5a396`) y de
> timestamps de Wayland (`52000d05`). Y **Helios/develop ya contiene el 100% de Apollo**
> (`git rev-list --count develop..upstream/master` = **0**).
> **Lo que Apollo dejó de hacer es SINCRONIZAR con Sunshine:** su último merge del abuelo fue
> `10fd290b` (**2025-09-27**). ESA es la brecha — no un fork abandonado.

Por eso, **la fuente REAL de fixes de plataforma y seguridad es el ABUELO Sunshine, no el padre Apollo.**
Este doc es el inventario de lo que Sunshine arregló y Apollo/Helios nunca recibió.

> Remote añadido (solo lectura): `sunshine → https://github.com/LizardByte/Sunshine.git`.
> Refrescar con `git -C Helios fetch sunshine --tags`. Para seguridad/plataforma, mirar Sunshine
> directamente además de Apollo (que ya casi no baja nada). Ver [[forks-helios-selene]], [[sunshine-issues-forums]].

## Topología del fork (medida, no estimada)

- **Merge-base Helios/develop ↔ sunshine/master = `1a96d135`** ("build(linux): update pkg-config systemd
  variable names" #4303), fechado **2025-09-26**. Es EXACTAMENTE el mismo punto que el merge-base de
  Apollo/master ↔ sunshine/master → **Apollo dejó de sincronizar de Sunshine el 2025-09-26** (justo tras
  la release Sunshine v2025.924.154138 del 24-sep-2025).
- **sunshine/master está 555 commits por delante** de ese merge-base, cubriendo **2025-09-27 → 2026-07-25
  (~10 meses)**. Todo eso es delta que Apollo/Helios NO tienen.
- **Merge-base Helios/develop ↔ upstream/master (Apollo) = `adc5c5a0`** ("Merge PR #1496 …ghsa-ph75…
  backport", 2026-05-21) → Helios forkeó de Apollo en **may-2026**, llevándose el backport GHSA de Apollo
  pero nada de los ~10 meses de Sunshine posteriores a sep-2025.
- Releases Sunshine que caen DENTRO de la brecha (Helios no las tiene): **v2026.516.143833** (16-may-2026,
  la del fix CVE-2026-32253), **v2026.724.182739** y **v2026.725.25407** (24-25 jul 2026).

**Difícil ≠ imposible, pero el árbol DIVERGIÓ fuerte** (churn de Helios/develop vs merge-base en los
archivos calientes, = riesgo de conflicto): `nvhttp.cpp` **+1070 líneas** (sistema de permisos de Apollo →
cualquier cherry-pick que lo toque es infierno), `video.cpp` +182, `platform/linux/misc.cpp` +105,
`crypto.cpp` +45 (fix propio de Helios), `vaapi.cpp` +11. Los fixes que tocan solo `video.cpp`/rutas de
encoder tienen conflicto MODERADO; los de captura Linux (misc.cpp/kmsgrab) MEDIO; los nuevos backends de
captura son ADITIVOS (archivos nuevos) pero arrastran cmake + submódulos.

## 🔴 SEGURIDAD (primero)

### Hallazgo clave: el auth-bypass CVE-2026-32253 YA lo tenemos, y MÁS estricto que Sunshine
Cross-check contra [[security-findings-2026-06.local]]: **NO debemos este backport — lo superamos.**
- Sunshine arregló el auth-bypass en **`888a6bb0`** ("Merge commit from fork", 2026-05-07, advisory
  GHSA-ph75-mgxh-mv57) tocando SOLO `src/crypto.cpp`: **quitó ÚNICAMENTE el case
  `X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY`** del `openssl_verify_cb`; **siguió haciendo `return 1`
  para `CERT_HAS_EXPIRED` y `CERT_NOT_YET_VALID`** (acepta certs expirados/no-válidos a propósito, por
  relojes de embebidos).
- **Helios/develop es MÁS estricto:** su `openssl_verify_cb` hace `return ok` (defiere TODO el veredicto a
  OpenSSL; los self-signed pineados siguen OK vía `cert_chain_t::verify()`). Verificado en
  `git show develop:src/crypto.cpp` — el comentario cita GHSA-ph75-mgxh-mv57 / CVE-2026-32253 y ya no hay
  línea `UNABLE_TO_GET_ISSUER_CERT_LOCALLY` ni `return 1`. Codificado como test rojo→verde (PR #5).
- **Conclusión:** Helios supera la postura de Sunshine en este CVE. **No backportear** el callback de
  Sunshine (sería un RETROCESO — reintroduciría la aceptación de expirados). Este era el punto de decisión
  #1 y queda cerrado a favor de Helios.

| # | Hash | Qué / por qué | Riesgo de NO backportear | Dificultad |
|---|------|---------------|--------------------------|------------|
| S1 | `2c59b2e6` | `fix(crypto): OpenSSL 4.x compatibility` (#5330, 24-jun). Ajusta uso de API de OpenSSL para compilar/correr con OpenSSL 4.x. | CachyOS avanza rápido; cuando el sistema pase a OpenSSL 4.x, Helios podría no compilar/romper crypto en runtime. Toca la ruta cripto (sensible). | **Media** — `crypto.cpp` divergió +45 líneas por el fix propio de Helios; conflicto probable pero acotado a esa unidad. Revisar a mano. |
| S2 | `ecba5c3c` | `fix(linux): security: drop CAP_SYS_ADMIN when possible, retain CAP_SYS_NICE` (#5075, 05-may). Suelta la cap privilegiada cuando no hace falta. | Hardening de menor privilegio. **OJO deploy:** Helios EN VIVO **necesita `cap_sys_admin` para KMS** (ver CLAUDE.md) → validar que el "when possible" NO rompa la captura KMS de Jordi antes de tomarlo. | **Media** — toca `platform/linux/misc.cpp` (+105 divergencia). Conflicto probable; requiere prueba de que KMS sigue capturando. |

Nota: los otros advisories que trackea [[security-findings-2026-06.local]] (CVE-2025-53095 CSRF, CVE-2024-*
pairing/file-read) viven en `confighttp.cpp`/`nvhttp.cpp` — auditarlos contra el estado de Helios es
trabajo aparte (nvhttp divergió +1070 líneas), no un cherry-pick.

## 🟠 ENCODER / NVENC / GPU (Blackwell RTX 5070 Ti + iGPU AMD — stack de Jordi)

| # | Hash | Qué / por qué | Riesgo de NO backportear | Dificultad |
|---|------|---------------|--------------------------|------------|
| E1 | `86a25385` | `fix(video): nullptr deref segfault on encode session teardown` (#5257, 28-jun). | Crash del daemon al cerrar/reiniciar sesión de encode — el daemon está EN PROD. | **Media** — `video.cpp` (+182 divergencia). |
| E2 | `7ecd0286` | `fix(video): UAF crashes during video reinit (reproducible on Vulkan)` (#5346, 28-jun). | Use-after-free → crash al reinit de video (cambio de códec/res, muy común con autoajuste de resolución de Jordi). | **Media** — `video.cpp`. |
| E3 | `e40d355f` | `fix(video): stream freezing on capture re-init (pipewire display switch)` (#5249, 26-jun). | Congelamiento de stream al re-init de captura (cambio de display). | **Media** — `video.cpp` + rutas de captura. |
| E4 | `1d9ab7b8` | `fix(linux): memory leak from unnecessary encoder re-probing` (#5404, 12-jul). | Fuga de memoria del daemon 24/7. | **Media**. |
| E5 | `32100783` | `fix(linux): auto-detect GPU with connected display for VAAPI and Vulkan` (#4961, 17-abr). | **Directamente el dolor "monitor equivocado" de Jordi** (Apollo #1543 CachyOS/KDE) en setup híbrido NVIDIA+iGPU AMD: elegir el GPU con display conectado. | **Media-alta** — toca `vaapi.cpp` + `misc.cpp` (ambos divergidos). |
| E6 | `38a94b3c` | `fix(linux/kms): skip NVIDIA cards for VAAPI on hybrid GPU laptops` (#4473, 25-ene). | Laptop/desktop híbrido NVIDIA+AMD: evita elegir mal el encoder VAAPI. Relevante al iGPU AMD + RTX de Jordi. | **Media**. |
| E7 | `225c3e9e` | `fix(linux/vulkan): encoder not working on NVIDIA GPUs` (#4994, 15-abr). | Habilita/arregla el encoder Vulkan en NVIDIA (ruta alterna a NVENC/VAAPI). | **Media** — parte del stack Vulkan nuevo (ver E-Vulkan abajo). |
| E8 | `a5af7907` | `feat(nvenc): split frame encoding on GPUs with 2+ nvenc blocks` (#4892, 19-abr). | **Blackwell RTX 5070 Ti tiene múltiples bloques NVENC** → menor latencia de encode. Perf, no crash. | **Media** — feature, `nvenc/`. |
| E9 | `5bacfd59` | `fix(nvenc): include bitstream restrictions in H.264/HEVC SPS` (#4556, 05-ene). | Corrección de conformidad del bitstream (decodificadores estrictos). | **Baja-media**. |
| E10 | `0db9f73e` | `feat(nvenc): intraRefresh + outputRecoveryPointSEI para h264/hevc` (#5091, 09-may). | Recuperación de errores sin keyframes completos → mejor resiliencia en red con pérdida (acceso remoto de Jordi por Internet). | **Media**. |

Contexto de riesgo de rebase (de [[sunshine-issues-forums]]): la base v2026.516.143833 metió
**regresiones de NVENC/captura** (#5147 FPS de captura, #5217 NVENC roto en tarjetas viejas). Tomar
commits SUELTOS de encoder evita arrastrar esas regresiones que un rebase completo sí traería.

## 🟡 WAYLAND / KMS / CAPTURA (CachyOS + KDE/KWin Wayland — stack de Jordi)

| # | Hash | Qué / por qué | Riesgo de NO backportear | Dificultad |
|---|------|---------------|--------------------------|------------|
| W1 | `a90d3068` | `feat(capture/linux): KWin direct screencast capture method` (#5009, 04-may). Nuevo `kwingrab.cpp` (713 líneas) + submódulo `plasma-wayland-protocols`. | **Método de captura NATIVO KWin** — el candidato más prometedor contra el stutter XWayland (#4884) y el "monitor equivocado" (#1543) del stack EXACTO de Jordi (KDE/KWin Wayland). | **Alta** — aditivo (archivo nuevo) PERO arrastra cmake + submódulo + glue en `misc.cpp`. Proyecto, no cherry-pick. |
| W2 | `874880e5` | `feat(linux)!: streaming vía XDG portals y Pipewire` (#4417, 03-feb). Nuevo `portalgrab.cpp` (1183 líneas). **Breaking (`!`)**. | Backend de captura por portal XDG (KDE/GNOME) — alternativa a KMS que no necesita `cap_sys_admin`. Base de la que dependen varios fixes posteriores de captura. | **Muy alta** — feature grande y breaking; cmake, submódulos, packaging. Solo si se adopta el stack de portal completo. |
| W3 | `a55d2d99` | `feat(linux): use connector name as default display name` (#5423, 21-jul). | Nombrar displays por conector → **selección de display predecible** (ataca #1543 monitor equivocado). | **Media**. |
| W4 | `bba6c6ca` | `feat(linux): pace capture at exact fractional NTSC framerates` (#5282, 30-jun). | Cadencia de captura a framerates fraccionarios exactos (23.976/29.97) → menos judder. | **Media**. |
| W5 | `40ae6c80` | `fix(linux): drop implicit DRM master for card fds` (#5286, 09-jul). | Evita tomar DRM-master implícito → menos conflicto con el compositor (pantallas negras/robo de sesión, cf. incidente KWin de Jordi). | **Media** — `misc.cpp`/kms. |
| W6 | `c2b5513a` | `fix(wayland): support DMA-BUF modifiers for wlroots capture` (#5132, 09-jun). | Corrección de captura wlroots (menos relevante en KDE, más si prueba sway/hyprland). | **Media**. |
| W7 | `a682ab07` | `fix(linux/kwin): retry init con privilegios elevados soltados si KWin no tiene CAP_SYS_NICE` (#5212, 29-may). | Robustez de init de captura KWin. Pareja de W1. | **Media**. |

## 🟢 ESTABILIDAD / CRASH / LEAKS (varios)

| # | Hash | Qué / por qué | Riesgo | Dificultad |
|---|------|---------------|--------|------------|
| C1 | `33aaefa8` | `fix(linux/pipewire): avoid memory leaks` (#5360, 03-jul). | Fuga en ruta pipewire (si se adopta W2). | Media |
| C2 | `ab52e27e` | `fix(audio-info): crash cuando el nombre de device tiene caracteres especiales` (#4095, 16-ene). | Crash de arranque por nombre de dispositivo de audio. | **Baja** — aislado. |

### Protocolo / moonlight-common-c (VIGILAR, no backportear a ciegas)
Sunshine bumpeó `third-party/moonlight-common-c` **muchas veces** en la brecha (últimos:
`…2ea4775→82e2514→703a069→e41355e`, #5402/#5428/#5439). El submódulo es **muy activo** y el protocolo debe
quedar ALINEADO host↔cliente (ver la skill `moonlight` / [[moonlight-doc-sintesis]]: no congelar el submódulo pero
tampoco romper ABI/ENet bundleado). **No bumpear el submódulo de Helios en aislado sin coordinar con
Selene** — es cambio de protocolo, riesgo de romper compatibilidad. Anotado como vigilancia, no como
cherry-pick suelto. También hubo `07317293 fix(input): don't send ALT when right-alt remapped to meta`
(#5318) y `3a69acef feat(rtsp): limit packetsize` (#5153) si se quiere afinar input/RTSP.

## TOP N — hacer primero

1. **S1 `2c59b2e6`** OpenSSL 4.x compat — evita que Helios deje de compilar cuando CachyOS suba OpenSSL.
2. **E1 `86a25385`** + **E2 `7ecd0286`** — dos crashes de encode/reinit; el daemon está EN PROD y el
   autoajuste de resolución de Jordi dispara reinits constantes.
3. **E5 `32100783`** (+ apoyo **E6 `38a94b3c`**) — auto-detección del GPU con display conectado: ataca
   directamente el "monitor equivocado" (#1543) del setup híbrido NVIDIA+AMD de Jordi.
4. **E4 `1d9ab7b8`** + **C1 `33aaefa8`** — leaks de memoria en un daemon 24/7.
5. **S2 `ecba5c3c`** — drop de CAP_SYS_ADMIN (hardening) **con la salvedad KMS**: probar que la captura
   KMS de Jordi sigue viva antes de adoptarlo.
6. **W1 `a90d3068`** (KWin direct capture) — el más prometedor contra el stutter XWayland de Jordi, pero
   es un PROYECTO (submódulo+cmake), no un cherry-pick de una tarde. Evaluar aparte.

## El resto (backlog)
E3, E7–E10 (encoder/vulkan/nvenc perf), W2–W7 (backends y fixes de captura Wayland), C2 (crash audio),
bumps de common-c (vigilar con Selene), fixes de input/RTSP. Priorizar cuando se toque cada área.

## Recomendación de estrategia: CHERRY-PICK selectivo, NO rebase
- **NO hacer un rebase/merge grande de Helios sobre sunshine/master.** Razones: (a) el árbol divergió
  fuerte — `nvhttp.cpp` +1070 líneas por el sistema de permisos de Apollo, `video.cpp` +182, misc/vaapi;
  un rebase de 555 commits sería un baño de conflictos sobre el core que da identidad a Apollo/Helios;
  (b) el daemon está EN PRODUCCIÓN (apollo.service, con NVENC y cap KMS) — un big-bang arriesga romperlo
  entero; (c) la base reciente de Sunshine (v2026.516) trae **regresiones conocidas de NVENC/captura**
  (#5147, #5217) que un rebase arrastraría "gratis".
- **Sí: cherry-pick QUIRÚRGICO por fix**, en ramas `fix/…`/`feat/…` una por ítem, priorizando los que
  tocan archivos poco divergidos (video.cpp, nvenc/, archivos nuevos de captura) y probando el daemon
  tras cada uno. Cada cherry-pick se hace en un **worktree de feature**, nunca sobre `develop` (que es de
  Jordi). Los backends nuevos (W1/W2) se tratan como PORT de feature, no cherry-pick.
- **Dado que Apollo está congelado, Sunshine es la fuente permanente:** dejar el remote `sunshine`,
  `fetch` periódico, y repetir este inventario cada tanto (o al aparecer un CVE) — Apollo ya casi no
  reconcilia su spaghetti río abajo.

## Ejecución de backports — tanda 2026-07-25 (worktree aislado `Helios-wt-backport`)

Cherry-picks quirúrgicos en `Helios-wt-backport` (worktree detached desde `develop@1ca4a957`), build
verificado con la receta dev (gcc-14, CUDA=OFF → VA-API). PRs abiertos contra `develop`.

**MERGEABLE (build verde, PR abierto):**
- **S1 `2c59b2e6`** OpenSSL 4.x compat → rama `fix/openssl-4x-compat`. Conflicto ÚNICO en `crypto.cpp`
  pero NO en el delta de auth-bypass de Helios (era en la creación del cert `X509_NAME`); resuelto
  tomando la versión de Sunshine (que además es la que compila, porque las líneas ya auto-fusionadas
  usan `name.get()`). Verificado: develop == merge-base en esa función → Helios nunca la tocó, era puro
  drift upstream. Trae también `tests/unit/test_crypto.cpp` (no compila con BUILD_TESTS=OFF).
- **E1 `86a25385`** (nullptr deref en teardown de encode) + **E2 `7ecd0286`** (UAF en reinit de video)
  → rama `fix/video-encode-crashes`. Ambos auto-fusionaron limpio en `video.cpp` pese a +182 divergencia.
- **E6 `38a94b3c`** (skip NVIDIA para VAAPI en híbridos) → rama `fix/skip-nvidia-vaapi-hybrid`. Limpio en
  `kmsgrab.cpp`. (Originalmente iba junto a E5 en `fix/gpu-display-autodetect`; al parquear E5 se renombró.)
- **Rebrand gamepad** → rama `chore/rebrand-gamepad-names`. Los 3 nombres "Sunshine … (virtual) pad" →
  "Helios …" en `inputtino_gamepad.cpp`. Vendor/product IDs INTACTOS.

**PARQUEADOS (para Jordi — NO forzados):**
- **E5 `32100783`** (auto-detect GPU con display conectado = dolor "monitor equivocado" #1543) —
  **CONFLICTO con divergencia real del fork.** Helios/develop **NO tiene `vulkan_encode.cpp`** (Apollo/
  Helios no bajó el stack de encoder Vulkan que Sunshine añadió post-merge-base). E5 está entrelazado con
  ese stack: bloque `#ifdef SUNSHINE_BUILD_VULKAN` en `video.cpp`, conflicto add/delete en
  `vulkan_encode.cpp`, y 3-way en `misc.cpp`/`video.cpp`/`macos/misc.mm`. Portarlo = proyecto (traer la
  infra Vulkan), no cherry-pick. **Es la feature más valiosa para Jordi** → vale la pena como proyecto
  aparte junto con E7 `225c3e9e` (encoder Vulkan en NVIDIA).
- **E4 `1d9ab7b8`** (leak de encoder re-probing) — **BLOQUEADO POR E5.** E4 (jul-2026) es posterior a E5
  (abr-2026) y su fix llama `platf::resolve_render_device()`, función que **introduce E5**. Sin E5 no
  compila (`'resolve_render_device' no es un miembro de 'platf'`, misc.cpp:955). Desparquear junto con E5.
- **S2 `ecba5c3c`** (drop CAP_SYS_ADMIN) — **SKIP a propósito:** Helios EN VIVO necesita `cap_sys_admin`
  para KMS (CLAUDE.md). No intentado.
- **W1 `a90d3068`** (KWin direct capture) — **SKIP:** archivo nuevo `kwingrab.cpp` + submódulo + cmake =
  port de proyecto, no cherry-pick. No intentado.

## 🎯 DECISIÓN DE CIERRE DE BRECHA (Jordi, 2026-07-26) — MERGE COMPLETO, no cherry-pick

Tras medir la superficie de choque real, Jordi decidió (AskUserQuestion, 3 respuestas):

**1) Estrategia = MERGE COMPLETO POR ETAPAS de `sunshine/master`** (no cherry-pick selectivo, no rebase).
Razón decisiva: es el ÚNICO que **AVANZA EL MERGE-BASE**. Con cherry-pick, el merge-base se queda en
2025-09-26 para siempre y CADA sync futuro repite los 561 commits. Matiz textual de Jordi:
*"completo y por etapas... pero SÓLO lo que no rompa lo que ya tenemos... si encuentras fixes en sunshine
que rompan algo en Helios, eres libre de hacer cherry-picking"* → **excluir lo que rompa, documentándolo**.

**2) Política de deltas = GANA SUNSHINE, salvo evidencia.** En internals de encoder/captura/timing cede
Apollo (Sunshine tiene 10 meses de pruebas en miles de máquinas vs un hack puntual).
- **CONSERVAR de Apollo:** Virtual Display + HDR (Windows), permisos granulares por-cliente, clipboard sync,
  comandos on-connect, OTP pairing, launch por UUID, y **nuestro fix CVE-2026-32253** (más estricto que Sunshine).
- **CEDER a Sunshine:** `min_fps_target` (Apollo `d25f2413` → Sunshine `44bf39be`), timing de encode /
  elasticidad de captura (Apollo `6c955482`+`1fd310b5` → Sunshine `bba6c6ca` pacing NTSC fraccional),
  naming/matching de displays KMS (`4b745485`+`a55d2d99`).
- **Regla cuando ambos tocaron lo mismo por razones distintas: INTEGRAR** (base Sunshine + feature de Apollo
  encima), nunca elegir un lado por comodidad.
- Tres commits NUESTROS ya eran backports a mano de Sunshine → en esos gana el original canónico:
  `68c6c8d2` (#5257/#5346 crashes de video), `46d8b058` (#5330 OpenSSL 4.x), `4ede6d2d` (#4473 VAAPI híbrida).

**3) Alcance del slice = merge + build verde en rama.** PR `feat/sync-sunshine-2026-07` + reporte de cesiones.
**El daemon EN VIVO no se toca** (`build-helios2` intacto como rollback); el despliegue y el QA visual los
decide Jordi después.

### Superficie de choque MEDIDA (no estimada) — el miedo estaba sobredimensionado
- 561 commits de Sunshine; **102 archivos en conflicto**, pero **93 archivos C++ que SOLO tocó Sunshine
  entran gratis** (cero conflicto).
- Solo **6 archivos** con peso real: `confighttp.cpp` (40 hunks), `nvhttp.cpp` (21), `config.cpp` (25),
  `process.cpp/h`, `video.cpp`, `platform/linux/misc.cpp`.
- **`virtual_display.cpp` (1198 líneas, el delta más grande de Apollo) NO existe en Sunshine → NO choca.**
  El temor "los deltas de VD/permisos chocan" era falso.

### ⚠️ Gotchas descubiertos al mergear (decisiones tomadas, NO reabrir)
- **Submódulo `moonlight-common-c` es OTRO REPO en cada lado:** Helios usa `ClassicOldSong/moonlight-common-c`
  (con las extensiones de protocolo de Apollo, de las que depende Selene); Sunshine usa
  `moonlight-stream/moonlight-common-c`. **Se conserva el nuestro** (pin `c999436`); el co-bump con Selene
  sigue siendo un slice APARTE y coordinado.
- Igual para **`Simple-Web-Server`** (fork de ClassicOldSong) y **`build-deps`** (rama `dist`, revert deliberado
  de Apollo en `1a87a6bd`).
- **`nanors`: Sunshine lo movió DENTRO de common-c (#5393) y eliminó `src/rswrapper.c/h` (#5369 `c42d20eb`).**
  Nuestro fork de common-c NO trae nanors (trae `reedsolomon/`) → habría roto el build. **Solución: nanors sigue
  standalone en `third-party/nanors`, BUMPEADO de `19f07b5` (2024-07) a `b1e3c22`** (el mismo pin que usa
  common-c upstream, ya con `oblas_common.c`), y se revirtieron las rutas en `cmake/compile_definitions/common.cmake`.
  `main.cpp` pasa de `#include "rswrapper.h"` a `#include <rs.h>` + `reed_solomon_init()`.
- **Web UI: se conserva la de Apollo EN BLOQUE** (`src_assets/common/assets/web/`) — Sunshine hizo un uplift
  grande de UI (#5225/#5166/#5158) que chocaba con las páginas de permisos/apps de Apollo. Adoptar el uplift
  de Sunshine queda como slice futuro. Los locales JSON siguen la misma regla.
- Se conservan borrados los workflows de CI de LizardByte (Apollo los quitó a propósito).
- Submódulos NUEVOS de Sunshine que SÍ entran: `glad`, `plasma-wayland-protocols`, `lizardbyte-common`,
  `nvapi` (antes `nvapi-open-source-sdk`). **Sin dependencias de sistema nuevas** (solo reorganización de los `.cmake`).

### 🎁 Lo que la brecha nos REGALA (verificado en el log)
- **YUV 4:4:4 por HW en NVIDIA** `39c9e845` + **4:4:4/4:2:0 con HDR** `e4740f06` → el backlog de 4:4:4 de Jordi.
  ⚠️ **Van por la ruta CUDA/cuda-gl y el daemon vivo compila con `SUNSHINE_ENABLE_CUDA=OFF`** → evaluar flip a CUDA=ON.
- **`4b745485` + `a55d2d99`** (match de display por nombre de conector) → arreglan **Apollo #1543**
  (CachyOS/KDE streamea el monitor equivocado) = el setup exacto de Jordi.
- Crashes: `7ecd0286` (UAF en reinit, Vulkan), `86a25385` (nullptr en teardown de encode), `e40d355f` (freeze en re-init de captura).
- `0752f641` encoder Vulkan · `71acfc57` VAAPI · `32100783` auto-detect de GPU con display conectado ·
  `40ae6c80` drop implicit DRM master · `fb3d85cf` selección dinámica del NV codec SDK.
- Bumps de `moonlight-common-c` (`b416e1e9`, `808b6223`) → NO tomados: son cambio de protocolo coordinado con Selene.

