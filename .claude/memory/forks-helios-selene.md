---
name: forks-helios-selene
description: "Plan de forks de Apollo (host) y Artemis (cliente) de streaming — nombres Helios/Selene, scope, fases y workflow"
metadata: 
  node_type: memory
  type: project
  originSessionId: 6e6fd470-356b-4c89-b5aa-355a84ae98b4
---

Jordi va a tomar la rienda de dos repos de game streaming semi/abandonados y mantenerlos como forks oficiales en `github.com/unjordi`. Workspace local: `/home/unjordi/code/HeliosSelene/` (carpeta NO-git que contiene los dos clones; renombrado de `ApolloArtemis`→`HeliosSelene` el 2026-06-30).

## Los upstreams (estado a 2026-06-28)
- **Apollo** = host, fork de Sunshine (LizardByte). Upstream `ClassicOldSong/Apollo`, rama `master`. **En NUESTRO fork Helios la rama de integración es `develop`** (creada 2026-06-28, default + protegida); `master` queda como espejo del upstream para `git fetch upstream` limpio. Último commit ~2026-05-21 (solo backports de seguridad de terceros). 200+ issues abiertos (incl. #1512 "¿es abandonware?"). Lo MÁS vivo de los dos.
- **Artemis** = cliente, fork de Moonlight Qt. Upstream `wjbeckett/artemis`, rama `develop`. Último commit **2025-08-31** (~10 meses, muerto). 20 issues. Builds dev rotos (flatpak GL `/app/gl-default`, AppImage sin ELF) — coincide con el diagnóstico previo (proyecto Juegos) que lo dio por descartado.
- Comparten el submódulo **moonlight-common-c** y el **protocolo Moonlight** → host y cliente DEBEN evolucionar alineados o se rompe el emparejado/compatibilidad.

## Nombres de los forks (decidido 2026-06-28)
Regla anti-drama (caso Azahar/Citra): nombre NUEVO y distinto + atribución RUIDOSA en README/About/releases ("fork de Apollo, que es fork de Sunshine"). Nunca reusar nombre ni ícono.
Se hereda el ADN sol/luna del linaje (Sunshine=sol, Moonlight=luna; Apollo/Artemis = gemelos sol/luna):
- **Host (fork de Apollo) → `Helios`** (titán del sol)
- **Cliente (fork de Artemis) → `Selene`** (titanide de la luna)
Verificado: sin colisiones en GitHub en el espacio de streaming; `unjordi` no tenía repos previos con esos nombres. (Descartados: Sol+Luna por genéricos, Ra+Khonsu por romper el aire griego.)

## Scope y fases (decidido 2026-06-28)
- **Ambición:** "itch primero, crecer luego" — forkear oficial (público) pero priorizar las necesidades reales de Jordi (que ya son features que faltan en upstream); estructura lista para crecer a fork comunitario, SIN comprometerse a los 200 issues el día 1.
- **Orden:** ambos en paralelo para mantenerlos alineados en features/compatibilidad. **Fase 0** cimientos (submódulos + forks + cerebro del proyecto) → **Fase 1** frustraciones de Apollo/Helios → **Fase 2** resucitar Artemis/Selene.
- **Workflow git** (norma de Jordi): nunca push directo a develop; ramas feat/fix/docs → PR → merge a `develop` (auto al instante, 1-3 devs); `upstream` configurado para seguir bajando seguridad. **TODO va a `develop` en AMBOS forks** (Helios y Selene).
  - **Branch protection activa (2026-06-28)** en `develop` de ambos: PR requerido (0 aprobaciones, sigue mergeando al instante), force-push y borrado bloqueados, SIN status checks requeridos (el CI heredado tiene jobs rotos macOS/Windows; añadir solo el build de Linux tras el triage), admins NO incluidos (escape de emergencia).
  - **GOTCHA merge-gate:** el clasificador de seguridad de Claude Code BLOQUEA que el agente haga `gh pr merge` de un PR que él mismo generó (self-approval), aunque la norma diga "merge al instante". → los PR generados por Claude los mergea Jordi a mano (o añade una regla de permiso Bash en settings). Crear rama/push/abrir PR sí está permitido.

## DESPLIEGUE en el host (2026-06-28) — Helios corriendo en vivo
El host CachyOS expuesto en `unjordi.pisa.mx` ahora corre **NUESTRO binario Helios** (con el fix del auth-bypass), no el `apollo` 0.4.8 del AUR.
- **Mecanismo (reversible):** override de systemd user (comando/ruta exactos en [[project_streaming_helios_apollo]],
  memoria global) apuntando al binario in-place del build tree; encuentra `build/assets` por ruta absoluta
  horneada — **OJO:** la ruta vieja quedó horneada en el binario; tras renombrar el workspace
  `ApolloArtemis`→`HeliosSelene` (2026-06-30) hay que **RECOMPILAR** para re-hornear la ruta nueva, o el
  binario buscará assets en la ruta vieja inexistente. Receta de build en CLAUDE.md. Capability replicada vía
  `setcap` (comando exacto en la memoria global) sobre el binario real (`sunshine-0.0.0.*`) para captura KMS/NVENC.
- **ROLLBACK (1 comando):** `systemctl --user revert apollo.service && systemctl --user restart apollo.service` → vuelve al `/usr/bin/apollo` del AUR.
- **Verificado:** servicio active, NVENC ok, web UI 307 en :47990, puertos 47984/47989/47990 escuchando.
- **CAVEATS:** (1) si recompilo el binario, **pierde la cap** → re-`setcap`. (2) el binario es user-writable con cap_sys_admin (menos limpio que root-owned); para producción seria, hacer un **paquete Arch propio** (makepkg del fork) y `pacman -U`, en vez del override al build tree. (3) `make install` a /usr/local se atascó (pkexec + rebuild web-ui >2min); el override al build tree fue la vía limpia. (4) pkexec necesita que Jordi autorice el diálogo KDE — correr en background para que no muera por timeout.

## Estado al cierre del día 1 (2026-06-28) — todo en `develop`, CI verde
**Helios `develop`:** rebrand + CI de Linux propio (`.github/workflows/linux-build.yml`, build en Ubuntu 24.04 vía `scripts/linux_build.sh --skip-cuda --skip-package --skip-cleanup --ubuntu-test-repo`) + suite googletest **reparada y verde** (gate duro) + **fix del auth-bypass** (ver [[security-findings-2026-06.local]]). Mergeados: PR #1/#2/#3 (rebrand/README/plan), #4 (CI), #5 (fix seguridad + tests).
**Selene `develop`:** rebrand + suite **QtTest nueva** (`tests/`, `qmake CONFIG+=test`, 22/22 verde) + CI propio (`.github/workflows/unit-tests.yml`) + fixes (#48 scripts, #65 refrescos, #62 flatpak). Mergeados PR #1-#3, #5-#9. **Único PR abierto: Selene #4** (CI macOS+Windows, espera validación en la Mac de Jordi — ver `RETOMAR-validacion-macos.md`).
**Host:** corre el binario Helios fixed (ver sección DESPLIEGUE arriba).

### Comandos de build/test (reproducibles)
- **Helios daemon:** `cmake -B build -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 -DCMAKE_BUILD_TYPE=Release -DBUILD_DOCS=OFF -DBUILD_WERROR=OFF -DSUNSHINE_ENABLE_CUDA=OFF -DCUDA_FAIL_ON_MISSING=OFF` → `make -C build sunshine -j$(nproc)` → `build/sunshine`.
- **Helios tests:** `cmake -B build -DBUILD_TESTS=ON` → `make -C build test_sunshine` → `./build/tests/test_sunshine --gtest_filter='ConfigParse.*:UtilHex.*:SecurityCertValidation.*:MdnsInstanceNameTests/*:FileHandler*'` (los HW/platform tests necesitan GPU/display; el PairingTest está `DISABLED_` por estado global).
- **Selene tests:** `qmake6 CONFIG+=test tests/tests.pro && make -j$(nproc)` → correr `tests/otpcrypto/tst_otpcrypto` y `tests/refreshrate/tst_refreshrate` (o `make check`).
- **RE-DESPLEGAR Helios al host tras cambiar código (runbook, actualizado 2026-07-25):** 1) `git pull` develop,
  2) rebuild el daemon (dir de build vivo + gotcha del cache stale por el rename `Apollo/`→`HeliosSelene`
  documentados en CLAUDE.md), 3) re-aplicar la capability vía `pkexec setcap` (¡el rebuild SIEMPRE pierde la
  cap!; comando exacto en [[project_streaming_helios_apollo]], memoria global), 4) `systemctl --user restart
  apollo.service`, 5) verificar journal (`Found H.264/HEVC/AV1 encoder: *_nvenc` + KMS captura el monitor) +
  `curl -ks -o/dev/null -w '%{http_code}' https://localhost:47990` (=307). Rollback: repuntar `ExecStart` al
  binario pre-backports (aún con cap) o a un respaldo (ruta exacta en la memoria global).

### Pendientes (para retomar)
- **Probar el stream end-to-end** con Deck/cel (lo desplegamos pero no se probó streameando).
- Validar macOS en la Mac (Selene #4) → mergear.
- Activar **status-checks requeridos** en branch protection de ambos `develop` (ya que el CI es verde/bloqueante).
- ~~**#1477** display virtual Linux (buque insignia)~~ **SOLTADO 2026-07 (superseded por Wolf).** Headless
  era la motivación #1 de forkear Apollo; Wolf ya lo resuelve mejor (contenedor aislado, no toca el monitor
  físico, multi-sesión). Helios NO invierte más en display virtual. El plan `docs/design/virtual-display-linux.md`
  queda como referencia histórica. Ver §DECISIÓN DE PRODUCTO abajo y [[diseno-headless-multisesion]].
- Limpieza Selene: que `OTPPairingManager` delegue a `app/utils/otpcrypto.h` (de-dup + quita el `qDebug` que loguea el PIN); borrar `test_otp_hash.cpp`/`test_hash.py` viejos.
- Empaquetado Arch propio de Helios (para reemplazar el override-al-build-tree por un `pacman -U` limpio).

## Display virtual en Linux — enfoque decidido (2026-06-28)
Plan detallado en el repo Helios: `docs/design/virtual-display-linux.md` (PR Helios #3). Investigado a fondo (PR #1477 + arquitectura de captura).
- **Enfoque elegido:** adoptar/terminar **Apollo PR #1477** (autor `AdivonSlav`) = **EDID-override vía debugfs** sobre un conector DRM físico **desconectado**, capturado por el `kmsgrab` de siempre. **NO** usa kernel module, NO evdi, NO driver tipo SudoVDA. Helper privilegiado `apollo-vdisplay-helper` (cap_dac_override, rutas allowlisted). Estado del PR: ~60-70%, funciona en AMD/Intel+KDE/GNOME Wayland (verificado por terceros), NVIDIA/X11 sin validar, estancado en review.
- **Complemento:** fallback pluggable estilo **Sunshine PR #4762** (`output_name` por nombre de conector + `pre_probe_cmd` do/undo con `SUNSHINE_CLIENT_*`) para casos sin conector libre / headless. = formaliza el hack de scripts externos de Jordi.
- **Puntos de integración clave:** `src/platform/linux/misc.cpp:955` (selector de captura), `src/process.cpp:236` (bifurcación `#ifdef _WIN32` donde enchufar el create/destroy en Linux), `src/nvhttp.cpp` (flag de capability VD, hoy `#ifdef _WIN32` → por eso Selene dice "no soportado"; hay que extenderlo = cambio host↔cliente alineado), `libdisplaydevice` = nullptr en Linux.
- **PREGUNTA ABIERTA crítica (Fase 0):** en KDE Wayland el backend de captura por DEFAULT es **wlr-screencopy** (por nombre de output), pero #1477 captura por **KMS** (por índice). Hay que confirmar/forzar cuál captura el conector virtual ANTES de nada.
- **Decisiones que faltan de Jordi:** ¿qué GPU usa el host? ¿tiene un conector desconectado libre? (definen el riesgo NVIDIA y si EDID-override aplica). Recomendado: colaborar con AdivonSlav en #1477, no hard-fork.

## Las frustraciones de Apollo = la hoja de ruta (del deploy real, ver [[deploy-streaming-y-resolucion]])
Casi todos los hacks externos de Jordi son features que faltan en el host:
1. ~~**Display virtual nativo en Linux/Wayland** (LA estrella)~~ → **AHORA LO CUBRE WOLF** (2026-07). El caso
   headless/aislado (correr un juego sin tocar el monitor físico, multi-sesión) lo resuelve Wolf en contenedor.
   Helios ya NO persigue display virtual. La disrupción del monitor físico que Jordi sufría se evita corriendo
   el juego en Wolf, no en la sesión real.
2. **Cambio de resolución/HDR nativo en KDE Wayland** (hoy: `apollo-set/restore-resolution.sh` por fuera).
   ← **ESTA sí sigue siendo de Helios** (aplica al caso "streamear mi máquina REAL"); Wolf no la cubre.

## Alcance de plataformas (decidido 2026-06-28)
- **Selene (desktop Qt):** Linux (primario) + **macOS (sí importa, por encima de Windows)** + Windows (baja prioridad). Jordi tiene una **Mac para ajustes nativos** → si un fix de macOS no queda al primer intento en CI, NO hacer CI ping-pong: terminarlo en la Mac.
- **Backlog de forks de cliente adicionales (pedido de Jordi 2026-07-19):** forkear Moonlight para **Android** y para **PSVita** — cada uno es OTRA base de código, NO sale de Selene (que es Qt desktop):
  - **Android:** `ClassicOldSong/moonlight-android` (Kotlin/Java; Artemis Android / Moonlight Noir).
  - **PSVita:** base `moonlight-vita`/`vita-moonlight` (C, SDK homebrew VitaSDK).
  - Orden acordado: **primero refinar Helios + Selene desktop, LUEGO expandir** a Android/Vita. Nombres a decidir (mantener el linaje sol/luna o variantes de Selene por plataforma).
- **CI de Selene (estado tras triage 2026-06-28):** sanos = Linux, RPi, Windows x64, **Flatpak (arreglado, PR #2 verde en CI)**, Compile-Sanity Ubuntu. Desactivados con `if:false` (no borrados) = Windows ARM64 + Universal Installer. macOS (PR #4, rama `chore/ci-triage`) — DOS causas en `globaldefs.pri` scope `macx`:
  1. `qyieldcpu.h`/`__yield` + clang nuevo → `-Wno-error=implicit-function-declaration`. **VERIFICADO que funciona** (el error pasó a warning, el build avanzó al link).
  2. `ld: framework 'AGL' not found` — Apple quitó AGL de los SDK nuevos (Xcode 16+/macOS 15+) pero el mkspec opengl de Qt aún lo linkea → `QMAKE_LIBS_OPENGL[_QT] -= -framework AGL` (usamos Metal/QuartzCore). **Aplicado y VALIDADO POR USO (2026-07-25):** el `Selene.app` del Mac es el cliente que decodifica HEVC (VideoToolbox, M4 Pro) en TODOS los tests multi-sesión 2-IP → el fix macOS PR#4 sirve en la práctica. La receta (aqt Qt, strip de AGL en los `.prl`) sigue siendo frágil, pero el binario compila y corre. (Comando nativo: `qmake6 artemis.pro CONFIG+=release && make`.)
  Windows ARM64 + Universal Installer = `skipped` OK (desactivados con `if:false`).
  **Gotcha:** el workflow dispara en push a `feat/**`/`fix/**` pero NO en `chore/**` ni `pull_request` → para validar ramas chore: `gh workflow run dev-build.yml --ref <rama>`.

## Selene (cliente) cuando toque
1. Arreglar builds (flatpak/AppImage) — prerequisito.
2. Detección dinámica de resolución dock/undock nativa (hoy: `moonlight-auto.sh` por fuera).
3. Bugs reportados que pegan a Jordi: #65 refrescos fraccionarios, #66 no guarda settings.
4. **Bug i18n en el CLI de pairing (hallado 2026-07-19):** el mensaje "Emparejando… Introduce el PIN" no sustituye el placeholder `%1` (loguea `QString::arg: Argument missing: ... Introduce '1%' en 1234`). Cosmético, el pairing funciona. Fix: revisar la string de pairing en el cliente (`app/cli/pair.cpp`/i18n `es`).
- **Pairing headless: PROBADO funcionando** (`Artemis pair <host> --pin 1234` con `QT_QPA_PLATFORM=offscreen`; el cliente hace poll esperando que el host acepte el PIN vía `POST /api/pin` que exige login admin del web UI). Único input faltante: **user/pass del web UI de Helios**. Receta completa en el reporte del turno 2026-07-19.

- **Upstream Artemis (wjbeckett/artemis) muerto en la práctica (confirmado 2026-06-28):** sus builds dev están
  rotos (flatpak: bug GL `/app/gl-default`; AppImage: sin binario ELF) y el repo lleva ~9 meses sin tocarse →
  Selene se compila **DESDE FUENTE**, nunca de esos binarios. (Refuerza por qué forkear tuvo sentido.)

Submódulos de ambos clones ya inicializados (`--init --recursive`) el 2026-06-28.
Relacionado: [[deploy-streaming-y-resolucion]] (setup, gotchas, autoajuste de resolución host↔cliente +
gotcha de pairing por cert-CN en LAN).

## DECISIÓN DE PRODUCTO (2026-07, con Jordi): ecosistema = Helios + Wolf + Selene
Tras validar Wolf en vivo (aísla input por diseño, containerizado, MIT), Jordi decidió que el ecosistema
usa **ambos servidores**:
- **Helios** (fork Apollo/Sunshine) = host para streamear la **máquina REAL** (monitor/sesión física).
- **Wolf** (games-on-whales, MIT, `~/code/ajenos/wolf`, deploy en `wolf-deploy/`) = backend **headless
  multi-sesión** aislado (la visión "yo con mi Steam, novia con el suyo"). Es su PROPIO server Moonlight
  (no wrapper de Sunshine), orquesta Docker+gamescope+GStreamer+inputtino.
- **Selene** (fork Moonlight-qt) = cliente único, habla a ambos.

**CONSECUENCIA para el roadmap (CONFIRMADO por Jordi 2026-07):** Wolf cubre el HEADLESS, que era la
motivación #1 de forkear Apollo. Por tanto el trabajo de **display virtual en Linux (#1477)** dentro de
Helios — antes marcado "LA estrella" — queda **SOLTADO/SUPERSEDED** (Wolf lo hace mejor: aislado, sin tocar
el monitor físico). **Roles limpios y sin solaparse:**
- **Helios** = "streamea tu MÁQUINA REAL, bien": fix de seguridad CVE (hecho), rebrand (hecho), seguir bajando
  seguridad de upstream Apollo, y la frustración #2 (resolución/HDR nativo en KDE Wayland — Wolf NO la cubre).
- **Wolf** = toda la inversión de headless / multi-usuario / aislamiento.
- **Selene** = cliente único de ambos.
Ver [[diseno-headless-multisesion]].

### ⏸️ Wolf: por qué se abandonó (2026-07-24) — la RAZÓN REAL, confirmada por Jordi
> Ítem vivo en [[estado-proyecto]] #18. Aquí solo el porqué.
El camino headless-Wolf queda **EN PAUSA**. La razón real, **confirmada por Jordi el 2026-07-24** (NO la que creíamos):
- **NO fue un bug de Wolf abierto.** El blocker Blackwell (issue #417) YA estaba arreglado en `:stable` por PR#425
  (merged 2026-06-08); el deploy usaba `:stable`. PR#424 fue descartado. No hay issue de encode Blackwell abierto.
- **NO fue el incidente KDE** — ese fue el 23-24 jul, **DÍAS DESPUÉS**, cosa aparte.
- **LO QUE DE VERDAD PASÓ (palabras de Jordi):** se resolvió el titileo, se ancló la RTX 5070 a Wolf, y **SÍ funcionó
  hasta conectarse a Wolf y lanzar las Wolf-apps con Selene desde la Mac (UNA conexión — Firefox streameó).** El MURO fue
  el **paso siguiente y objetivo real: DOS conexiones SIMULTÁNEAS** (Jordi + Liora). Ahí fueron **crashes una y otra vez
  y NUNCA se logró estabilidad.** Nunca se alcanzó la prueba Steam+Liora. (Ojo: Selene→**Helios** normal SÍ funciona; la
  inestabilidad es específica de Wolf, y el punto que reventó fue la MULTI-SESIÓN.)
- **Gate real de retry = ESTABILIDAD de Wolf, en particular MULTI-SESIÓN (2 conexiones simultáneas)** — diagnosticar y
  matar los crashes. NO un fix de upstream ni el flip de GPU. **Leads concretos:** core dumps de Wolf preservados
  (ruta exacta en [[project_streaming_helios_apollo]], memoria global; timestamps 07-21 21:57Z y 22:21Z;
  07-24 01:14Z) — punto de partida del debug.
- **Cómo retomar:** `wolf-deploy/` listo (`:stable`, método manual NVIDIA), config en `/etc/wolf`. Orden: (1) reproducir
  el crash con `WOLF_LOG_LEVEL=DEBUG` + analizar los backtraces; (2) recién con conexión estable, ir por Steam+Liora.
- **POR QUÉ IMPORTA / criterio de éxito (Jordi):** *"sin eso [multi-sesión estable], nomás tenemos UN headless
  inestable"* — y **Helios ya cubre el single / máquina-real**. Wolf SOLO justifica su lugar en el ecosistema si logra
  **multi-sesión ESTABLE** (2+ conexiones simultáneas sin crashear). El éxito del retry no es "que prenda", es eso.
- **Intento de verificación 2026-07-24 (Claude, en vivo):** Wolf `:stable` levanta LIMPIO y **estable en idle**
  (RestartCount=0, puertos 48989/48984/49010 + RTP, NVENC h264/h265/av1 + zero-copy renderD128) y **paireó OK** sin
  crashear (log: `Succesfully paired`). PERO **NO se pudo confirmar multi-sesión**: el único cliente automatizable local
  (Selene headless Linux `~/.cache/selene-e2e/bin/selene`) está **roto como arnés** — segfaultea en `selene list` y se
  confunde por **colisión mDNS** (descubre Helios:47989 Y Wolf:48989 en la misma caja). Es fallo del CLIENTE de prueba,
  NO de Wolf. **La confirmación de 2 sesiones estables REQUIERE clientes REALES:** el `Selene.app` del Mac (que sí sirve)
  + un 2º dispositivo (Liora), conectando por **IP:puerto explícito (`192.168.1.250:48989`), NO por mDNS** (evita la
  colisión con Helios). Ese es el único test fiel — y donde vive el titileo + el crash histórico.
- **Verificación 2, 2026-07-24 (cliente Selene REPARADO):** se arregló el segfault de `selene list` (PR unjordi/Selene#14
  → develop; el CLI fugaba el `ComputerManager` → race de `~QSettings` en el `DelayedFlushThread` al salir; fix = parentar
  el CM al launcher). Esto **desbloqueó el arnés local**, pero (ver Verificación 3) el arnés local NO es suficiente.
- **🔴 Verificación 3, 2026-07-24 (tarde, luz verde total de Jordi "sin mecate") — CORRIGE la V2:** corrí el demo
  `wolf-deploy/demo-wolf-multisesion.sh` con `APP='Wolf UI'` ×2 (compositor real) Y `Test ball` ×2. Hallazgos DUROS:
  - ✅ **Wolf es ESTABLE:** NO crasheó en 3 corridas seguidas (compositor incluido). `RestartCount=0`, `OOMKilled=false`,
    **0 core dumps nuevos** (sigue en 3). Los 2 contenedores `wolf-ui:main` corrieron CONCURRENTES (wayland-1/wayland-2).
    → **Contradice de frente el "crashea una y otra vez con 2 sesiones"** histórico. Wolf, al menos en idle+launch de 2
    compositores, aguanta.
  - ❌ **El demo da FAIL, pero NO por Wolf:** solo el cliente A recibe video; B nunca ("No video traffic" + tormenta
    `EVP_DecryptFinal_ex failed`, 224 en una corrida). **Pasa IGUAL con `Test ball` ×2** → NO es del compositor: es que
    **DOS clientes Moonlight en el MISMO host** comparten IP (`192.168.1.250`) y pelean los puertos UDP locales de RX
    (47998/48000) → el 2º cliente no recibe. **Artefacto del ARNÉS single-host, no límite de Wolf.**
  - 🔴 **CORRECCIÓN de la V2:** el "demo de 2 sesiones PASÓ, ambos reciben video" era **FALSO/optimista** — hoy NO
    reproduce (Test ball ×2 también falla B). El arnés single-host **nunca** pudo probar 2 sesiones de verdad. Esto ya
    lo anticipaba la nota de arriba (líneas "requiere clientes REALES... por IP:puerto explícito"): el arnés local miente.
- **Verificación 4, 2026-07-24 — TEST FIEL con 2 IPs (método de Jordi: SSH al Mac):** el escenario Jordi+Liora son 2
  máquinas = 2 IPs. Arnés correcto = **local (A, IP LAN de esta máquina — ver [[project_streaming_helios_apollo]]
  global, `~/.cache/selene-e2e/bin/selene`) + MacBook (B, su propia IP LAN) por SSH** (`ssh unjordi@<ip-mac>
  /Applications/Selene.app/Contents/MacOS/Selene stream <ip-local>:48989 '<app>' ...`). El Mac YA está emparejado
  (`config.toml`), decodifica HEVC por VideoToolbox (M4 Pro), y `selene stream` corre por SSH directo (sin
  `launchctl asuser`). Script orquestador efímero en `/tmp` (no persistido).
  **RESULTADO (Test ball ×2, IPs distintas concurrentes):**
  - ✅ **Wolf ENTREGA video a AMBAS sesiones** (a diferencia del mismo-host donde B no recibía nada). Mac B decodificó
    **10 frames HEVC (VideoToolbox, sostenido)**; local A recibió primer paquete y **decodificó 310 líneas** (AV1 vía NVDEC).
  - ✅ **Wolf ESTABLE:** `RestartCount=0`, sin OOM, 0 core dumps nuevos.
  - ⚠️ Local A **terminó a los 16s** ("lack of a successful video frame", `-101`) — **artefacto del arnés headless, NO de Wolf**:
    con `--video-codec auto` Wolf le negoció AV1, A cargó `av1_cuvid` (decode en GPU) pero yo forcé `LIBGL_ALWAYS_SOFTWARE=1`
    sobre Xvfb → decodifica en GPU y **no puede presentar el frame** (render software sin GPU). El Mac (cliente REAL con
    display) no tiene ese problema. Para un arnés local sano: dejar que A renderice por GPU (display real) o forzar decode+render software coherentes.
  - ⚠️ **Anomalía EVP storm — ENTENDIDA, NO CERRADA (corregido 2026-07-25 tras AUDITORÍA experta; antes se marcó
    "RESUELTA = mismo IP" — era MIS-DIAGNÓSTICO):** la tormenta `EVP_DecryptFinal_ex failed` es del **canal de
    CONTROL (ENet)** — `control.hpp:387 decrypt_packet` → `crypto::aes_decrypt_gcm` → `aes.hpp:130`. NO afecta el video
    (server→client, canal aparte), por eso el video siempre fluyó; sí puede tirar input/IDR-requests/TERMINATION.
    **CAUSA RAÍZ REAL (revisada, `control.cpp:97-124`):** NO es "mismo IP" per se. El demux de Wolf casa la sesión primero
    por `enet_secret_payload` (único por sesión, robusto) y SOLO cae al match por IP si el secret-match FALLA. El gatillo
    real es el **fallo de secret-match** + **sesiones stale/huérfanas** acumuladas → la key equivocada se liga al peer →
    GCM tag falla. Empíricamente 2 IPs distintas ×5 = 0 EVP y mismo-IP = 125, pero eso es porque el mismo-IP hace ambiguo
    el FALLBACK, no porque el IP sea la causa. **Corolario: PUEDE recurrir con UNA sola IP** si se acumulan sesiones
    huérfanas (familia de los `Wolf-UI_<sid>` sin reapear). **Mitigación real = REAPING FIABLE de sesiones/contenedores
    antes de cada corrida**, NO "2 IPs". Para Jordi+Liora (2 IPs, estado limpio) no debería dispararse, pero el
    multi-sesión NO está "cerrado".
  - 🔴 **Bug de cripto SERIO de Wolf (upstream, F1 de la auditoría) — NO es trivia:** el canal de control tiene **nonce
    reuse** (IV GCM = array de 16B con solo `iv_data[0]=seq` truncado a 1 byte, `control.hpp:387-395`; y server→cliente
    **siempre** cifra con `IV=0` fijo, `control.cpp:89 "// TODO: seq?"`) + `handle_openssl_error` (`crypto/src/utils.cpp:7-10`)
    **NO lanza** → `decrypt_authenticated` **devuelve el plaintext SIN AUTENTICAR** cuando el tag GCM falla (`aes.hpp:113-135`)
    en vez de descartar. = **canal de control FALSIFICABLE** (un atacante on-path puede recuperar keystream e inyectar input
    o un TERMINATION). Con los puertos de Wolf **abiertos a Internet** era explotable remoto. **ACCIÓN TOMADA 2026-07-25:**
    puertos de Wolf **cerrados a LAN** (ufw `192.168.1.0/24`; ver [[security-findings-2026-06.local]]). **Pendiente:** reportar
    upstream (IV = seq completo 12B conforme a Sunshine; server debe incrementar seq; decrypt debe DESCARTAR en fallo de tag).
  - ✅ **PATH COMPOSITOR `Wolf UI` ×2 (mismo arnés 2-IP, 2026-07-24) — TAMBIÉN PASÓ a nivel Wolf:** los 2 contenedores
    `wolf-ui:main` corrieron concurrentes, **Wolf ESTABLE** (RestartCount=0, 0 dumps nuevos), y el **Mac (cliente real)
    decodificó 10 frames HEVC del compositor** (sostenido). Local A = mismo artefacto headless. EVP storm = 109 (idéntico a
    Test ball → NO es del compositor; es constante del escenario 2-IP). → El "nunca estable con 2 sesiones + compositor"
    **NO se reproduce**. Es el hallazgo que da vuelta al abandono histórico.
  - ✅ **SOAK 60s Wolf UI ×2 (2026-07-25, IPs distintas, ambos legs REALES):** con el cliente local forzado a **H.264
    software** (`--video-codec H.264` — OJO: el CLI exige ese string EXACTO, `h264` da "Invalid video-codec choice") para
    que renderice en Xvfb, **AMBAS sesiones sostuvieron los 60s completos** (local corrió hasta 00:01:04 sin morir; Mac
    actividad continua hasta 00:01:04, 0 "No video traffic"), **2 contenedores `wolf-ui:main` concurrentes**, Wolf `RC=0`,
    `EVP=0`, 0 dumps. (Nota: con `--video-codec auto` el local recibe pero muere ~16s porque Wolf le negocia AV1→NVDEC-GPU
    y el render software headless no presenta el frame — artefacto del arnés, no de Wolf.) También apareció un contenedor
    `WolfFirefox_<lobby>` inesperado (Wolf UI crea un "lobby"; posible auto-launch/estado stale — curiosidad, no afectó estabilidad).
  - **VEREDICTO (matizado por la auditoría 2026-07-25):** Wolf no crasheó en los soaks cortos — el miedo histórico NO se
    reprodujo en 60s, entrega video a 2 IPs simultáneas (compositor incluido). PERO los soaks **nunca ejercitaron el
    escenario que de verdad rompe (#265): la DESCONEXIÓN a media sesión** (teardown del pipeline zero-copy de UNA sesión
    rompe las OTRAS vivas — el maintainer lo confirma, muerde aunque sean 2 IPs), ni se probó `WOLF_USE_ZERO_COPY=FALSE`
    (el deploy corre el TRUE, más arriesgado), ni se analizaron los `backtrace.*.dump` históricos. → Wolf queda **"A PRUEBA"**.
    Lo que FALTA para bill-of-health limpio: (a) **2 clientes REALES con video simultáneo + DESCONEXIÓN a media sesión**
    bajo ZERO_COPY TRUE **y** FALSE (= Jordi+Liora, SU QA); (b) analizar los backtraces preservados; (c) reaping fiable de
    sesiones (ver EVP arriba). Build del cliente local: [[build-selene-linux-headless]].

## 🔍 AUDITORÍA EXPERTA + PLAN REDEFINIDO (2026-07-25)
Jordi pidió una auditoría "modo experto en streaming" para NO arrastrar deuda técnica y redefinir prioridades. 4 agentes:
protocolo NVIDIA a nivel bits ([[nvidia-gamestream-protocol]] + skill `gamestream-protocol`), auditoría de decisiones, e
inventario de fixes de los ABUELOS (Sunshine→Helios [[backport-sunshine-a-helios]], moonlight-qt→Selene
[[backport-moonlightqt-a-selene]]). Autonomía de Jordi para aplicar fixes + armar el plan (regla dura: nada que rompa
nuestros deltas → se PARQUEA, no se fuerza; grants en `autorizaciones-vigentes.local.md`).

**Hallazgos que CORRIGEN decisiones nuestras:**
- **EVP storm mal diagnosticado** (bullet corregido arriba): causa real = secret-match fallthrough + sesiones stale;
  puede recurrir con 1 IP; enterró un bug de cripto SERIO (canal de control falsificable). Wolf: "resuelto" → "a prueba".
- **Cripto de control de Wolf falsificable + puertos a Internet** → **cerrados a LAN el 2026-07-25** (ufw).
- **Pins de `moonlight-common-c` NO alineados:** Helios `c999436`, Selene `ad329b2` (5 detrás, lineal, mismo fork de
  ClassicOldSong). Bajo riesgo HOY, pero LTR #120 lo vuelve ruptura real → alinear a `c999436` (PR en curso).
- **Cosecha de abuelos NUNCA hecha:** Selene 0 commits de moonlight-qt (abuelo VIVO, ~11 meses de fixes sin reclamar);
  Helios solo sigue a Apollo CONGELADO (555 commits/~10 meses detrás de Sunshine, el abuelo vivo con seguridad/deps).
  BUENA noticia: nuestro fix de CVE-2026-32253 es MÁS estricto que el de Sunshine → NO le debemos ese backport.
- **macOS validado por uso** (era "sin validar"); **rebrand a medias** (íconos apollo.* + nombres de gamepad Sunshine).

**PLAN REDEFINIDO (prioridades):**
- **P0 seguridad:** ✅ Wolf cerrado a LAN. ⬜ reportar upstream los bugs de cripto de Wolf (F1).
- **P1 salud de forks (EN CURSO, agentes/PRs):** alinear pin `common-c`→`c999436`; backports Sunshine→Helios (OpenSSL 4.x
  compat `2c59b2e6`, crashes de video `86a25385`/`7ecd0286`, leak `1d9ab7b8`, autodetect GPU híbrida `32100783`+`38a94b3c`
  = ataca el "monitor equivocado" #1543); backports decoder Vulkan/HDR moonlight-qt→Selene. **Parqueando conflictos.**
  Establecer CADENCIA de cosecha de abuelos (re-correr inventarios periódicamente / ante cada CVE). NO backportear el
  callback de la CVE de Sunshine (sería regresión) ni `ecba5c3c` CAP_SYS_ADMIN-drop (Helios NECESITA la cap para KMS).
- **P2 seguridad Helios expuesto (F6):** pins de deps (OpenSSL≥3.5.5, libcurl≥8.12, FFmpeg≥8.1.2, FreeType); auditar
  `confighttp` (CVE-2025-53095 CSRF→cmd-injection, CVE-2024-31220 lectura de archivo unauth).
- **P3 rebrand tail (F8):** nombres de gamepad (`inputtino_gamepad.cpp`, EN CURSO) + **íconos apollo.icns/ico/png/svg →
  Helios** (necesita asset de diseño de Jordi — NO existe ícono Helios aún).
- **P4 el test que DECIDE Wolf (F3/F4/F9, interactivo Jordi+Liora):** 2 clientes reales + Steam + **desconexión a media
  sesión** (trigger #265) bajo ZERO_COPY TRUE y FALSE + analizar backtraces + temp/fps. Wolf "a prueba" hasta esto.
- **P5 condicional:** si Wolf falla el test → promover la frustración #2 (resolución/HDR nativo KDE Wayland,
  [[deploy-streaming-y-resolucion]]) — única feature Helios-own restante y Helios single ya sirve.
- **P6 higiene (F10):** borrar `find_passphrase.py`/`test_otp_hash.cpp`/`test_hash.py`/`AV1_DETECTION_ANALYSIS.md`/`config.log`
  del root de Selene; delegar `OTPPairingManager`→`otpcrypto.h` (quita el `qDebug` del PIN).

**Pendientes menores anotados (no perder):** otras reglas ufw `Anywhere` sin identificar (`48016-48042/tcp`,
`48030-48032/udp`) — investigar a qué proceso pertenecen; salt OTP CSPRNG (`QRandomGenerator::system()` vs `global()`) sin
re-verificar.
