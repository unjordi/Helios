---
name: deploy-streaming-y-resolucion
description: Cómo se desplegó Helios(Apollo)-host + Moonlight/Selene-cliente en vivo (2026-06-28), el MECANISMO de autoajuste de resolución host↔cliente (= "frustración #2" que Jordi quiere cazar), y el gotcha de pairing por cert-CN en LAN. Minado de la sesión Juegos/PowerScripts.
metadata:
  type: project
---

Destilado de la minería de transcripts (2026-07-24) del slug `Juegos` (sesión `2c5c140e`, 2026-06-28).
**Canónica completa (receta reproducible):** vive en el proyecto Juegos/PowerScripts:
`streaming-apollo-moonlight.md` + el script `scripts/linux/apollo_Configurar.sh` (instala apollo+cage,
arma SSL, escribe config, agrega la app "Big Picture (gamescope)"). Aquí queda lo **directamente
accionable para Helios/Selene** (no duplico la receta entera).

## 🎯 AUTOAJUSTE DE RESOLUCIÓN host↔cliente (= la "frustración #2" a cazar)
El host debe igualar su modo de display a la resolución que pide el cliente al conectar (y revertir al
desconectar). Mecanismo observado en el deploy real de Apollo/Helios en CachyOS:
- Apollo expone `SUNSHINE_CLIENT_WIDTH` / `SUNSHINE_CLIENT_HEIGHT` (env de la app lanzada) con la resolución
  pedida por el cliente.
- Se aplica al host con **`wlr-randr`** (wlroots) o **`kscreen-doctor`** (KDE), y cadena de fallback:
  `--resolution` explícito → `xrandr --current` (para Game Mode / gamescope Xwayland) → valor guardado.
- Overlay de stats de Moonlight se apaga con `showperfoverlay=false`.
- Existe la skill `moonlight-bigpicture` (proyecto Juegos) que apunta el atajo de Moonlight del Deck a la
  app "Big Picture (gamescope)" que lanza `gamescope + steam -gamepadui` a la resolución del cliente.
### 🐛 ARREGLADO 2026-07-27: fallo SILENCIOSO cuando el monitor no tiene la resolución pedida
`helios-set-resolution.sh` (ex `apollo-`) tenía dos intentos de match, **ambos sobre el MISMO WxH**
(con y sin refresh). Si el monitor no tenía esa resolución, `MODE_NUM` quedaba vacío, el `if` no
entraba y el script **salía con éxito sin hacer nada, en silencio**.
- **Caso real:** Moonlight en el Deck pidió **1280×800** (nativa del Deck, 16:10); el LG UltraGear no
  tiene ningún modo 1280×800 → no ajustó. Selene pedía 1280×720, que sí existe, y por eso ese sí
  funcionaba. **La pantalla bloqueada fue coincidencia**, no la causa (era otro cliente).
- **Arreglo:** 3er fallback al **modo más cercano** (distancia `|dw|+|dh|`, desempate por refresh) +
  **logging** a `~/.local/state/helios-streaming.log`. Verificado en vivo: 1280×800 → modo 23
  (1280×720@60), y el round-trip completo `3440x1440 → 1280x720 → 3440x1440` con HDR restaurado.

### ⚠️ DOS GOTCHAS DE `kscreen-doctor` (verificados 2026-07-27, valen para cualquier script)
1. **Devuelve exit 0 aunque el output NO exista y aunque el modo NO exista.** Confiar en `$?` hace que
   un script reporte "OK" sin haber cambiado nada — el mismo fallo silencioso que se venía a arreglar.
   **Hay que RELEER el estado y comparar**, no creerle al código de salida.
2. **Colorea su salida con escapes ANSI**, y envuelve en ellos justamente el **modo ACTIVO**
   (`23:<ESC>[01;32m1280x720@60.00*<ESC>[0;0m`). Cualquier `grep` del modo activo falla en silencio si
   no se limpian antes: `kscreen-doctor -o | sed 's/\x1b\[[0-9;]*m//g'`.

**Por qué importa:** en KDE Wayland (el escritorio real de Jordi) este autoajuste es frágil — es la
frustración #2 del roadmap ([[forks-helios-selene]]). Punto de partida para el fix: revisar cómo Helios
aplica el cambio de modo en KDE Wayland (¿`kscreen-doctor`? ¿respeta/revierte?) y si el binario `build-helios`
lo hace distinto a `apollo_Configurar.sh`.

## ⚠️ GOTCHA de PAIRING por cert-CN en LAN (interop Helios/Selene)
En la MISMA LAN, Moonlight/Selene descubre el host por su **IP local** (IP/dominio exactos de esta máquina en
[[project_streaming_helios_apollo]], memoria global), pero el cert de Apollo/Helios está firmado para el
**dominio público** → el CN no cuadra con la IP → el pairing **falla en
"stage #4"** (el host marca "success" pero el cliente no lo completa). Nota: Selene hace **cert-PINNING**
(guarda el cert en el primer pairing), así que una vez emparejado por el canal correcto ya no re-valida CN;
el tropiezo es en el PRIMER pairing LAN contra un cert de CN público. Fuente: `2c5c140e`, 2026-06-28.

## Contexto del deploy real (2026-06-28)
- Host Apollo/Helios en CachyOS **expuesto público** (dominio, IP y ruta del cert exactos en
  [[project_streaming_helios_apollo]], memoria global).
- Cliente: Moonlight en el **Steam Deck**, emparejado.
- App "Big Picture (gamescope)": lanza gamescope+steam a la resolución del cliente, con auto-lock/unlock de sesión.

## Contexto GPU Blackwell (relevante para Helios encode / flip-GPU)
Juegos Windows/Proton crashean en init gráfico en la GPU NVIDIA Blackwell de esta torre (modelo/driver exactos
en [[project_streaming_helios_apollo]], memoria global) + Proton/DXVK/vkd3d sobre Wayland; los nativos sí
corren; los MISMOS juegos corren en el Steam Deck (AMD). Causa raíz = inmadurez del stack Blackwell + ese
tramo de driver. Explica por qué el encode/GPU es frágil en la misma caja que hostea Helios (contribuye al
por qué del flip-GPU y del caveat Blackwell de Wolf #417).
Fuente: `eeaa209e`, 2026-07-13 (investigación cerrada 2026-06-09), memoria del proyecto Juegos.

## 🔴 BUG 2026-07-27 (noche): un undo que se CUELGA aborta TODA la cadena (y deja la máquina abierta)
Reportado por Jordi como "en Steam Big Picture no se actualizó la resolución al cerrar". La causa real
es peor: `setsid steam steam://close/bigpicture` **se colgó** (no falló).
```
22:08:13.430  Executing Undo Cmd: [setsid steam steam://close/bigpicture]
22:08:23.430  Fatal: Hang detected! Session failed to terminate in 10 seconds.
```
**Mecanismo** (`src/process.cpp` ~742, `proc_t::terminate`): los undo se recorren **en orden INVERSO**
(`--_app_prep_it`) y cada uno hace **`child.wait()` sin timeout**. Un comando que bloquea detiene la
cadena entera → nunca corrió `helios-restore-resolution.sh` **ni el `helios-lock.sh` del
global_prep_cmd** → monitor atorado en 720p **y máquina DESBLOQUEADA**. A los 10 s el watchdog de
Sunshine mató el daemon (arrancó de nuevo a las 22:08:36).
- Nota: `terminate()` **NO** hace `break` con exit≠0 (solo un warning); el que sí hace `break` es
  `pause()`. Aquí el problema no fue el código de salida sino el **bloqueo**.

**Arreglado en `apps.json`** (respaldo `~/helios-rollback/apps.json.20260727-preUndoOrder.bak`), 2 cosas:
1. **Reordenar**: el comando riesgoso de Steam se movió al índice [0] del array → como el undo va en
   reverso, ahora corre **AL FINAL**, después de restaurar la resolución. Principio general:
   **los undo críticos (resolución, lock) van al FINAL del array; los riesgosos, al principio.**
2. **Acotarlo**: `timeout 10 setsid -f steam steam://close/bigpicture`. Medido 2026-07-27:
   `setsid -f` regresa en **1 ms** (forkea y suelta); `setsid` sin `-f` bloquea hasta que muera el
   hijo. Ese era el cuelgue.

✅ **RESUELTO en el código (2026-07-28, PR unjordi/Helios#15):** el bucle de undo de `terminate()` ya
tiene **timeout por comando**. Resultó ser la mitad de un problema mayor — una **carrera de datos** que
crasheaba el daemon. Detalle completo en [[bug-terminate-race]].

## 🎮 2026-07-27: Steam Big Picture veía el control del Deck como PLAYSTATION (config, no bug)
Era la ÚNICA app con `gamepad = ''` (= **auto**); las otras dos ya tenían `xone`. En auto, Helios elige
el tipo emulado según lo que reporta el cliente: el control del Deck expone **giroscopio y touchpad**,
que en el mapa de Sunshine son señas de **DualSense** → Steam lo detectó como PlayStation.
Lo delataba el warning en las apps con `xone`: *"Gamepad 0 has motion sensors, but they are not usable
when emulating a joypad different from DS5"*.
- **Valores válidos en Linux** (`src/platform/linux/input/`): `auto`, `xone`, `ds5`, `switch`
  (+ `disabled`, que se maneja en `process.cpp:247`).
- **Aplicado:** `Steam Big Picture` → `gamepad: "xone"`, alineada con las otras dos
  (respaldo `~/helios-rollback/apps.json.20260727-preGamepad.bak`).
- **TRADE-OFF que Jordi debe conocer:** con `xone` los **glyphs son ABXY** (correcto para el Deck, que
  es físicamente Xbox) pero se **pierden gyro y touchpad**. Con `ds5` los conservas pero Steam muestra
  glyphs de PlayStation (cruz/círculo) que no casan con los botones físicos del Deck.
  **PENDIENTE de su QA:** si extraña el gyro, se cambia a `ds5` y asume los glyphs.

## ✅ NO ES BUG: el gamescope no cambia la resolución A PROPÓSITO
Jordi reportó "en steam gamescope no se actualizó la resolución al conectar". Es **diseño deliberado**
suyo, documentado en el encabezado de `helios-bigpicture.sh`: *"sin tocar el monitor físico ni dejar
barras por mode-set; gamescope se pone fullscreen y rellena el monitor; gamescope renderiza
internamente a la resolución del cliente"*. Por eso su DO es `helios-bigpicture-save.sh` (solo
**guarda** modo+HDR para que el undo restaure exacto), no `set-resolution`.
**Costo real de ese diseño** (corregido 2026-07-27 tras leer el código — la primera versión de esta
nota decía "encoda 3440×1440, ~6× los píxeles" y **era FALSO**):
- ❌ **NO** se encoda a la resolución del monitor. `src/video.cpp:2017` fija `ctx->width = config.width`
  / `ctx->height = config.height`, o sea la resolución **del CLIENTE**. El ancho de banda es normal.
- ✅ **Sí** se **captura** el monitor completo (3440×1440 por KMS) y se **escala por frame** a 1280×800.
  Es costo de GPU/memoria que un mode-set evitaría, y pesa más con `CUDA=OFF` (`GPU→RAM→GPU`).
- ✅ **Doble reescalado**: gamescope renderiza 1280×800 → sube a 3440×1440 → Helios baja a 1280×800.
  Dos interpolaciones para volver al mismo tamaño; se pierde nitidez en texto/UI (justo lo de Big Picture).
- ✅ **QA de Jordi 2026-07-27:** *"en modo gamescope sí se veía retefeo pero todo estaba bien mapeado"*.
  - **"Retefeo" CONFIRMA el doble reescalado** — es el costo real y se nota.
  - **"Bien mapeado" DESCARTA mi teoría de las barras por aspecto** (había especulado que 21:9→16:10
    metería barras arriba/abajo y gamescope otras a los lados). El encuadre está bien; el modelo del
    escalador que armé estaba de más. Queda como recordatorio de no afirmar geometría sin verla.
### 📐 MEDIDO (turno nocturno 2026-07-27/28): el Deck usaba el 44.9% de su panel
Simulación fiel de la cadena con test card tipo UI generada **a cada resolución de render**
(`scratchpad/sim_v2.py` de esa sesión; comparación visual en `gamescope-comparacion.png`, en la raíz
del workspace). Geometría: monitor 3440×1440 (21:9) → cliente 1280×800 (16:10).

| Estrategia | Contenido en el Deck | % del panel | Nitidez |
|---|---|---:|---:|
| **A — actual**: sin mode-set, render 1280×800 | 857×536 | **44.9%** | 1.00× |
| B — sin mode-set, render 3440×1440 nativo | 1280×536 | 67.0% | 1.81× |
| C — mode-set 1920×1080, render nativo | 1280×720 | 90.0% | 2.45× |
| **D — mode-set 1280×720, render nativo** | 1280×720 | **90.0%** | **3.42×** |
| E — mode-set 2560×1080, render nativo | 1280×540 | 67.5% | 1.73× |

**No era solo el doble reescalado: eran DOS juegos de barras encadenados** — pillarbox de KWin
(16:10 dentro de 21:9) + letterbox de Helios (21:9 dentro de 16:10). Gana **D** porque la cadena
entera queda a la misma resolución: **cero resampleos**. Es lo que la app "Desktop" ya hacía bien y
el gamescope se había quedado fuera.
- **Caveat de método:** la varianza del Laplaciano premia el borde pixel-perfect; **C** renderiza más
  detalle real (supersampleado) y podría gustar más en texto aunque mida menos. Por eso el script
  trae el dial `HELIOS_SUPERSAMPLE` (default 1 = 1:1).
- **El 90% es el techo** con este monitor: no tiene ningún modo 16:10 y `kscreen-doctor` no crea modos
  personalizados. El 100% exigiría el output virtual (#1477) — ver [[estado-proyecto]].
- **LA CORRECCIÓN QUE LO DESBLOQUEÓ:** el flicker de 240 Hz que motivó el diseño original **era de
  RESTAURAR a 240 Hz, no del mode-set** — lo dice el propio `helios-bigpicture-save.sh`. Round-trip
  `3440x1440@239.99 → 1280x720@60 → 3440x1440@239.99 + HDR` verificado limpio.

**Desplegado:** app **`Big Picture (nítido)`** (`~/.local/bin/helios-bigpicture-nitido.sh`), conviviendo
con la original. Comparte el save/undo de la original. Pendiente de QA en [[estado-proyecto]].

## ℹ️ Las opciones `dd_*` de Sunshine NO sirven en Linux
El merge trajo el sistema nativo de display device (`dd_resolution_option`, `dd_refresh_rate_option`,
`dd_hdr_option`, `dd_mode_remapping`…) visible en la web UI. **En Linux es un no-op**:
`third-party/libdisplaydevice/src/` solo tiene `common/`, `macos/` y `windows/`. De ahí la línea del log
`Display device configuration is disabled. Reverting any active display device configuration.`
→ **En este host, los prep-cmd scripts siguen siendo el ÚNICO mecanismo de ajuste de resolución.**

## ℹ️ El "(Experimental)" que vio Jordi es de SELENE, no una alerta nueva
Etiquetas heredadas de Moonlight en `app/languages/qml_es.ts`: `AV1 (Experimental)`,
`Enable HDR (Experimental)`, `Enable YUV 4:4:4 (Experimental)`. Son las tres que trae palomeadas.
HDR y 4:4:4 ya los validó funcionando, así que aquí "experimental" = "upstream no lo garantiza en toda
combinación", no "inestable en este stack".

## 📛 RENAME 2026-07-27: los scripts son `helios-*`, ya NO `apollo-*`
Jordi: *"de paso que no diga apollo sino helios"*. Los **9** scripts de `~/.local/bin/` se renombraron
`apollo-*.sh` → `helios-*.sh` y se actualizaron las **9 referencias vivas** (7 en `apps.json`, 2 en
`sunshine.conf`). Verificado: las 7 rutas referenciadas existen y son ejecutables; daemon reiniciado sin
errores. Los originales están en `~/helios-rollback/scripts-apollo-prerename/`.
- ⚠️ **`/etc/sudoers.d/apollo-unlock` CONSERVA el nombre viejo** a propósito: es root-owned y la regla
  autoriza el COMANDO (`/usr/bin/loginctl lock-sessions|unlock-sessions`), **no** la ruta del script, así
  que el rename no la afecta. Renombrarla exigiría root y no aporta nada.
- ⚠️ El **servicio systemd sigue siendo `apollo.service`** (viene del paquete AUR). Ese no se tocó.
- Las menciones a `apollo-*.sh` MÁS ABAJO en este archivo son **históricas** (describen lo que pasó en
  esas fechas, cuando ese era su nombre). No son rutas vigentes.

## 🐛 BUG ENCONTRADO 2026-07-27: el undo NO corre en desconexión SUCIA (`terminate-on-pause`)
El QA del 26-jul (abajo) validó el round-trip **cerrando el stream a propósito**. El 27-jul el Deck se
**desconectó SOLO** y el resultado fue distinto: el monitor se quedó en **1280×720** y el undo nunca corrió.

**Mecanismo (`src/process.cpp:671-683`):** al desconectarse todos los clientes se llama `proc_t::pause()`.
Si `_app.terminate_on_pause` es **false**, la app queda *pausada indefinidamente* y los prep-cmds de **undo
NUNCA se ejecutan**; solo `terminate()` los corre. Las apps de Jordi tenían `terminate-on-pause: false`
(salvo "Big Picture (gamescope)"), y el default de Apollo al parsear `apps.json` también era `false`.
Síntoma delator en el log: última línea `Session pausing for app [...]`, sin `Executing Undo Cmd`, y
`/serverinfo` ya reportando `SUNSHINE_SERVER_FREE`.

⚠️ **La consecuencia GRAVE no era la resolución sino el `global_prep_cmd`:** su undo es
`apollo-lock.sh` → **la máquina se quedó DESBLOQUEADA** tras la desconexión, en un host expuesto a Internet.

**Arreglado en dos capas (Jordi 2026-07-27: *"ponle terminate-on-pause por default a todas las apps que
tenemos ahorita y en el futuro"*):**
1. **Config en vivo:** las 3 apps de `~/.config/sunshine/apps.json` en `terminate-on-pause: true`
   (respaldo: `~/helios-rollback/apps.json.20260727-preTerminateOnPause.bak`).
2. **Código:** rama `feat/terminate-on-pause-default` — default `false → true` al parsear apps.json,
   igual para las entradas sintéticas "Desktop (fallback)" y "Virtual Display", checkbox de app nueva
   en la web UI activado, y `terminate_on_pause_desc` explicando la consecuencia de apagarlo.
   ("Remote Input" ya era true; "Terminate" se deja en false — es entrada de control.)

**Trade-off aceptado:** con `true`, un parpadeo de red termina la sesión en vez de permitir reconectar y
retomar. Se lo advertí a Jordi explícitamente para "Steam Big Picture" (cerraría BP a media partida) y
aun así lo quiso parejo. Se puede optar por lo anterior por-app con `"terminate-on-pause": false`.

## ✅ QA EN VIVO 2026-07-26 (Deck) — autoajuste de resolución round-trip VALIDADO (con cierre LIMPIO)
Con Selene (AppImage de CI) en el Steam Deck emparejado a Helios (daemon backporteado build-helios2):
el prep-cmd `apollo-set-resolution.sh` bajó el host a **1280×720** al arrancar el stream de "Desktop", y
el undo `apollo-restore-resolution.sh` **restauró el monitor nativo (3440×1440 21:9, 239.99 Hz, HDR ON,
perfil Integrado)** al cerrar. Confirmado por Jordi (2 screenshots) + log de Helios. Frustración #2 = OK
de punta a punta. **403 previo al abrir "Desktop" = permisos por-cliente de Apollo sin otorgar tras el
pairing** (paso operativo, no bug). Ese primer stream salió SDR/HEVC 8-bit (HDR no activado en Selene +
el modo 720p del host en SDR) → HDR pendiente de validar: exige HDR en Selene Y que el display streameado
del host esté en HDR (frontera experimental Linux/Wayland).

## 💡 DISEÑO: el selector "auto" de códec ignora el decode por HW del cliente
> Ítem vivo en [[estado-proyecto]] #15. Aquí solo el análisis.
En el QA del Deck, con códec en **"auto"**, la negociación eligió **AV1** — que el Deck (APU Van Gogh,
**RDNA2, sin decode AV1 por hardware**) solo puede decodificar **por software**. Funcionó fluido a 720p, pero
el "auto" NO debería preferir un códec que el cliente solo puede hacer por software teniendo HEVC con decode
por HW disponible. **Meta:** que el auto-select (lado Selene y/o negociación con Helios) **prefiera el códec
que el cliente decodifica por HARDWARE** (HEVC en RDNA2/Van Gogh), cayendo a software solo si no hay opción.
Es un fix de lógica de selección de códec (Selene client). Prioridad media (afecta batería/calor/estabilidad en HW sin AV1).

## Hallazgo QA 2026-07-26: HEVC+HDR ✅ en el Deck; 4:4:4 bloqueado por el HOST (Helios)
- **HEVC + HDR 10-bit VALIDADO** en el Deck (log: `hevc_nvenc` + `HDR Rec.2020 PQ 10-bit`), decode por HW
  (VCN), fluido. Es el combo recomendado del Deck (vs AV1 que en Van Gogh/RDNA2 va por software).
- **YUV 4:4:4: Selene muestra "el host no soporta 4:4:4"** → lo bloquea **Helios** (host), no el cliente.
  Helios no anuncia la capacidad 4:4:4 (extensiones SCM_ chroma de Sunshine). **CAUSA CONFIRMADA 2026-07-26:**
  Helios está 561 commits detrás de Sunshine (Apollo dejó de sincronizar el 2025-09-27) y el soporte 4:4:4 del
  host llegó DENTRO de esa brecha: `39c9e845` "Add hardware yuv444 chromasubsampling support on nvidia cards
  (cuda/cuda gl)" y `e4740f06` "Allow YUV4:2:0 HDR and YUV4:4:4 HDR on nvidia cards". ⚠️ **Van por la ruta
  CUDA/cuda-gl y el daemon vivo se compila con `SUNSHINE_ENABLE_CUDA=OFF`** → cerrar la brecha es necesario
  pero puede NO ser suficiente: habrá que evaluar el flip a CUDA=ON. Ver [[backport-sunshine-a-helios]].
  El NVENC Blackwell SÍ puede 4:4:4 → limitación de SOFTWARE de Helios. **BACKLOG: habilitar/backportear
  4:4:4 en Helios** (candidato del inventario backport-sunshine-a-helios). Prioridad baja (nicho: nitidez de texto en escritorio, SDR).
- **Framepacing:** Selene lo hereda de Moonlight-Qt (ajustes de Video). ON = movimiento parejo (+~1 frame latencia); OFF = mínima latencia.

## 💡 DISEÑO: handshake de capacidades host→cliente (JSON claro, no adivinar)
> Ítem vivo en [[estado-proyecto]] #17. Aquí solo el análisis y la restricción de Jordi.
Idea de Jordi: que al conectar/sincronizar, el **host (Helios) le pase al cliente (Selene) un JSON con las
opciones DISPONIBLES** (códecs + si cada uno es HW/SW en ese host, HDR, 4:4:4, resoluciones/fps, bit-depth…)
para que el cliente **no tenga que adivinar** ni ir por intento-y-error (como pasó con el 4:4:4, que salió
"host no soporta" de forma reactiva).
- **Ya existe parcialmente:** el protocolo Moonlight expone `/serverinfo` con flags **`SCM_*`** (ServerCodecModeSupport)
  que anuncian códecs/HDR/HEVC-RExt-4:4:4/etc. El "host no soporta 4:4:4" salió porque Selene lee esos flags y
  Helios NO setea el de 4:4:4. Ver skills `moonlight` + `gamestream-protocol`.
- **Lo que falta (el item):** (1) que Helios anuncie TODO correctamente (incl. 4:4:4 cuando aplique — liga con el
  otro backlog); (2) enriquecer el intercambio a un JSON legible/completo de capacidades; (3) que Selene lo use
  PROACTIVAMENTE en el auto-select (elegir códec HW-decodable, formato compatible) en vez de reactivo.
  Liga con: "auto de códec debería preferir decode por HW del cliente" + "habilitar 4:4:4 en Helios".
  **RESTRICCIÓN DURA (Jordi 2026-07-26): debe ser una EXTENSIÓN del protocolo, aditiva y RETRO-COMPATIBLE**
  — no un cambio que rompa clientes/hosts viejos (Moonlight base, Sunshine puro). Modelo a seguir: como los
  flags `SCM_*` o un campo/endpoint OPCIONAL nuevo que los peers que no lo entienden simplemente ignoran, sin
  perder la interoperabilidad con el ecosistema Moonlight. Prioridad media.

### ✅ RESUELTO 2026-07-27: el host YA ANUNCIA 4:4:4 (H.264 + HEVC), con `CUDA=OFF`
El merge de la brecha (PR unjordi/Helios#12) **trajo el soporte 4:4:4 por HW en NVIDIA**
(`39c9e845` #4965 + `e4740f06` HDR). Desplegado `build-helios3` el 2026-07-27, el host anuncia
`ServerCodecModeSupport = 2032385 (0x1F0301)`, que incluye **`SCM_H264_HIGH8_444`,
`SCM_HEVC_REXT8_444` y `SCM_HEVC_REXT10_444`**. Antes (build-helios2) Selene decía "el host no
soporta 4:4:4" → **la brecha con Sunshine era la causa, y cerrarla lo resolvió.**
- **AV1 4:4:4 sigue NO anunciado** — `av1_nvenc: YUV 4:4:4 not supported` es límite de NVENC para AV1,
  no del build. Para 4:4:4 hay que elegir **HEVC** (o H.264) en Selene, no AV1.
- ❌ **Se refutó la hipótesis "CUDA=OFF bloquea el 4:4:4"** (anotada el 26-jul leyendo el código): los
  flags salen del **probe real** de encoders, no de qué se compiló. `gl_cuda_vram_t` es la ruta 4:4:4
  **zero-copy**, no la única. El flip a `CUDA=ON` queda como optimización de rendimiento, no como
  desbloqueo. Detalle y lección de método en [[sync-sunshine-2026-07]].
- ⏳ **Pendiente el QA en vivo de Jordi**: anunciar la capacidad ≠ que el stream 4:4:4 funcione.
