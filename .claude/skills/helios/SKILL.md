---
name: helios
description: Knowhow del HOST Helios (fork de unjordi de ClassicOldSong/Apollo, que a su vez forkea Sunshine) — qué añade Apollo sobre Sunshine (virtual display, permisos por-cliente, clipboard), build en CachyOS (gcc-14, cap_sys_admin, NVENC), deploy apollo.service, el CVE arreglado, pain points (issues Apollo) y la hoja de ruta (VD nativo Linux #1477). Carga al tocar Helios/ o el host de streaming.
---

# Helios — el HOST del ecosistema Helios+Wolf+Selene

Destilado accionable. El detalle vive en las memorias enlazadas al final (§8) — este skill es el
mapa, no el territorio. Los números de issue evolucionan (snapshot 2026-07-25).

## 1. Qué es y linaje
**Cadena:** Sunshine (LizardByte) → **Apollo** (ClassicOldSong) → **Helios** (fork de unjordi).
- Helios ES Apollo rebrandeado — el codebase en `Helios/` es Apollo, mismo árbol Sunshine.
- Es el **host de la MÁQUINA REAL**: streamea el escritorio/sesión física (monitor real), NO headless.
- Habla **protocolo Moonlight** estándar → cualquier cliente Moonlight sirve; el cliente hermano es
  **Selene** (fork de Moonlight-qt). Comparten `moonlight-common-c` → **host y cliente evolucionan
  ALINEADOS o se rompe el handshake**.
- **Complementa a Wolf**, no compite: Helios = "streamea mi máquina real / continuar donde lo dejé"
  (sesión NO aislada, 1 host / 1 escritorio). Wolf (games-on-whales, MIT) = headless multi-usuario
  aislado. Roles limpios y sin solaparse.
- **Nombres (regla anti-drama):** SIEMPRE Helios/Selene, NUNCA Apollo/Artemis. Atribución ruidosa en
  README/About ("fork de Apollo, que es fork de Sunshine"). El unit de systemd sí sigue llamándose
  `apollo.service` (del paquete AUR — ver §3).

## 2. Delta de Apollo sobre Sunshine (lo que Helios HEREDA)
Sunshine ya hacía captura+encode+protocolo Moonlight. Apollo añade encima:
1. **Virtual Display integrado con auto-resolución/refresh/HDR** — crea un display virtual al arrancar
   el stream que calza la resolución/aspect/refresh/HDR del cliente y lo destruye al salir. Identidad
   FIJA por cliente (el SO recuerda la config de display por dispositivo). **Windows-only hoy** (driver
   **SudoVDA**); en **Linux es no-op** (`create_settings_manager()` devuelve `nullptr`, el capability
   nunca se anuncia → clientes ven "server doesn't support virtual displays"). Es la feature estrella.
2. **Permisos granulares por-cliente** — el **PRIMER cliente emparejado recibe TODOS los permisos**;
   los siguientes solo `View Streams` + `List Apps`, hay que **concederles el resto a mano** en la web UI
   o ven *Permission Denied*. Bits: Launch/List Apps, Clipboard Set/Read, Server Command, Controller/
   Touch/Pen/Mouse/Keyboard Input, View Streams.
3. **Clipboard sync** host↔cliente (gobernado por los permisos Clipboard Set/Read).
4. **Comandos on-connect / on-disconnect** — ejecutar al conectar/desconectar un cliente (ej. auto
   pause/resume del juego). Se apoya en el patrón do/undo de `global_prep_cmd` heredado de Sunshine.
5. **Input-only mode** — cliente que solo manda input (couch co-op / control remoto).
6. **Dual-GPU + headless "seamless"** — fijar `Adapter Name` al dGPU + `Headless mode`, sin dummy plug.

## 3. Build & deploy en ESTA caja (CachyOS + NVIDIA Blackwell)
Detalle en `HeliosSelene/CLAUDE.md` y [[forks-helios-selene]]. Lo esencial:

**Build (dev):** gcc-14 es **OBLIGATORIO** (el sistema trae gcc-16 → rompe con `bad_weak_ptr`).
```
cmake -S Helios -B Helios/build -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DBUILD_WERROR=OFF -DSUNSHINE_ENABLE_CUDA=OFF
make -C Helios/build -j$(nproc)
```
Deps: `gcc14 nodejs npm appstream-glib` (+ runtime del AUR `apollo`).

**⚠️ CRÍTICO — cap_sys_admin para KMS (por-ARCHIVO):**
```
pkexec setcap cap_sys_admin+p "$(readlink -f Helios/build/sunshine)"
```
Sin la cap NO captura display (KMS monitor list vacío → "probing failed" → todos los encoders fallan).
Es **por-archivo** y el binario lleva hash de versión (`sunshine-0.0.0.<hash>`) → **cada rebuild pierde
la cap → re-aplicar**. (`pkexec` = diálogo KDE; correr en background para que no muera por timeout.)

**Encode EN VIVO:** el binario de prod (`build-helios/`) encoda por **NVENC** (h264/hevc/av1; KMS
captura el monitor en la NVIDIA). OJO: esto **difiere** de la receta de build de arriba (`CUDA=OFF` →
VA-API); el binario en vivo se compiló con **CUDA/NVENC ON**.

**Deploy (reversible):** servicio `apollo.service` (systemd `--user`). Override en
`~/.config/systemd/user/apollo.service.d/override.conf` fija `ExecStart` al binario de dev
(`build-helios/`). GPU: RTX 5070 Ti (Blackwell) + iGPU AMD Radeon.

**Runbook re-deploy tras cambiar código:** 1) `git pull` develop → 2) `make -C build sunshine -j$(nproc)`
→ 3) **re-`setcap cap_sys_admin+p`** (¡el rebuild la perdió!) → 4) `systemctl --user restart apollo.service`
→ 5) verificar journal + `curl -ks -o/dev/null -w '%{http_code}' https://localhost:47990` (=307).

**Rollback:** backups en `~/helios-rollback/`; o `systemctl --user revert apollo.service` → vuelve a
`/usr/bin/apollo` del AUR **PERO sin el fix CVE-2026-32253** → evitar (ver §5, §6).

> **INTOCABLE el daemon de PROD.** El host en vivo sirve a Internet; no lo reinicies/rompas sin
> intención. Cambios reales van por rama→PR→deploy con el runbook, no ad-hoc sobre el binario vivo.

## 4. ⚠️ Pain points / a medio cocer (issues de Apollo — snapshot 2026-07-25)
- **Apollo en CONGELACIÓN DELIBERADA (#1512).** Feature-complete según el maintainer; NO invierte en la
  base Sunshine ("spaghetti mixed with cement"), apuesta a una reescritura de bajo nivel sin fecha. **Sin
  release formal desde ~10 meses** (último tag `v0.4.8` sin binarios). **Consecuencia dura:
  Blackwell/Wayland/VD-Linux es trabajo de Jordi o de NADIE** — no esperes que upstream lo resuelva.
- **#1543 — Wrong Monitor en CachyOS/KDE Wayland (⚠️ SETUP EXACTO DE JORDI).** Sin VD en Linux, Helios
  streamea el **monitor equivocado** (agarra uno lateral/vertical) y **NO hay setting** para elegir qué
  display capturar. Dolor directo a resolver en Helios.
- **#1162 — NVENC falla a inicializar en RTX 5070 Ti (⚠️ MISMA GPU).** Falla en Artemis con NVENC
  mientras Moonlight funciona igual → apunta a bug del lado cliente/negociación, no del encoder.
- **VD nativo en Linux no existe (#1161).** → **#1477 "EDID-override vía debugfs" = el PR A RESCATAR**
  (experimental, ~60-70%, sin mergear; AMD/Intel+KDE/GNOME Wayland OK, NVIDIA/X11 sin validar). Ver §7
  sobre por qué esto quedó SOLTADO en el roadmap actual.
- **Fugas de VRAM en VD-Windows (#1544):** dwm.exe fuga ~0.5 MB/redraw mientras SudoVDA está attach
  (~100 MB/min sin streamear). Contexto, no aplica al deploy Linux de Jordi.
- Long tail ruidoso: familia "Slow Connection"/stutter cada 1-3 s (#608/#1019/#1523…), audio crackling
  Linux (#1422), crashes VAAPI/detección de GPU. No heredar ciegamente regresiones (ver [[issue-triage-2026-06]]).

## 5. 🔧 Trucos / gotchas
- **Puertos abiertos a Internet = DISEÑO INTENCIONAL, NO falla.** `47984-48010` (+ los de Wolf) en
  `allow 0.0.0.0/0` es a propósito (Jordi accede desde fuera de la LAN). Cita de Jordi: *"no es falla de
  seguridad sino decisión de diseño"*. **NO lo reportes como hallazgo.**
- **NO revertir al binario AUR sin el fix CVE.** `/usr/bin/apollo` NO trae el fix de CVE-2026-32253; con
  los puertos abiertos, revertir = exposición REAL. Mientras corra `build-helios` (con el fix) = deseado.
- **Autoajuste de resolución host↔cliente (= frustración #2, la que SÍ sigue siendo de Helios).** Apollo
  expone `SUNSHINE_CLIENT_WIDTH`/`SUNSHINE_CLIENT_HEIGHT` a la app lanzada; se aplica al host con
  **`wlr-randr`** (wlroots) o **`kscreen-doctor`** (KDE), fallback `--resolution` → `xrandr --current` →
  guardado. En KDE Wayland (escritorio real de Jordi) es **frágil** → revisar cómo Helios aplica/revierte
  el modo. Wolf NO cubre esto (es del caso "streamea mi máquina real").
- **Captura KMS necesita `nvidia_drm.modeset=1`** para que el KMS liste el monitor en la NVIDIA.
- **Pairing por cert-CN en LAN (interop con Selene).** El cert de Helios está firmado para
  `unjordi.pisa.mx` pero en LAN Selene descubre por IP (`192.168.1.250`) → CN no cuadra → **pairing falla
  en "stage #4"** (host marca "success", cliente no completa). Selene hace cert-PINNING → solo tropieza en
  el PRIMER pairing LAN contra el cert de CN público.

## 6. Seguridad
- **🟢 CVE-2026-32253 / GHSA-ph75-mgxh-mv57 — auth-bypass (CVSS ~9.8), ARREGLADO (Helios PR #5).**
  `openssl_verify_cb` (`src/crypto.cpp`) hacía `return 1` para `CERT_HAS_EXPIRED`/`CERT_NOT_YET_VALID` →
  aceptaba certs de cliente expirados/no-válidos. Fix: el callback deja el veredicto a OpenSSL
  (`return ok`); los self-signed pineados siguen OK vía `cert_chain_t::verify()`. **Codificado como test
  rojo→verde** (`SecurityCertValidation`, 4/4). Tradeoff: clientes con reloj muy desfasado deben
  sincronizar hora antes de emparejar.
- **#1546 — 2 GHSA advisories de Apollo** (`ph75-mgxh-mv57` ya con fix backporteado en commit #1496;
  la otra `6p7j-5v8v-w45h` revisar). Relacionar con el hallazgo propio de arriba.
- **Heredables (TODO):** CVE-2025-53095 (CSRF→cmd injection en `confighttp.cpp`), CVE-2024-31220
  (file-read sin auth), pins de deps (OpenSSL/curl/ffmpeg). Pairing fuera-de-orden y RTSP overflows =
  ya MITIGADOS. Si algún día se mergea #1477: auditar el helper `apollo-vdisplay-helper` (`cap_dac_override`
  → allowlist estricta del conector, EDID 256B sin traversal). Detalle en [[security-findings-2026-06.local]].

## 7. Lo que YA sabemos / validamos
- **Helios corre EN VIVO** (2026-06-28→): binario propio `build-helios` con el fix CVE (no el AUR 0.4.8),
  rebrandeado, servicio active, web UI 307 en :47990, puertos 47984/47989/47990 escuchando.
- **NVENC verificado** (h264/hevc/av1, journal 2026-07-24) + **captura KMS OK** con `cap_sys_admin`.
- **CI de Linux propio verde** + suite googletest reparada (gate duro). Rebrand mergeado a `develop`.
- **VD-Linux (#1477) SOLTADO/superseded (2026-07):** era la motivación #1 de forkear, pero **Wolf** cubre
  mejor el caso headless/aislado (contenedor, no toca el monitor físico, multi-sesión). Helios ya NO
  invierte en display virtual. El plan `docs/design/virtual-display-linux.md` queda como referencia
  histórica. **Rol limpio de Helios ahora:** "streamea tu MÁQUINA REAL bien" = seguir bajando seguridad
  de upstream + frustración #2 (resolución/HDR nativo en KDE Wayland).
- **Git (norma de Jordi):** nunca push directo a `master`/`develop`; ramas `feat/fix/chore/docs` → PR →
  auto-merge server-side (1-3 devs). `master` = espejo de upstream para `git fetch upstream` limpio.

## 8. Referencias
- [[apollo-doc-sintesis]] — qué añade Apollo sobre Sunshine, arquitectura, caveats (fuente de §1-§2).
- [[apollo-issues-forums]] — barrido de issues/PRs/roadmap del maintainer (fuente de §4).
- [[forks-helios-selene]] — plan de forks, deploy en vivo, decisión de producto Helios+Wolf+Selene.
- [[security-findings-2026-06.local]] — CVE-2026-32253, advisories heredables, puertos = diseño.
- [[deploy-streaming-y-resolucion]] — deploy en vivo + mecanismo del autoajuste de resolución + gotcha cert-CN.
- [[issue-triage-2026-06]] — triage de los 4 trackers, PRs huérfanos a rescatar, mapa dolores→issues.
- Skills hermanos: **`sunshine`** (la base upstream) + **`moonlight`** (el protocolo host↔cliente).
