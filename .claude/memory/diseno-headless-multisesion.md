---
name: diseno-headless-multisesion
description: Dirección de diseño para headless multi-sesión en Helios — puertos, topologías, broker+pool. Decisión ABIERTA (2026-07-19)
metadata:
  type: project
---

# Headless multi-sesión Helios — dirección de diseño (ABIERTA, 2026-07-19)

**Visión de Jordi:** que él se conecte a un headless de Steam y su novia a un emulador (o su propia
cuenta de Steam), **a la vez, cada quien en su headless aislado**, sin exponer "8 puertos por usuario".

## Realidad del modelo de puertos (verificado en código)
`src/network.cpp:187` → `map_port(p) = config::sunshine.port + p`. TODOS los puertos (HTTP 47989,
HTTPS, RTSP 48010, video/audio/control/mic UDP) son **offsets de UN puerto base**. O sea: se expone
**un rango contiguo pequeño POR HOST**, y **una sola vez** — nunca "8 por sesión/usuario". La premisa
de "cada headless su set de puertos" solo aplica si cada headless es un HOST separado sin gateway.

## Lo que Apollo YA da (verificado)
- Virtual displays **por-cliente**: `named_cert.always_use_virtual_display` (nvhttp.cpp:254/351),
  `isolated_virtual_display_option` (config.cpp:1210). Un host puede dar a cada cliente su display.
- Tracking de sesiones: `session_id_counter`, `map_id_sess` (nvhttp.cpp:150-152), `find_and_stop_session`.
- **Límite a verificar**: `rtsp.cpp:518` "we currently only support a single pending RTSP session" →
  la concurrencia real de streaming hay que auditarla (¿es solo el pending del handshake o un tope duro?).

## Dos topologías
- **A) Un host, N virtual displays (Apollo-nativo).** Una caja, un rango de puertos, N clientes con su
  display. PROBLEMA para la visión de Jordi: "cada quien SU cuenta de Steam" → mismo OS user = colisión
  de Steam; cuentas distintas necesitan sesiones/entornos aislados. No lo resuelve un solo host multi-display.
- **B) N backends aislados detrás de UN endpoint (broker + pool de puertos).** Cada headless = su
  contenedor/user/VM con su cuenta de Steam + su virtual display. Se expone **UN rango público**; un
  **broker** en el endpoint: (1) empareja/autentica al cliente, (2) elige/levanta el backend aislado,
  (3) asigna un set de puertos **del pool** para esa sesión ACTIVA, (4) relaya los streams. Los puertos
  se liberan al cerrar → concurrencia = tamaño del pool, no "un set por usuario para siempre".
  **Es viable sobre el protocolo Moonlight** porque serverinfo/RTSP le dicen al cliente a qué
  address:port conectarse → el broker controla ese hand-off (como hace GeForce NOW: broker → backend).

## RESOLUCIÓN de la auditoría (agente arquitectura, 2026-07-19)
**Concurrencia real de Apollo — 3 capas:**
- Handshake RTSP: serializado SOLO en el pending del launch (ventana de ms), NO es tope de concurrencia (`rtsp.cpp:492-518`).
- Streaming: **multi-sesión REAL** — puertos globales compartidos + demux por session-id (control `connect_data`, video/audio `ping_payload`; `stream.cpp:490-544,1850-1895`). Dos clientes YA pueden streamear a la vez con displays distintos.
- App+display: **SINGLETON GLOBAL = el cuello de botella real.** `proc::proc` único, `launch` rechaza 2ª app distinta con 400 "An app is already running" (`nvhttp.cpp:1224-1236`); `config::video.output_name`/`configure_display` globales (`process.cpp:327,337`). Los virtual displays por-cliente aíslan PANTALLA dentro de UNA app, no cuentas/entornos.

**Veredicto:** un solo host NO logra "yo con mi Steam, novia con el suyo" (singleton de app + estado global de display + Steam es 1-por-usuario-SO). **Se necesitan backends AISLADOS** (contenedor/usuario/VM por cuenta), cada uno una instancia Helios headless.

**Topología recomendada: B2 — N backends aislados detrás de BROKER + POOL de puertos (relay).**
- Cada backend = Helios sin patchear, con un `config::sunshine.port` base distinto (47989, 48021, 48053…) → rangos disjuntos, **cero cambios de código** (`map_port` ya lo hace, `network.cpp:187`).
- El **hand-off** está en la respuesta HTTP: `serverinfo` (puertos, `nvhttp.cpp:906`) y `launch/resume` → `sessionUrl0=rtsp://<addr>:<port>` (`nvhttp.cpp:1334-1342`). Quien controla ese string controla a dónde va el cliente.
- **Broker** (componente NUEVO, fuera del core Helios): autentica por cert/uuid, elige/levanta el backend, reescribe `sessionUrl0`/puertos, y **relaya UDP/TCP OPACO** (sin descifrar — el cifrado por-sesión gcm_key queda intacto extremo-a-extremo). Pool de puertos externos dimensionado por **sesiones concurrentes ACTIVAS**, no por #backends → cumple "no un set por headless".
- Relay debe ser kernel-level (nftables/eBPF DNAT) por latencia, no user-space.

**Roadmap:** Fase 0 = **CERO código**: 2 contenedores, base-ports 47989/48021, cada uno su Steam, conectar Selene a cada uno directo → prueba aislamiento + concurrencia. Fase 1 = MVP broker (reverse-proxy pairing + reescribe sessionUrl0 + relay port-pool de 2 slots, mapeo cliente→backend estático). Fase 2 = on-demand + pool dinámico. Fase 3 = escala N.

**Riesgos:** latencia del relay; el broker reescribe HTTP de pairing → resolver TLS/certs (passthrough SNI + reescribir XML); Selene cachea IPs de `LocalIP`/`serverinfo` (`nvhttp.cpp:946-958`) → sanear; virtual display headless en Linux (la ruta auditada `process.cpp:236` es `_WIN32` → verificar Xvfb/gamescope en el fork).

## Fase 1 — MVP del broker IMPLEMENTADO (2026-07-19, turno nocturno)
Código en `helios-broker/` (Python asyncio, stdlib only — `go` no está instalado). `demo.sh` = **10/10 PASS**.
- **PROBADO autónomo:** (A) enrutado por identidad `uniqueid → backend` contra 2 backends Helios REALES
  headless (b2:48021 Xvfb:99, b3:48053 Xvfb:100) + reescritura de `ExternalPort/HttpsPort/LocalIP` a los
  públicos del broker (47000/46995), sin filtrar puertos internos; (B) relay UDP por 5-tupla (echo
  round-trip + aislamiento 2 clientes); (C) rewrite de `sessionUrl0` (48042→puerto del slot) + relay RTSP
  TCP conectando al backend vivo. Verificado que HTTP `/serverinfo` se sirve SIN pairing (`nvhttp.cpp:1736-1738`).
- **BLOQUEADO (necesita a Jordi):** pairing real + API HTTPS (mTLS con cert paireado, `nvhttp.cpp:1661-1702`
  → creds del web UI); stream de video end-to-end (Steam/app + NVENC + QA visual); `launch`/`resume` reales.
  La espina del TLS (terminar TLS con cert pineado por Selene + re-emitir al backend) está documentada en
  `helios-broker/README.md` con 2 opciones para Fase 2.
- **Pendiente Fase 2** (en README): liberar slot al cerrar sesión (hoy solo se asigna), on-demand de
  backends, relay kernel-level (nftables/eBPF) por latencia, TLS/pairing real.

## Fase 2 — E2E con el cliente Selene REAL (2026-07-19, turno nocturno)
Código en `helios-broker/` (broker.py Fase 2 + provision.py + udptun.py). Verificado con **Selene.app real
en la Mac** (192.168.1.84) contra un backend Helios headless GESTIONADO por el broker.

**El insight que lo desbloquea (sin creds de Jordi):** el broker LANZA sus backends → controla su config dir.
Por tanto: (1) fija la password admin del web UI en el primer arranque (`POST /api/password`, sin auth);
(2) **inyecta su propio cert de cliente** en `sunshine_state.json`→`root.named_devices[]` del backend, y como
Helios verifica el mTLS por **pinning X509** contra esos PEM (`crypto.cpp cert_chain_t::verify`), el backend
lo acepta como paireado SIN baile de PIN → así el broker termina el TLS del cliente y re-emite al backend con
SU identidad (leg-2); (3) mete el PIN por el backend cuando un cliente pairea (`login`→cookie→`/api/pin`).

**PROBADO E2E (cliente real, evidencia dura):**
- **Pairing automatizado:** el broker relaya `/pair` y mete el PIN → el backend gana el cert del cliente (`SeleneClient` en named_devices).
- **TLS termination + mTLS leg-2:** `serverinfo` HTTPS vía broker → `PairStatus=1`; `applist`+`launch` responden.
- **Rewrite del hand-off:** `serverinfo` (puertos/LocalIP) y `launch` (`sessionUrl0`→puerto del slot) reescritos.
- **Launch real:** el backend lanza la app "Desktop" (captura Xvfb) + crea encoder libx264.
- **RTSP handshake COMPLETO por el broker:** OPTIONS→DESCRIBE(SDP)→SETUP×3→PLAY relayados; `server_port=` de los
  SETUP reescritos backend(48030/31/32)→slot(47109/10/11). Contador del relay: **rtsp(up=2594 down=815) bytes reales**;
  el cliente avanza a "Initializing video/audio/control/input stream".
- **Relay UDP del pool:** datagramas reales entregados backend↔cliente (test directo túnel→slot→backend = control (50,0) bytes; `test_relay.py` 5-tupla).

**Hallazgos clave (código):**
- RTSP cifrado (`rtspenc://`) se activa sólo si el cliente manda `corever>=1` (`nvhttp.cpp:394-402`), NO por
  `lan/wan_encryption_mode`. El broker reescribe `corever→0` → RTSP en claro → puede reescribir `server_port`.
  (El media sigue cifrado por gcm_key; sólo el canal RTSP queda en claro.)
- RTSP es **conexión-por-request**: Moonlight abre un TCP nuevo por cada OPTIONS/DESCRIBE/SETUP/PLAY y Helios
  cierra tras cada respuesta (`RtspConnection.c:374-501`). ⇒ el relay RTSP NO se destruye al cerrar una conexión;
  el slot vive hasta `/cancel`. (Bug encontrado y arreglado: liberar el slot en el cierre de la 1ª conexión RTSP
  mataba el listener antes del DESCRIBE.)

**Lo que NO cerró — media UDP del stream real (acotado, NO es el broker):** tras el RTSP, el cliente falla al
establecer la conexión ENet de control (UDP 47110) y no arranca el video. Causa: el firewall (nft, sólo root)
de ESTA máquina abre sólo los puertos de PROD; el broker se alcanza por **túnel SSH**, con el cliente Moonlight
y el endpoint del túnel UDP en el **mismo loopback de la Mac** → los datagramas de media del cliente no llegan
al socket del túnel (`udptun rx=0`), aunque un emisor de prueba en la misma Mac sí llega (control (50,0) bytes
al backend). Es artefacto de co-ubicar cliente+túnel; **en LAN sin firewall el cliente manda el media UDP directo
a los puertos del slot y los relays (ya probados) lo transportan.**

## Fase 2b — MEDIA UDP REAL CERRADA con Selene Linux nativo, todo localhost (2026-07-19, turno nocturno)
El gap del Mac (ENet de control no conectaba) **quedó CERRADO** compilando el cliente **Selene NATIVO en
Linux** y corriendo el E2E **100% por loopback** (sin túnel, sin firewall — loopback NO está firewalleado).

**Evidencia dura — contadores del relay del broker (`STATS slot=47100`, stream Desktop ~12s):**
- **video (backend→cliente): 4,088,832 bytes** por el relay UDP del broker (H.264/HEVC real)
- **control ENet BIDIRECCIONAL: down 13,261 B / up 4,065 B** → el ENet de control CONECTÓ (lo que fallaba en Mac)
- audio (cliente→backend) 1,080 B ; rtsp up=2597 down=815 ; total ~4.1 MB. Cliente: "Received first video packet after 0 ms".
- Reproducido por `helios-broker/demo-e2e-local.sh` (imprime PASS con los bytes). Snapshot en `helios-broker/run/evidence/`.

**Cómo se logró (todo sin root):**
- **Build Selene Linux** (CachyOS): faltaban SDL2_ttf y `vulkan/vulkan.h` (root). Resueltos a prefix local:
  SDL2_ttf 2.24.0 desde fuente (cmake, VENDORED=OFF) + Vulkan-Headers clonados (libvulkan.so runtime ya está).
  `PKG_CONFIG_PATH` + `CPLUS_INCLUDE_PATH` (el `INCLUDEPATH+=` de qmake top-level NO propaga a subproyectos).
  qmake6 + make. Binario en `~/.cache/selene-e2e/bin/selene` (reporta "Selene 0.6.7").
- **Cliente headless (gotcha):** el probe de decode/render abre Vulkan; NVIDIA-Vulkan **CRASHEA headless**
  (`VK_KHR_surface`/`pl_map_avframe_ex`). Se neutraliza Vulkan (`VK_ICD_FILENAMES` inexistente + `LIBGL_ALWAYS_SOFTWARE=1`)
  → fallback SDL/GL software + `--video-decoder software`. Xvfb propio (:101) + HOME/XDG aislados.
- **Fix del broker (race que localhost destapó):** el cliente manda `clientchallenge` justo tras `getservercert`,
  llegando al backend ANTES de que el broker complete `login→POST /api/pin` (sobre WAN la latencia lo tapaba)
  → `Incorrect PIN`. Solución en `broker.py`: **gate por uniqueid** (`_pin_ready` `threading.Event`) que retiene
  el relay del `clientchallenge` hasta que el PIN está alimentado. Sin tocar Sunshine.

**Sigue para Jordi:** QA visual del picture (ninguna tool autónoma "ve" el video); clientes EXTERNOS (abrir el
firewall nft root para broker 47000/46995 + pool, o cliente en otra máquina LAN); Steam real + NVENC (hoy app
"Desktop" + encoder software); relay kernel-level (nftables/eBPF) por latencia; multi-backend con 1 cert público
(SNI o keypair compartido); timeout de release del slot; rebrand del binario Selene en `develop` (esta rama
chore/ci-triage aún compila `artemis`).

Relacionado: [[forks-helios-selene]], [[issue-triage-2026-06]] (VD Linux PR #1477).

## HALLAZGO CRÍTICO (2026-07-19) — aislamiento de INPUT, no solo de display
En el demo en vivo, el backend headless capturó el Xvfb aislado (:99, display OK) PERO la **inyección de
input llegó al escritorio REAL (:0)** → los periféricos del Mao (vía Selene) controlaron el KDE real de
Jordi y no podía cerrarlo. Causa: Apollo/Sunshine inyecta input vía **uinput** (dispositivos virtuales a
nivel de kernel/sistema), que el compositor de la sesión ACTIVA (KDE en :0) captura — el Xvfb pelón NO
aísla eso. **Conclusión de diseño (refuerza topología B):** un backend headless DEBE aislar también el
input — contenedor con su propio /dev/uinput namespace, o un compositor headless que maneje su input
(gamescope), o una seat/sesión dedicada. Un Xvfb en el host compartido es INSEGURO para producción.
Próximo paso: investigar `src/platform/linux/*input*` de Helios + prototipar aislamiento (sin volver a
inyectar al escritorio real).

## Aislamiento de input — SOLUCIÓN (R&D 2026-07-19, prototipos SEGUROS, sin tocar :0)

### (a) CAUSA RAÍZ confirmada en código — archivo:línea
Helios inyecta TODO el input en Linux vía la librería **`inputtino`** (submódulo
`third-party/inputtino`, es de Games-on-Whales), que crea **dispositivos virtuales uinput** a nivel
de kernel. Cadena verificada:
- `src/input.cpp:1678` → `platf_input = platf::input();` — único backend de input.
- `src/platform/linux/input/inputtino.cpp:24-26` → `platf::input()` devuelve `new input_raw_t()`.
- `src/platform/linux/input/inputtino_common.h:30-51` → `input_raw_t` crea en su ctor
  `inputtino::Mouse::create({.name="Mouse passthrough", .vendor_id=0xBEEF, .product_id=0xDEAD})`
  y `Keyboard::create({.name="Keyboard passthrough", ...})`.
- `third-party/inputtino/src/uinput/mouse.cpp:25-64,90-97` → `create_mouse()` hace
  `libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uidev)` → abre `/dev/uinput`
  y registra un evdev GLOBAL. `move()` (:127) escribe `EV_REL/REL_X` directo al fd; NO hay ningún
  `display_name`, seat ni socket objetivo en toda la ruta de mouse/keyboard.
- **NO existe backend alternativo**: `grep XTEST|XTestFake|XWarpPointer src/` = 0 hits; `cmake/…/linux.cmake:215`
  añade `inputtino` como el único `input` de Linux. (XTEST/libXtst NI SIQUIERA está linkeado.)

**Por qué llegó a :0:** un uinput device nace en `/devices/virtual/input/` **sin tag de seat**. Verificado
en vivo con `udevadm info -n /dev/input/event20` sobre el device real de inputtino: NO trae `TAGS=…:seat:`
ni `ID_SEAT` → libinput lo trata como **seat0 por defecto**. El compositor de la sesión ACTIVA (el KDE
**Wayland** de Jordi en seat0, `wayland-0`) abre libinput sobre seat0 y por tanto **lee ese device**. El
Xvfb `:99` que Helios captura es solo una superficie de PANTALLA X11; no tiene ni idea de dónde viene el
input. Display y uinput son subsistemas ORTOGONALES: capturar `:99` no re-enruta el input, que sigue
yendo al dueño del seat.

Confirmación adicional en vivo: ya había estos 3 devices presentes en baseline (VID/PID de ejemplo, detalle
en [[project_streaming_helios_apollo]], memoria global) → hay un Helios/Apollo corriendo con sus uinput
globales sobre seat0 (no se tocó).

### (b) Tabla comparativa de opciones de aislamiento

| Opción | ¿Aísla INPUT del :0? | ¿Aísla display? | Esfuerzo | Requiere parche a Helios | Veredicto |
|---|---|---|---|---|---|
| **Xvfb pelón** (estado actual) | **NO** — uinput global → seat0/KDE | Sí (X11) | 0 | No | **INSEGURO** — es el bug en vivo |
| **Contenedor** docker/podman (uinput passthrough) | **NO** — uinput NO está namespaced; fuga al host/seat0 (PROBADO) | Sí (si corre su Xvfb/compositor dentro) | Medio | No | **NO sirve** mientras haya compositor en seat0. Solo aísla si el host NO tiene sesión gráfica (modelo Wolf) |
| **gamescope headless** | **NO por sí solo** — inyecta libei/virtual-pointer en `gamescope-0`, pero Helios manda por uinput global que gamescope headless NO lee | Sí (propio `gamescope-0` + Xwayland `:1`) | Medio-alto | **Sí** (Helios→libei) | Buen destino, pero NO cierra el gap sin patch de input |
| **Seat/sesión dedicada** (udev tag + compositor en seat propio) | **SÍ** — regla udev quita los devices de inputtino de seat0 | Con su compositor | Medio-alto (root: udev + seat + VT/headless) | **No** (solo runtime/udev) | Viable SIN tocar código; wiring de seat/VT es la complejidad |
| **XTEST → Xvfb** (parche) | **SÍ** — XTEST es POR-DISPLAY; inyecta solo en el `:99` capturado (PROBADO) | Sí (el mismo Xvfb) | Bajo-medio (parche acotado) | **Sí** (nuevo backend de input) | **RECOMENDADO** — encaja con el headless-Xvfb que el broker YA usa |

### (c) Prototipos ejecutados y evidencia (NADA tocó :0)
Scripts/binario en `scratchpad/` (`uinput_probe.c` — crea un uinput device que **EMITE CERO eventos**,
solo para ver dónde aterriza; jamás inyecta movimiento/click).

1. **uinput no tiene seat-targeting (host).** La sonda creó `Mouse passthrough` (VID beef/PID dead) →
   apareció en `/devices/virtual/input/input280` sin tag de seat, igual que los reales. Confirma la
   causa raíz sin inyectar nada.
2. **Contenedor NO aísla uinput (docker, decisivo).**
   - `docker run ubuntu:24.04` SIN `--device /dev/uinput` → `/dev/uinput` **AUSENTE** en el container
     (inputtino no podría ni crear el device: cero input, cero fuga).
   - `docker run --device /dev/uinput …` creando `DOCKER-LEAK-TEST` (cero eventos) → el device
     **APARECIÓ en el HOST** (`/proc/bus/input/devices`, `input281`, `/devices/virtual/input/`). ⇒ uinput
     **NO está namespaced**; con el device pasado, fuga al host/seat0. El container solo "aísla" negándole
     el input por completo.
3. **XTEST SÍ aísla (Xvfb :99, decisivo).** `Xvfb :99` aislado + `DISPLAY=:99 xdotool mousemove 640 360`
   + `click 1` → puntero de `:99` = (640,360) con click aterrizado; puntero de `:0` = (0,0) **antes y
   después, intacto**. Screenshot: `scratchpad/xtest-iso-99.png`. XTEST viaja por el protocolo X del
   display objetivo → físicamente incapaz de tocar `:0`.
4. **gamescope headless arranca aislado.** `gamescope --backend headless` → compositor en `gamescope-0`
   + su propio Xwayland `:1` (NO toca `wayland-0` de KDE) y loguea *"Successfully initialized libei for
   input emulation"* → tiene su propia ruta de input, pero hay que ALIMENTÁRSELA (Helios no habla libei).

### (d) Plan / solución recomendada
**Recomendado (menor esfuerzo, encaja con el headless-Xvfb actual del broker): parchear Helios con un
backend de input XTEST por-display.**
- **Diff conceptual:** hoy `platf::input()` (`inputtino.cpp:24`) es el único backend. Añadir un backend
  `x11_input` (compilado en Linux) que en su ctor haga `XOpenDisplay(display_name)` sobre el MISMO display
  que captura x11grab (`x11grab.cpp:388,401` ya recibe `display_name`) y traduzca `move/button/keyboard_update`
  a `XTestFakeRelativeMotionEvent` / `XTestFakeButtonEvent` / `XTestFakeKeyEvent`. Seleccionable por
  config (p. ej. `input_backend = xtest|uinput`, default uinput para no romper el escritorio local).
  Linkear `libXtst` (no está en `cmake/`). Limitación honesta: XTEST solo aplica a displays **X11/Xvfb**
  (no gamepad, no Wayland nativo) — para el headless-Xvfb del broker es justo lo que hay.
- **Alternativa SIN código (runtime/udev):** regla udev tageando los devices de inputtino a un seat
  dedicado (atributos matcheables verificados: `ATTRS{id/vendor}=="beef"`, `ATTRS{id/product}=="dead"`,
  `ATTRS{name}=="Mouse passthrough"`), p. ej.:
  `SUBSYSTEM=="input", ATTRS{id/vendor}=="beef", ATTRS{id/product}=="dead", ENV{ID_SEAT}="seat-helios", TAG+="seat"`
  + correr el compositor headless en `seat-helios`. Los saca de seat0/KDE sin tocar el binario. **Requiere
  root** (regla en `/etc/udev/rules.d` + `loginctl`/VT) → **paso PARA HACER CON JORDI**, no se ejecutó (la
  regla dura prohíbe tocar seat0/root a ciegas). Riesgo: si la regla matchea por VID/PID genérico beef/dead
  podría afectar a otra instancia de Apollo en la misma caja.

**NO se abrió PR de código:** el backend XTEST es una feature real que exige compilar con gcc-14 + QA visual
de stream con Jordi (no se declara "listo" a ciegas). Queda como propuesta acotada. El workspace HeliosSelene
es NO-git; esta doc es el entregable durable.

**Pendiente para Jordi (backlog):**
- [XTEST] Implementar backend `x11_input` + `libXtst` en `cmake/…/linux.cmake`, config `input_backend`, rama
  `feat/xtest-input-headless` → PR a `unjordi/Helios develop`. QA: stream a Xvfb aislado confirmando input SOLO ahí.
- [udev-seat] Probar la regla de seat dedicado con root (udev + seat + compositor) — evaluar vs XTEST.
- [modelo Wolf] Si se va a contenedores: el host del backend NO debe tener compositor en seat0 (o los uinput
  fugan) — decidir si el backend vive en una caja/VM headless separada.
- Neutralizar el riesgo de que la regla udev / el aislamiento afecte al Apollo de PROD que corre en la misma máquina.

## DECISIÓN (2026-07-19): aislamiento de input = SEAT DEDICADO
Jordi eligió el enfoque de **seat dedicado** (sobre XTEST) porque aísla TODO el input incluido **gamepad**
(los juegos lo necesitan) y **sin cambios de código** en Helios. Mecanismo: regla udev que taggea los
devices virtuales de inputtino (vendor 0xBEEF / product 0xDEAD / name "Mouse passthrough" + keyboard +
gamepad) a un seat propio (ej. `seat-helios`), de modo que libinput NO los asigne a seat0 (el KDE de
Jordi). Necesita root de Jordi para instalar la regla + wiring del seat. Pendiente: diseñar runbook +
udev rule + verificación SEGURA (sin arriesgar :0). XTEST queda como alternativa/quick-win descartada por ahora.

## DISEÑO seat dedicado + runbook (R&D 2026-07-19) — VEREDICTO: dos bloqueos duros

> Investigación de código + inspección READ-ONLY en vivo (sin root, sin tocar :0/PROD). El seat dedicado
> **NO cumple las dos premisas que lo hicieron ganar** ("sin cambios de código" y "simple"): choca con dos
> problemas duros e independientes. Se documenta el diseño completo igual, con runbook reversible, PERO el
> veredicto honesto es que el seat puro **desactiva el secuestro de :0 pero NO entrega el input al juego**
> sin montar una pila pesada (vkms + compositor con DRM master). Fallback recomendado al final.

### (a) Identificadores CONFIRMADOS (archivo:línea + verificación en vivo)
Todos los devices se crean con `libevdev_uinput_create_from_device(..., LIBEVDEV_UINPUT_OPEN_MANAGED, ...)`
→ abren `/dev/uinput`, bus **USB (0003)**, nodo `root:input 0660`. Definición de identidad:

| Device | name (`ATTRS{name}`) | VID | PID | ver | Fuente |
|---|---|---|---|---|---|
| Mouse (rel) | `Mouse passthrough` | 0xBEEF | 0xDEAD | 0x111 | `src/platform/linux/input/inputtino_common.h:32-37`; lib `third-party/inputtino/src/uinput/mouse.cpp:25-64` |
| Mouse (abs) | `Mouse passthrough (absolute)` | 0xBEEF | 0xDEAD | 0x111 | `mouse.cpp:66-97` — **registra un nodo `js0`** (ejes ABS → parece joystick) |
| Keyboard | `Keyboard passthrough` | 0xBEEF | 0xDEAD | 0x111 | `inputtino_common.h:38-43`; `uinput/keyboard.cpp:27-40` |
| Touch | `Touch passthrough` | 0xBEEF | 0xDEAD | 0x111 | `inputtino_common.h:68-73`; `uinput/touchscreen.cpp:28-32` |
| Pen | `Pen passthrough` | 0xBEEF | 0xDEAD | 0x111 | `inputtino_common.h:74-79`; `uinput/pentablet.cpp` |
| Gamepad Xbox | `Sunshine X-Box One (virtual) pad` | **0x045E** | **0x02EA** | 0x0FFF | `src/platform/linux/input/inputtino_gamepad.cpp:30-34`; `uinput/joypad_xbox.cpp:23-70` |
| Gamepad Switch | `Sunshine Nintendo (virtual) pad` | **0x057E** | **0x2009** | — | `inputtino_gamepad.cpp:37-41`; `uinput/joypad_nintendo.cpp` |
| Gamepad PS5 | `Sunshine PS5 (virtual) pad` | **0x054C** | **0x0CE6** | 0x8111 | `inputtino_gamepad.cpp:45-53`; creado por **uhid** `src/uhid/joypad_ps5.cpp:246-257` (bus BLUETOOTH/USB) → `hidraw` + nodos evdev hijos |

Verificado EN VIVO (read-only, PROD corriendo) en `/proc/bus/input/devices` y `udevadm info`: los 3 devices
`Mouse passthrough` / `Mouse passthrough (absolute)` (**js0**) / `Keyboard passthrough` con
`ATTRS{id/vendor}=="beef"`, `ATTRS{id/product}=="dead"`, `ATTRS{id/version}=="0111"`, `ATTRS{phys}==""`,
sin `ID_SEAT` (→ seat0) — confirma que caen en seat0 por default. (Instancia/PID/binario exactos de esa
verificación en vivo son solo referencia histórica de diagnóstico — detalle en
[[project_streaming_helios_apollo]], memoria global — no asumir que persisten.)

**⚠️ Caveat de identificadores (crítico para la regla):**
- Mouse/keyboard/touch/pen comparten **0xBEEF/0xDEAD** (VID/PID basura que NINGÚN hardware real usa) →
  matcheables por VID/PID con seguridad.
- **Los gamepads usan VID/PID REALES de Microsoft/Nintendo/Sony.** Matchear gamepads por VID/PID
  **atraparía los controles físicos reales de Jordi.** Hay que matchearlos por `ATTRS{name}=="Sunshine
  * (virtual) pad"`, NUNCA por VID/PID.
- Los nombres siguen diciendo **"Sunshine"** (el fork NO rebrandeó estos strings). No hay knob de config
  para el nombre del device (grep en `config.cpp/config.h` = 0 hits): el nombre está hardcodeado.

### (b) BLOCKER A — PROD y headless son INDISTINGUIBLES para udev
PROD (apollo.service / el sunshine de dev) y un 2º backend headless son **el MISMO binario** → producen
devices con firma **byte-idéntica** (mismos name/VID/PID/version). Y un uinput MANAGED **no lleva ningún
atributo del creador**: el nodo es `root:input` sin importar quién lo creó, y el `udevadm info
--attribute-walk` NO expone uid/pid/cgroup/proceso — solo capabilities, id/* y name. udev **no tiene con
qué separar** las dos instancias.
⇒ Una regla que taggee `beef/dead` a `seat-helios` **también le arranca el input a PROD de seat0** →
rompe PROD (justo el escenario que la regla de seguridad prohíbe).

**Únicas salidas al Blocker A:**
1. **Dar al headless una identidad DISTINTA** (name y/o VID/PID propios) → parche mínimo (4-8 líneas) a
   `inputtino_common.h` (+ nombres de `inputtino_gamepad.cpp`), o mejor un knob `env/config` que Helios
   pase a `DeviceDefinition`. Entonces la regla matchea SOLO el headless y PROD queda intacto. **Esto
   CONTRADICE la premisa "sin cambios de código" que hizo ganar al seat sobre XTEST.**
2. Correr el headless **solo cuando PROD esté apagado** (frágil, no concurrente — mata la visión multi-sesión).
3. Namespace/usuario/contenedor: NO ayuda — ya probado (2026-07-19) que uinput NO está namespaced; el
   device aflora al host con la misma firma sin importar el usuario/namespace creador.

### (c) BLOCKER B — un seat headless necesita DRM master + un compositor que LEA evdev
- **logind solo considera "gráfico" un seat que tiene un `master-of-seat` (device DRM).** En vivo:
  `loginctl list-seats` = solo `seat0`; `seat0` tiene el master `drm:card1` (NVIDIA). Un `seat-helios`
  nuevo **no tiene GPU** → no es usable para una sesión gráfica salvo que le des un DRM master. Sin
  hardware, eso obliga a **`vkms`** (Virtual Kernel Mode Setting, DRM virtual por software) tagueado a
  `seat-helios` — camino exótico y poco probado en multi-seat.
- **El routing por seat solo decide QUÉ compositor lee esos evdev.** Y aquí el mismatch de fondo: los
  compositores fáciles de correr headless **NO leen evdev**:
  - **Xvfb** (lo que usa el broker HOY) no tiene drivers evdev — solo acepta input por XTEST/su protocolo.
  - **gamescope --backend headless** usa **libei** (probado 2026-07-19), no evdev.
  - **weston --backend=headless** no toma input de evdev.
  Los que SÍ leen libinput/evdev de un seat (`kwin_wayland --drm`, `weston` backend drm) **exigen seat +
  DRM master**.
  ⇒ Taguear los devices a `seat-helios` **quita el leak de seat0 (desactiva el secuestro del KDE de
  Jordi) pero el input NO llega a ningún lado** con Xvfb — cae en el vacío. Para que el input LLEGUE al
  juego hay que montar la pila completa: `vkms` + sesión en `seat-helios` + `kwin_wayland --drm` sobre el
  card de vkms leyendo esos evdev, y que Helios capture la salida de ESE compositor (KMS/Xwayland) y
  codifique por NVENC en card1 (el encode es agnóstico del seat). Es una arquitectura pesada, frágil y no
  probada — y borra el atractivo "simple/sin código" del seat.

### (d) Diseño de la regla udev + wiring (como se pidió, con los caveats de arriba)
Archivo `/etc/udev/rules.d/61-helios-seat.rules` (el `61-` corre ANTES de `71-seat.rules`, que lee
`ID_SEAT` y añade el tag `seat`; por eso NO usar 72-):
```
ACTION=="remove", GOTO="helios_seat_end"
SUBSYSTEM!="input", GOTO="helios_seat_end"

# mouse/keyboard/touch/pen — VID/PID basura, seguros. (Ver BLOCKER A: si PROD corre a la vez,
# ESTO TAMBIÉN taggea PROD. Solo seguro si el headless usa identidad distinta —name/VID propios—
# y aquí matcheas ESA identidad, no beef/dead.)
ATTRS{id/vendor}=="beef", ATTRS{id/product}=="dead", ENV{ID_SEAT}="seat-helios", TAG+="seat"

# gamepads — por NAME (su VID/PID son IDs reales MS/Nintendo/Sony → jamás por VID/PID)
ATTRS{name}=="Sunshine X-Box One (virtual) pad", ENV{ID_SEAT}="seat-helios", TAG+="seat"
ATTRS{name}=="Sunshine Nintendo (virtual) pad",  ENV{ID_SEAT}="seat-helios", TAG+="seat"
ATTRS{name}=="Sunshine PS5 (virtual) pad",       ENV{ID_SEAT}="seat-helios", TAG+="seat"

LABEL="helios_seat_end"
```
**Con el parche de identidad (recomendado):** cambiar el match a la firma propia del headless, p. ej.
`ATTRS{name}=="Helios headless mouse"` (o un VID reservado tipo `0xF00D`) → matchea SOLO el headless,
PROD 0xBEEF/0xDEAD sigue en seat0 intacto.

DRM master del seat (necesario por Blocker B):
```
# /etc/udev/rules.d/61-helios-seat.rules  (añadir)
SUBSYSTEM=="drm", KERNEL=="card*", DRIVERS=="vkms", ENV{ID_SEAT}="seat-helios", TAG+="seat", TAG+="master-of-seat"
```
(vkms cuelga de `/sys/devices/platform/vkms`; si `DRIVERS=="vkms"` no matchea, usar `DEVPATH=="*platform/vkms*"`.)

Wiring del seat: `modprobe vkms` → aparece `cardN` → udev lo taggea master-of-seat+seat-helios →
`seat-helios` pasa a `CanGraphical=yes` → una **sesión** en ese seat (systemd/pam con `XDG_SEAT=seat-helios`)
corre `kwin_wayland --drm`/`weston` sobre el card de vkms → su libinput abre SOLO los evdev de seat-helios
→ el juego (lanzado dentro de esa sesión) recibe el input → Helios captura esa salida.

### (e) Runbook REVERSIBLE para Jordi (root; PROD apagado o headless con identidad distinta)
> Precondición de seguridad: hacerlo con **PROD detenido** (`systemctl stop apollo.service`) O con el
> parche de identidad ya aplicado. Nunca con la regla `beef/dead` genérica y PROD vivo.
```
# 1. Instalar regla (revisar primero)
sudo install -m 0644 61-helios-seat.rules /etc/udev/rules.d/61-helios-seat.rules
sudo udevadm control --reload

# 2. Cargar DRM virtual y crear el seat gráfico
sudo modprobe vkms                       # rollback: sudo modprobe -r vkms
# retriggear los devices ya existentes para que tomen el tag (NO conecta ni inyecta nada):
sudo udevadm trigger --subsystem-match=input --subsystem-match=drm --action=change

# 3. Verificación SEGURA (NO inyectar aún) — ver §(f)

# 4. (solo si §f pasa) atar sesión y compositor al seat — requiere greeter/systemd-run con XDG_SEAT=seat-helios
#    p.ej.: sudo systemd-run -p User=helios -E XDG_SEAT=seat-helios kwin_wayland --drm ...
```
**Rollback total (reversible):**
```
sudo rm -f /etc/udev/rules.d/61-helios-seat.rules
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=input --subsystem-match=drm --action=change
sudo modprobe -r vkms
```
(Los devices vuelven a seat0 al re-triggear sin la regla; nada persiste.)

### (f) Verificación SEGURA de aislamiento (SIN arriesgar :0, SIN inyectar)
Todo lo siguiente es READ-ONLY y NO mueve el puntero de :0:
```
# ¿el device quedó en seat-helios y NO en seat0?
udevadm info -q property -n /dev/input/eventN | grep ID_SEAT      # → seat-helios
loginctl seat-status seat-helios                                  # lista los Mouse/Keyboard passthrough
loginctl seat-status seat0 | grep -i passthrough                  # → VACÍO (ya no están en seat0)
# ¿el compositor de seat0 (KDE) ya NO los ve?
sudo libinput list-devices | grep -A2 passthrough                 # bajo seat0 no deben aparecer
```
Solo si TODO lo anterior confirma que los devices NO están en seat0, y con PROD detenido, se podría
probar una inyección observando desde SSH que el puntero de :0 no se mueve (`DISPLAY=:0 xdotool
getmouselocation`) — pero esto ya es opcional; el gate real es el `seat-status`/`libinput` de arriba.

### (g) VEREDICTO honesto + FALLBACK
El seat dedicado **falla sus dos premisas**: (A) para no romper PROD necesita un parche de identidad →
NO es "sin código"; (B) para ENTREGAR el input al juego necesita vkms + compositor DRM en el seat → NO es
"simple", y es una pila frágil/no probada. Lo único que el seat logra barato es la **mitad de seguridad**
(sacar los devices de seat0 y matar el secuestro del KDE), pero deja el input en el vacío con el Xvfb del
broker actual.

**Fallbacks, de más limpio a más pragmático:**
1. **Modelo Wolf (el más robusto):** el backend headless vive en una **caja/VM SIN sesión gráfica en
   seat0**. Sin compositor en seat0, los uinput globales no los secuestra nadie; el compositor del propio
   headless (kwin/gamescope con su seat) los consume. Cierra A y B de raíz porque no hay PROD-en-seat0 que
   colisione. Costo: separar el backend de la máquina-escritorio de Jordi.
2. **XTEST-a-Xvfb (el mejor fit para el broker actual):** el backend de input `x11_input` por-display que
   ya estaba propuesto (y fue descartado). Encaja EXACTO con el Xvfb del broker: inyecta por el protocolo
   X del display capturado → físicamente incapaz de tocar :0, sin uinput, sin seat, sin vkms, sin problema
   de discriminador PROD. **Gap honesto: NO cubre gamepad ni Wayland nativo** (era la razón por la que
   Jordi lo descartó). Para cubrir gamepad ahí, combinarlo con inputtino-uinput + identidad distinta +
   regla `LIBINPUT_IGNORE_DEVICE=1`/seat SOLO para el gamepad.
3. **Híbrido acotado (si se quiere seat igual):** aplicar el parche de identidad al headless (resuelve A) y
   usar la regla udev con `ENV{LIBINPUT_IGNORE_DEVICE}="1"` en vez de seat completo → los compositores de
   seat0 IGNORAN esos devices (mata el secuestro) sin montar vkms. Sigue sin entregar el input por Xvfb
   (mismo Blocker B), así que solo sirve combinado con un compositor que sí lea evdev.

**Recomendación:** para el objetivo real de Jordi (aislar TODO incl. gamepad, concurrente con PROD en la
misma caja) el camino con mejor relación robustez/esfuerzo es el **modelo Wolf (fallback 1)**: backend
headless en VM/contenedor en una caja sin seat0 gráfico. Si el backend DEBE convivir con el KDE de Jordi
en la misma máquina, el seat dedicado es viable pero exige **parche de identidad + vkms + compositor DRM**
— documentarlo como tal, no como "runtime sin código".

## Modelo Wolf — R&D de arquitectura (2026-07-19)

> Investigación de docs + código de games-on-whales (URLs al final). Inspección local READ-ONLY (sin
> Docker corrido, sin root, sin tocar :0/PROD). **Hallazgo que CORRIGE el análisis del seat de arriba:**
> Wolf **NO** enruta mouse/teclado por uinput — los **inyecta programáticamente en su propio compositor**,
> así que NO fugan a seat0 por construcción. Solo el **gamepad** crea un uinput global, y su mitigación
> (regla udev a un "seat de estacionamiento") es MUCHO más ligera que el vkms+DRM que temía el análisis del
> seat (el contenedor lee el nodo evdev DIRECTO, no vía un seat gráfico). Ver §(b).

### (a) Arquitectura de Wolf
- **Es un servidor Moonlight PROPIO** (C++/Rust), NO es Sunshine/Apollo. Implementa pairing HTTP/S, RTSP,
  RTP video (H.264/HEVC), RTP audio (Opus), control por ENet — su propio stack. ⇒ **Selene (Moonlight Qt)
  se conecta a Wolf igual que a Helios**: ambos hablan Moonlight, cliente COMPATIBLE con los dos.
- **Contenedor por app-launch.** Wolf corre como contenedor con `--network=host` + `docker.sock` montado y
  **lanza contenedores hermanos** (uno por instancia de app; "dedicated folder structure for every app
  instance"). Diseñado para **múltiples sesiones simultáneas**, cada una su app. Estado por-cliente
  (paired_clients, overrides de control) en su `config.toml`.
- **Compositor = `gst-wayland-display`** (micro-compositor Wayland basado en **Smithay**, Rust, expuesto
  como **plugin de GStreamer** + C API). Expone el **framebuffer crudo** directo al pipeline de encode
  (zero-copy vía DRM/EGL). **NO soporta XWayland**: las apps que lo necesitan (Steam) corren **gamescope
  DENTRO del contenedor como cliente Wayland** de Wolf, y gamescope les da XWayland. Capas:
  Wolf-Wayland → gamescope (cliente Wayland) → XWayland → app X11.
- **GPU NVIDIA:** 3 vías — (1) **nvidia-container-toolkit** ≥1.16.0 + driver ≥530 con `--gpus=all` +
  `NVIDIA_DRIVER_CAPABILITIES=all` + `NVIDIA_VISIBLE_DEVICES=all`; (2) volumen manual del driver; (3)
  montar `/dev/nvidia*` directo. `WOLF_RENDER_NODE` elige el render node; recomiendan render Wayland +
  encode GStreamer en la **misma** GPU (zero-copy). Encode HW por CUDA/NVENC/QSV/VAAPI.

### (b) CÓMO WOLF AÍSLA EL INPUT — el punto que nos trajo aquí (DOS mecanismos distintos)
**1. Mouse/teclado/touch → inyección PROGRAMÁTICA en el compositor (NO uinput, NO seat, NO fuga).**
El plugin `gst-wayland-display` acepta eventos por **structure messages de GStreamer** (ej.
`gst_structure_new("MouseMoveRelative", "pointer_x", …)`), que entran directo al compositor Smithay y de
ahí a los clientes Wayland — **jamás crea `/dev/uinput`, jamás toca un seat, jamás es visible para KDE**.
Confirmado por doc de Wolf: *"You can run Wolf in a very unprivileged setting without uinput/uhid,
unfortunately this means that you'll be restricted to only using mouse and keyboard"* → sin uinput = solo
mouse+teclado, porque ESOS van por el compositor. **ESTA es la diferencia de raíz con Helios:** Helios
manda mouse/teclado por inputtino→uinput GLOBAL (el bug del secuestro de KDE); Wolf los mete a su
compositor. En una caja con KDE en seat0, el mouse/teclado de Wolf **NO fuga** (a diferencia de Helios hoy).

**2. Gamepad (+ uhid: giroscopio/rumble PS5) y apps que escanean `/dev/input` → SÍ usan inputtino/uinput
GLOBAL (visible en el host).** Wolf usa la MISMA librería `inputtino` que Helios; para gamepad no hay
escapatoria del uinput. Mitigaciones que Wolf **sí trae**:
- **Reglas udev que Wolf shippea:** dan acceso al grupo `input` (`uaccess`) y **mueven los joypads
  virtuales a un seat dedicado `seat9`** (`ENV{ID_SEAT}="seat9"`) → el compositor de seat0 (KDE) **no los
  agarra**. (`uinput`/`uhid` con `MODE=0660 GROUP=input`.)
- **`fake-udev`:** Wolf hace `mknod` del device DENTRO del contenedor objetivo (`docker exec … mknod
  /dev/input/<n> c <major>:<minor>`) y **emite el evento udev (`NETLINK_KOBJECT_UEVENT`) DENTRO del
  network-namespace del contenedor** + escribe `/run/udev/data/` → la app lo detecta **sin que el evento
  fugue al host**. El contenedor lee el device por el **nodo mknod'd DIRECTO** (no por libinput de un seat).

**CORRECCIÓN a los Blockers del análisis del seat (arriba):**
- **Blocker B NO aplica a Wolf.** `seat9` es solo un "seat de ESTACIONAMIENTO" para que seat0/KDE no
  agarre el gamepad; **el contenedor consume el device leyendo el nodo evdev directo**, no por un seat
  gráfico. ⇒ `seat9` **NO necesita vkms, ni DRM master, ni un compositor logueado**. La pila pesada que
  temíamos era porque asumíamos que el consumidor era un compositor atado al seat; Wolf lo evita.
- **Blocker A (PROD vs headless indistinguibles) — parcial.** Solo aplica al **gamepad** (mouse/teclado ya
  no crean uinput → cero colisión ahí). Riesgo abierto: Wolf usa inputtino igual que Helios → **verificar
  si los NOMBRES de device del gamepad de Wolf chocan con los de Helios PROD** ("Sunshine … (virtual) pad")
  si ambos corren en la misma caja; la regla `seat9` por-nombre podría matchear ambos. **INCÓGNITA por
  confirmar en el código de Wolf.**

### (c) ¿Viable en la máquina de Jordi (KDE Wayland + RTX 5070 Ti en seat0)? — SÍ, con matices
- **NO exige host headless.** La issue #7 "go fully headless" era para **eliminar la dependencia de
  uinput/udev** (de ahí salió el modo unprivileged solo-mouse+teclado), no un requisito de correr sin
  escritorio. Wolf corre como contenedor **junto a KDE**.
- **Mouse/teclado:** seguros por construcción (compositor-inyectado, sin uinput). **Gamepad:** necesita la
  **regla udev `seat9`** que Wolf trae (root, one-time, LIGERA — sin vkms). 
- **GPU:** hay que **instalar `nvidia-container-toolkit`** (NO está en la caja; verificado read-only).
  Driver/GPU exactos en [[project_streaming_helios_apollo]] (memoria global) — cumplen de sobra el mínimo
  que Wolf pide → usar un toolkit reciente. Requiere root → **con Jordi, no auto-instalar.**
- **Privilegios del contenedor Wolf** (no exigen matar KDE): `--network=host`, `docker.sock`, `/dev`,
  `/run/udev`, `/dev/uinput`+`/dev/uhid`, `--device-cgroup-rule "c 13:* rmw"`.
- **Caveats/incógnitas honestas:** (1) **contención de GPU** — encode de Wolf + juegos en la misma 5070 Ti
  mientras KDE también la usa; (2) `--network=host` con puerto Moonlight default **47989 COLISIONA con el
  Helios PROD** (mismo default) → correr Wolf en otro puerto base o parar PROD en la prueba; (3) nombres de
  gamepad Wolf vs Helios (Blocker A parcial, arriba); (4) audio (PipeWire/Pulse) por-contenedor.

### (d) Ruta de adopción — A (usar Wolf) vs B (adaptar Helios)
| | **A: usar Wolf tal cual** como backend headless | **B: adaptar Helios** (gamescope + input ruteado estilo-Wolf) |
|---|---|---|
| Esfuerzo | **BAJO-MEDIO** — Wolf ya resuelve el problema | **ALTO** — es re-implementar Wolf dentro de Apollo |
| Aísla input (el bug) | **SÍ, gratis** (compositor-inyectado + seat9 gamepad) | Hay que re-plomar todo el input de Helios (hoy uinput global) |
| Multi-sesión / contenedor-por-app / VD on-demand / GPU share | **Ya hecho** en Wolf | Construir desde cero |
| Cliente Selene | **Compatible** (Moonlight) | Compatible (Moonlight) |
| Costo | Codebase DISTINTO (C++/Rust/Smithay/GStreamer); features/config de Helios NO migran; mantener 2 servers o Wolf=backend + Helios=host-con-monitor | Mantiene 1 codebase/feature-set, pero duplica años de trabajo de Wolf justo en la parte más difícil (input) |

**RECOMENDACIÓN: Path A.** Usar **Wolf como el backend headless multi-sesión aislado**; dejar
**Helios/Apollo para el caso "host con monitor real"/local**. Selene habla con ambos (Moonlight). Resuelve
el leak de input SIN parchear Helios. (Opción intermedia — adoptar solo `gst-wayland-display` como
capture+input de un modo headless de Helios — cuesta casi lo mismo que Path B; descartada de entrada.)

### (e) Cómo encaja el broker+pool (`helios-broker/`)
- **Dentro de UNA caja, Wolf hace REDUNDANTE el broker+pool:** un solo Wolf ya es el pool + el aislamiento
  (multi-cliente, contenedor-por-app, multiplexa Moonlight sobre `--network=host`). No necesitas
  broker para "N sesiones detrás de un endpoint" en una máquina.
- **El broker sigue sumando en scale-out / mixto:** (1) **varias cajas/VM Wolf** (más allá de la GPU de una
  máquina) → el broker es la puerta de entrada que **enruta por identidad** entre instancias Wolf, reescribe
  `sessionUrl0`/puertos y **relaya** — justo lo que ya prototipamos; (2) **mezclar backends Wolf + Helios**
  tras un endpoint; (3) el TLS-termination + hand-off que ya construimos aplica igual al Moonlight de Wolf.
- **Reuso concreto del código:** identity-routing + rewrite de `sessionUrl0` + relay UDP son reutilizables
  para el caso cross-instancia. Lo que se **cae** es el port-pool por-sesión dentro de una caja (Wolf ya
  multiplexa). Incógnita: modelo de estado persistente por-cuenta-Steam (Wolf lo cubre con folder por-app +
  volúmenes; confirmar).

### (f) Próximos pasos concretos (para hacer CON Jordi — nada auto-ejecutado)
1. **Leer el código de Wolf** para (a) confirmar los NOMBRES de device del gamepad (choque con Helios PROD,
   Blocker A) y (b) el modelo `config.toml` de apps/sesiones/persistencia por-usuario.
2. **Instalar `nvidia-container-toolkit`** en CachyOS (root; AUR) + verificar con un contenedor CUDA. Diferido.
3. **Resolver colisión de puerto:** Wolf default 47989 = Helios PROD → correr Wolf en otro base-port o parar PROD.
4. **Trial (diferido, con Jordi):** correr Wolf como contenedor, parear Selene, lanzar app de prueba y
   **VERIFICAR que mouse/teclado NO tocan el KDE** (el bug en vivo) + gamepad por la regla `seat9`. Read-only
   hasta que Jordi apruebe correr contenedores.
5. **Decidir rol del broker:** 1 caja → Wolf solo; multi-caja/mixto → broker delante de N Wolf (reusar
   identity-routing + hand-off del `helios-broker`, soltar el port-pool por-sesión).

## Despliegue de Wolf PREPARADO (2026-07-19) — config + runbook, sin ejecutar nada

> Preparacion read-only (sin root, sin Docker corrido, sin tocar :0/PROD). Estudio del repo
> clonado `~/code/ajenos/wolf` (docker/, docs adoc, 85-wolf.rules) + inspeccion en vivo.
> **Entregables en `~/code/HeliosSelene/wolf-deploy/`**: `docker-compose.yml`, `85-wolf.rules`
> (copia + notas), `RUNBOOK.md` (runbook root reversible paso a paso).

### Puertos (COLISION con PROD resuelta)
Wolf expone 6 puertos, todos configurables por ENV (fuente: `docs/.../user/pages/quickstart.adoc`,
seccion "Which ports are used by Wolf?"). Defaults COLISIONAN con PROD en **HTTP 47989** y
**RTSP 48010**. Solucion = bloque **+1000** (conserva offsets):

| Servicio | env var | default | deploy |
|---|---|---|---|
| HTTP | `WOLF_HTTP_PORT` | 47989 | **48989** |
| HTTPS | `WOLF_HTTPS_PORT` | 47984 | 48984 |
| Control (UDP) | `WOLF_CONTROL_PORT` | 47999 | 48999 |
| RTSP | `WOLF_RTSP_SETUP_PORT` | 48010 | **49010** |
| Video (UDP) | `WOLF_VIDEO_PING_PORT` | 48100 | 49100 |
| Audio (UDP) | `WOLF_AUDIO_PING_PORT` | 48200 | 49200 |

En Selene se teclea SOLO el puerto HTTP (`192.168.1.250:48989`). **Verificado en codigo**: Selene
lee el HTTPS del `<HttpsPort>` del serverinfo (`Selene/app/backend/nvcomputer.cpp:182-184`, NO lo
deriva como HTTP-5) y el RTSP del `rtspSessionUrl` (`moonlight-common-c Connection.c:272`) → los
puertos custom de Wolf se descubren dinamicos.

### GPU y sistema (verificado en vivo)
- Mapeo de render node NVIDIA-vs-iGPU y versión de driver: ver inventario en
  [[project_streaming_helios_apollo]] (memoria global). El default `WOLF_RENDER_NODE` ya apunta a la
  NVIDIA — correcto.
- **`nvidia-container-toolkit` en repo OFICIAL** (`extra/nvidia-container-toolkit 1.19.1`, >=1.16.0
  que pide Wolf; NO hace falta AUR). Driver cumple el mínimo (>=530). `/dev/uinput` y `/dev/uhid` presentes.
- Config GPU en el compose = metodo container-toolkit (`--gpus`/`deploy.resources` + `NVIDIA_*=all`).
  **Caveat**: upstream marca este metodo como "not as stable" (issue #152) y prefiere el manual
  (volumen de drivers); fallback documentado en el runbook.

### Input / colision de nombres Wolf-vs-Helios: DESCARTADA (cierra el "Blocker A")
El `85-wolf.rules` del repo confirma que Wolf identifica su input con firmas DISTINTAS a Helios:
- mouse/teclado: vendor **`ab00`** (Helios usa `0xBEEF/0xDEAD`).
- gamepads: **`Wolf X-Box One/PS5/Nintendo (virtual) pad`** + `Wolf gamepad (virtual) motion sensors`
  (Helios usa `Sunshine ... (virtual) pad`).
La regla mueve mouse/kbd de Wolf a `seat9` (parking, NO necesita GPU/DRM/compositor) y da acceso a
los gamepads. Como las firmas NO coinciden, la regla **no toca el input de PROD** → segura de
instalar con PROD vivo. (Cierra la incognita que §Modelo Wolf (b) dejo abierta.)

### Arquitectura del despliegue (del repo)
Wolf = 1 contenedor con `--network=host` + `docker.sock` + `/dev`+`/run/udev` montados; lanza
contenedores hermanos por app (Steam/Firefox/RetroArch/... via GStreamer+`gst-wayland-display`).
Estado en `/etc/wolf/` (config.toml **autogenerado** al primer arranque — NO se escribe a mano;
key/cert.pem, fake-udev, profile_data/ por-app). PulseAudio embebido (supervisord). Apps default:
Wolf UI, Test ball, perfil `user` con Firefox (+Steam referenciado).

### Pendiente para Jordi (nada ejecutado)
1. Correr el `RUNBOOK.md` (root): instalar toolkit + `nvidia-ctk runtime configure`; instalar
   udev rule; `docker compose up -d`; verificar serverinfo en 48989 + PROD intacto.
2. Emparejar Selene (`192.168.1.250:48989`) por el flujo de PIN de Wolf (URL en logs → mete PIN).
3. **QA visual (solo Jordi):** video OK + confirmar que mouse/teclado/gamepad NO controlan el KDE
   de seat0 (el bug que motivo todo). Empezar con "Test ball" (sin GPU/descarga), luego Firefox/Steam.
4. Decidir rol del broker: 1 caja → Wolf solo; multi-caja/mixto → broker delante (reusar
   identity-routing + hand-off de `helios-broker`).

### Fuentes (URLs)
- Wolf docs: https://games-on-whales.github.io/wolf/stable/ · How it works:
  https://games-on-whales.github.io/wolf/stable/dev/how-it-works.html · fake-udev:
  https://games-on-whales.github.io/wolf/stable/dev/fake-udev.html · Headless Wayland:
  https://games-on-whales.github.io/wolf/stable/dev/wayland.html · Quickstart (udev/GPU/privilegios):
  https://games-on-whales.github.io/wolf/stable/user/quickstart.html · Configuration:
  https://games-on-whales.github.io/wolf/stable/user/configuration.html
- Compositor `gst-wayland-display` (input programático): https://github.com/games-on-whales/gst-wayland-display
- inputtino: https://github.com/games-on-whales/inputtino
- Issues: #7 "Go fully headless" https://github.com/games-on-whales/wolf/issues/7 · #81 "Fix Steam input in
  unprivileged containers" https://github.com/games-on-whales/wolf/issues/81
- Local (read-only, 2026-07-19): docker 29.6.1 ✓, gamescope ✓, NVIDIA driver OK (versión en memoria global), `nvidia-container-toolkit` **AUSENTE**, solo `seat0`, `Helios/third-party/inputtino` presente.

## ✅ WOLF VALIDADO EN VIVO (QA de Jordi) — aislamiento de input CONFIRMADO
Jordi emparejó Selene↔Wolf por LAN (`192.168.1.250:48989`, PIN vía `/pin/#hash`), streameó "Test ball",
y **confirmó que el input NO controló su KDE** (el bug que nos trajo aquí). Wolf aísla el input por
diseño (mouse/kbd inyectados programáticamente al compositor, no uinput global). Esto es LISTO por QA
del usuario, no solo técnico. Deploy vivo: `wolf-deploy/docker-compose.yml` (bloque +1000, GPU zero-copy
en renderD128). PROD (47989) intacto. **Wolf = el backend headless del proyecto.** Pendiente: Steam real
(login de Jordi), multi-sesión concurrente (visión "yo+novia"), acceso externo (port-forwarding router).

## Wolf multi-sesión — PRUEBA CONCURRENTE (2026-07-21, agente, headless 100% en la Linux)
**Veredicto honesto: SÍ hay multi-sesión concurrente, con un blocker de deploy (compositor) que
impide el stream ESTABLE — pero NO es defecto de concurrencia (el mismo blocker pega en 1 sola sesión).**

### Cómo se probó (reproducible)
Script: **`wolf-deploy/demo-wolf-multisesion.sh`** (versionado). Levanta DOS clientes Selene headless
(A y B) cada uno con **HOME/XDG/config y Xvfb propios** (`:121` / `:122`), Vulkan NVIDIA neutralizado
(`VK_ICD_FILENAMES=/nonexistent` + `LIBGL_ALWAYS_SOFTWARE=1`) + `--video-decoder software` — el mismo
patrón de `helios-broker/demo-e2e-local.sh`. Binario: `~/.cache/selene-e2e/bin/selene`. Logs de cada
corrida en `wolf-deploy/run-multisesion/`. NO tocó PROD (47989/47990 intactos), ni ~/.config/sunshine,
ni el pairing del Mac. Correr: `cd wolf-deploy && ./demo-wolf-multisesion.sh` (imprime PASS/FAIL).

### Pairing programático del PIN a Wolf (RESUELTO — clave para automatizar)
El `#hash` de la URL `http://IP:48989/pin/#<SECRET>` es el **secret** del pair request. El submit real es:
`POST http://IP:48989/pin/` con body JSON `{"pin":"1234","secret":"<SECRET>"}` (Content-Type json) →
responde `"OK"`. El SECRET se extrae del log de Wolf `Insert pin at http://IP:48989/pin/#<SECRET>`
(`docker logs wolf`). Fuente: `wolf/src/moonlight-server/rest/servers.cpp:39-56` + `rest/html/pin.html:64-79`.
Cada cliente elige su PIN con `selene pair IP:48989 --pin NNNN`; se somete ESE mismo PIN al secret que
Wolf logueó para ese pairing. **GOTCHA:** Wolf **borra los pending pair del MISMO client_ip** al llegar
uno nuevo (`servers.cpp:80-85`); como los dos clientes headless salen del mismo IP (192.168.1.250),
hay que emparejar **SECUENCIAL** (A completo → B), no en paralelo. Además hay que esperar una línea
`Insert pin` NUEVA (baseline count), porque quedan líneas viejas del Mac en el log.

### Evidencia dura de CONCURRENCIA (de la corrida 20:49–20:50)
- **2 clientes AISLADOS emparejados**: `config.toml` de Wolf lista **3 `[[paired_clients]]`**, cada uno con
  cert distinto y su **propio `app_state_folder`** (`11561140821194591267`, `159802558851840615` = A y B;
  el 3º es el Mac). Ese folder-por-cliente ES el aislamiento de estado/HOME por sesión que pide la visión.
- **2 launches concurrentes**: cada Selene logueó `Launch response ... <sessionUrl0>rtsp://.../49010</sessionUrl0>
  <gamesession>1</gamesession>` y `RTSP port: 49010`.
- **AMBOS clientes recibieron video A LA VEZ**: los dos loguearon `Received first video packet after 0 ms`.
  Cliente A además decodificó un stream **limpio 30fps**: `Incoming/Decoding/Rendering frame rate: 30.00 FPS`,
  `Frames dropped: 0.00%`, `Average decoding time: 0.45 ms`.
- **4 pipelines GStreamer vivos simultáneos** (video+audio × 2 sesiones): las 4 `Pipeline reached End Of Stream`
  cayeron juntas (20:50:17.98–18.01) → las 2 sesiones estaban activas al mismo tiempo.
- `docker ps`: **NO** aparecieron contenedores hermanos, porque **"Test ball" usa `runner type=process`**
  (un `sh -c while sleep` + `videotestsrc` de GStreamer dentro de Wolf), no `docker`. El contenedor-por-sesión
  (`{name}_{session_id}`, `runners/docker.cpp:184`) solo aparece con apps docker (Firefox/RetroArch/Steam).

### BLOCKER de deploy (NO de concurrencia) — compositor Wayland virtual vacío
Las 2 sesiones se **cayeron a los ~5s**: A → `Server notified termination reason: 0x80030023`; B → quedó en
`Waiting for IDR frame`. Causa en logs de Wolf (repetida en AMBAS sesiones):
`Wayland endpoint /run/user/wolf/ exists but is not a socket` → `[STREAM_SESSION] Wayland socket  was not
ready, aborting runner startup` (nótese el **doble espacio** = `wayland_socket_name` VACÍO). El path queda
`/run/user/wolf/` + `""` = el directorio → falla el check. Origen: `create_wayland_display(...)` /
`gst-wayland-display` devuelve socket name vacío → `wait_for_wayland_socket` falla → `StopStreamEvent`
(`sessions/moonlight.cpp:167-183`). El CUDA context sí se crea (`Creating CUDA context ... renderD128`), pero
el **compositor Wayland virtual por-sesión no levanta**. **CRÍTICO para el diagnóstico:** este MISMO error
pegó en el intento 1-a-1 del Mac (20:27) → es un bug de deploy GPU/compositor que afecta a CUALQUIER sesión
(1 ó 2), **no** una limitación de multi-sesión. Test ball igual entregó video porque su patrón sale de
`videotestsrc` (no necesita el compositor); una app docker (Firefox/Steam) que SÍ lo necesita quedaría negra.

### Respuestas al encargo
(a) **¿2 sesiones concurrentes aisladas?** SÍ a nivel de sesión/pairing/pipeline/recepción de video: 2 clientes
   aislados (cert+state-folder propios) emparejados y AMBOS recibiendo video a la vez, 4 pipelines vivos. NO se
   pudo demostrar el stream ESTABLE ni el contenedor-hermano-por-sesión por el blocker del compositor.
(b) **¿Dónde falló?** No en el pairing del 2º (ambos OK) ni en que Wolf serialice (no serializa — corrió 2
   sesiones). Falló el **compositor Wayland virtual** (`gst-wayland-display` → socket vacío), que tumba el
   runner a los ~5s. Afecta 1 y 2 sesiones por igual (deploy/GPU), no la concurrencia.
(c) **Reproducir:** `cd wolf-deploy && ./demo-wolf-multisesion.sh` (evidencia en `run-multisesion/`).
(d) **Para la prueba real (Steam + 2 personas):** FALTA arreglar el compositor Wayland virtual de Wolf en
   esta caja (investigar por qué `gst-wayland-display` da socket vacío: ¿versión del plugin en `:stable`,
   permisos de `XDG_RUNTIME_DIR=/run/user/wolf`, driver NVIDIA 610 vs gbm/wayland en contenedor, `nvidia-drm.modeset`?).
   Sin eso, apps docker (Steam/Firefox) quedan negras. Con eso arreglado, esperar `WolfSteam_<sid1>` y
   `WolfSteam_<sid2>` como 2 contenedores hermanos (evidencia de aislamiento por contenedor). Además: login
   de Steam (Jordi) + acceso externo (router) para clientes fuera de la LAN.

## Wolf compositor — RESUELTO (2026-07-21, turno autónomo) — era el MÉTODO GPU, no la imagen
**El "bug del compositor" (socket Wayland vacío) tenía DOS causas superpuestas, ambas ya
entendidas y RESUELTAS. El compositor de Wolf FUNCIONA en esta caja con `:stable` + método
GPU MANUAL.** Deploy final vivo: `wolf-deploy/docker-compose.yml` (imagen `:stable`, método
manual driver-volume, `WOLF_USE_ZERO_COPY=TRUE`, `WOLF_LOG_LEVEL=INFO`). PROD (47989/47990)
intacto (apollo.service `active`).

### Causa 1 (falso positivo) — "Test ball" tiene `start_virtual_compositor=false`
Todo el diagnóstico previo (incl. la §"Wolf multi-sesión — PRUEBA CONCURRENTE" de arriba)
midió con **"Test ball"**, la ÚNICA app del `config.toml` con `start_virtual_compositor=false`
(verificado: Wolf UI/Firefox/Steam/RetroArch/... todas son `true`). En ese caso Wolf NO crea
compositor y hace `on_ready->set_value({})` con `socket_name` VACÍO (`sessions/moonlight.cpp`
~L97-140); luego `wait_for_wayland_socket(runtime_dir, "")` (`sessions/common.hpp`) hace `stat`
del path `/run/user/wolf/`+"" = el DIRECTORIO → `exists but is not a socket` +
`Wayland socket  was not ready` (doble espacio). **Ese error NO es el compositor** — es el path
sin-compositor de Test ball. Para probar el compositor hay que usar app con
`start_virtual_compositor=true` (**Wolf UI** es la más ligera).

### Causa 2 (el bug real) — el container-toolkit PANICA el compositor (wolf#379); el manual NO
A/B duro, MISMA imagen `:stable`, MISMA app **Wolf UI**, disparado con Selene headless local:
- **container-toolkit** → `thread panicked at wayland-display-core/src/comp/mod.rs:390: Failed
  to create GsCUDABuf` → thread del compositor muere, socket se destruye, runner NO arranca.
  = **games-on-whales/wolf#379** (CachyOS+NVIDIA+toolkit; el maintainer ABeltramo recomienda el
  método manual). Síntoma acompañante: `libnvrtc.so: undefined symbol: nvrtcGetCUBINSize` (wolf#405).
- **método MANUAL** (driver volume: `gow/nvidia-driver` con `NV_VERSION=$(cat
  /sys/module/nvidia/version)`=610.43.03, montado en `/usr/nvidia`) → `Create wayland compositor`
  → `EGL hardware-acceleration enabled` → `Listening on wayland socket. socket_name="wayland-1"`
  → `Render device: GB203 [RTX 5070 Ti]` → `Wayland display ready, listening on: wayland-1`.
  El socket `/run/user/wolf/wayland-1` EXISTE (`srwxr-xr-x`) y el contenedor-hermano
  `Wolf-UI_<sid>` queda **Up >30s** (no cae a los 5s). Con **zero-copy TRUE**.

### Ninguna IMAGEN arregla el compositor (probadas todas)
- `:main`, `:dev-wayland`, `:wayland` = imágenes VIEJAS/muertas (≈1GB vs 2.3GB de stable; config
  path `/wolf/cfg`, ignoran los `WOLF_*_PORT`, crash-loop `bind: Address already in use` / segfault).
  Las ramas git homónimas NO existen (gh api 422); son tags stale de ramas borradas.
- `:fix-nvidia-blackwell-zero-copy` (PR#424 — **CERRADO SIN MERGE, enfoque DESCARTADO** a favor de #425; era Blackwell sm_120+ DMABuf→glupload→cudaupload),
  `:fix-nvidia-zero-copy` (PR#439), `:test-gst-wld-block-linear` = arrancan bien pero fallan IGUAL
  con toolkit (el fix es el MÉTODO GPU, no la imagen). PR#425 "fix Blackwell black screen" y PR#418
  "wayland startup race" YA están MERGED en `:stable`.
- **`WOLF_USE_ZERO_COPY=FALSE`** (el "fix" del intento previo) NO era el fix real: el compositor
  seguía sin arrancar (con Test ball) y con toolkit panicaba en otra ruta igual. Con método manual,
  zero-copy TRUE funciona → se dejó TRUE.

### Cómo se disparó la sesión sin la Mac (reproducible)
Selene headless en la Linux (`~/.cache/selene-e2e/bin/selene`, env del `demo-wolf-multisesion.sh`:
Xvfb+HOME/XDG propios, Vulkan NVIDIA neutralizado). Pairing programático (`selene pair IP:48989
--pin 1234` + secret del log `Insert pin at .../pin/#<SECRET>` → `POST /pin/`), luego `selene
stream IP:48989 "Wolf UI"`. Señal: log `Listening on wayland socket` + `docker exec wolf ls
/run/user/wolf/` (socket `wayland-N`) + `docker ps` (`Wolf-UI_<sid>` vivo). Scripts de prueba en
el scratchpad del turno (test-wolfui.sh / test-compositor.sh), no versionados.

### PENDIENTE (QA visual, solo Jordi)
Que el picture de una app REAL se vea bien — el encode H.264 en Blackwell (wolf#417) YA está ARREGLADO en
`:stable` (fix = PR#425, merged 2026-06-08; ver corrección VERIFICADA al final de esta sección) — y
que el input NO controle el KDE de seat0. El objetivo de ESTE task (compositor crea socket +
sesión sobrevive) está CUMPLIDO y verificado técnicamente; el QA visual no lo hace una tool autónoma.

Fuentes: wolf#379 (CachyOS GsCUDABuf), #405 (nvrtc Blackwell), #417 (H.264 Blackwell), #425/#418
(merged); docs .../user/quickstart.html "Nvidia (Manual)". RUNBOOK.md §"Compositor — RESUELTO".

### ✅ VERIFICADO EN GITHUB 2026-07-24 (agente, `gh` autenticado) — correcciones a los números
- **El blocker de Blackwell = issue #417** ("H.264 failing on blackwell, works on ampere"; reportado en una RTX
  5070 Ti). **CERRADO/resuelto.** Fix = **PR #425** ("bump gst-wayland-display to fix Blackwell black screen"),
  **MERGED a `stable` el 2026-06-08**, validado en RTX 5080 driver 610.43.02 (mismo tramo 610.x que Jordi). Causa
  raíz: el `gst-wayland-display` pineado usaba `VideoFormat::from_fourcc` (solo YUV) para el dmabuf `AR24`/BGRA →
  formato erróneo → pantalla negra; #425 sube el pin al commit que usa `gst_video_dma_drm_fourcc_to_format`.
- **PR #424 fue DESCARTADO** (cerrado sin merge por su propio autor "in favor of #425"). La imagen
  `:fix-nvidia-blackwell-zero-copy` = enfoque abandonado → **NO usarla como fallback**; el fix real vive en `:stable`.
- **#418 NO es de Blackwell** (fix de race de arranque de la Wolf-UI Wayland; merged, tema distinto).
- **#379 era una RTX 4070 (NO Blackwell), driver 580xx** — el GsCUDABuf panic es real pero es del container-toolkit
  en CachyOS; workaround = método manual driver-volume (sigue vigente). Relacionados Arch+NVIDIA: #301, #306 (cerrados).
- **IMPLICACIÓN:** el deploy usa `:stable` (desde 07-21, posterior al fix) → **YA tenía el fix de #417**; el encode NO
  fue el problema. **RAZÓN REAL del abandono (confirmada por Jordi 2026-07-24):** tras resolver el titileo y anclar la
  RTX a Wolf, **SÍ se conectó y se lanzaron Wolf-apps con Selene desde la Mac (1 conexión — Firefox OK)**; el muro fue
  el paso siguiente y objetivo real: **2 conexiones SIMULTÁNEAS** (Jordi+Liora) → ahí **crashes una y otra vez, nunca
  estabilidad.** NO fue el encode Blackwell ni el incidente KDE (ese fue días después). El "validado en vivo" de arriba
  era un éxito de 1 conexión, no multi-sesión estable. Gate de retry = estabilidad de Wolf en MULTI-SESIÓN (core dumps
  en `/etc/wolf/cfg/backtrace.*.dump`). Ver [[forks-helios-selene]] §⏸️ BACKLOG.

## ✅✅ ARQUITECTURA VALIDADA EN VIVO (QA de Jordi) — sin flicker  ⚠️ [SUPERSEDED: flip ABANDONADO 2026-07-24 — ver "ACTUALIZACIÓN 2026-07-24" al final del doc]
Flip de GPUs aplicado: **monitor físico → iGPU AMD (card0-HDMI-A-2, 4K, KWin en amdgpu)**; **RTX 5070 Ti
DEDICADA a Wolf (sin display físico, todos los conectores card1 disconnected)**. Resultado confirmado por
Jordi: **Firefox streamea por Wolf y el monitor físico NO titila** (cero flicker). Esto resuelve de raíz la
frustración #1 (virtual display disrumpiendo el monitor) — vía separación de GPU, no vía parche de Helios.
Compositor de Wolf arreglado con el método MANUAL de driver NVIDIA (no container-toolkit, wolf#379).
Estado (19-jul): al mover el cable "just worked" (KWin Wayland siguió el monitor a la iGPU). ⚠️ El intento
POSTERIOR de dar persistencia al flip (Paso A, `KWIN_DRM_DEVICES`) desató el incidente del 23→24-jul y el flip
fue **ABANDONADO** — ver "ACTUALIZACIÓN 2026-07-24" al final del doc.

## Decisión 2026-07-21 · Paso C (Vulkan pin) — SE DEJA en NVIDIA (no se toca)
El pin de Vulkan de esta máquina (ruta exacta en [[project_streaming_helios_apollo]], memoria global) se
queda apuntando a la NVIDIA.
- **Por qué cumple el IFF de Jordi ("dejarlo SI Y SÓLO SI no implica titileo"):** el titileo era contención de
  SCANOUT (monitor físico + display virtual de streaming en la MISMA GPU), ya resuelto al mudar el scanout del
  monitor a la iGPU AMD. `vulkan_gpu.conf` solo enruta el RENDER de apps Vulkan del host, NO el scanout → es
  ORTOGONAL al titileo. Peor caso simultáneo (juego host en RTX + Wolf streameando en RTX) = contención de
  CÓMPUTO (stutter/fps), NO titileo del monitor (está en la iGPU, GPU distinta). Nunca "sin GPU": el archivo solo
  elige CUÁL GPU renderiza.
- **CAVEAT honesto:** razonamiento arquitectónico sólido, SIN QA visual bajo carga simultánea. Si algún día se usa
  simultáneo y SE VE titileo → hallazgo NUEVO, no el bug viejo.
- **Reversión (reversible):** editar/borrar el archivo — a `radeon_icd.json` = juegos host en iGPU; o borrarlo = host
  ve ambas GPUs y elige. Surte efecto al próximo login.
- Los dos ICDs instalados: `nvidia_icd.json` (RTX) y `radeon_icd.json` (radv/iGPU AMD).

## ⚠️ ACTUALIZACIÓN 2026-07-24 · EL FLIP DE GPU FUE ABANDONADO (tras incidente serio)
La sección "ARQUITECTURA VALIDADA EN VIVO" de arriba describe el flip FUNCIONANDO (19-jul) — sigue siendo cierto
que el fix del titileo (monitor→iGPU, RTX→Wolf) FUNCIONA — **pero la CONFIG está DESHECHA** y el experimento
headless queda EN PAUSA. Qué pasó:
- Al intentar dar **persistencia** al flip (Paso A: `KWIN_DRM_DEVICES`), la noche del 23→24-jul se desató una
  cascada de bugs de KDE/sistema (pantalla negra sin SDDM/Wayland). El Claude de la Mac (por SSH) la resolvió.
- Jordi **movió el cable HDMI de vuelta a la NVIDIA** → escritorio otra vez en la RTX (setup simple original),
  Wolf apagado. Verificado estable 07-24 12:24 (uptime 16h48, 67°C, load ~1).
- **Reporte completo + las 4 trampas duras del incidente (LEER, no reinventar ni duplicar aquí):** memoria
  GLOBAL `~/.claude/projects/-home-unjordi/memory/incidente-nocturno-2026-07-23.md`
  (`project_kde_plasmashell_reload.md` para el gotcha de `reload`). Si se retoma el flip, empezar por ahí.
