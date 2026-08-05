---
name: bitacora
description: Historia append-only de HeliosSelene — qué pasó y cuándo. Lo que SIGUE va en estado-proyecto.md, no aquí. Se appendea al FINAL con >>, nunca se edita arriba.
metadata:
  type: project
---

# Bitácora — qué pasó

> **Cómo se escribe:** SIEMPRE al final y con `>>`, nunca con un Edit
> (`printf '%s\n' '- FECHA · qué' >> bitacora.md`). Así varias sesiones/agentes pueden escribir a
> la vez sin pisarse. **Qué SIGUE** va en [[estado-proyecto]]; el **porqué y el detalle** en la
> memoria temática que corresponda. Aquí solo el registro.

- 2026-06-28 · Deploy inicial de Helios+Moonlight en vivo. Autoajuste de resolución host↔cliente funcionando. Detalle en [[deploy-streaming-y-resolucion]].
- 2026-06-28 · Auditoría de seguridad: se detecta el fallo de validación de certs y se codifica como test rojo. **OJO:** se registró como "auth bypass"; el 2026-07-28 se corrigió ese encuadre (ver abajo).
- 2026-07-18 · Rename de carpetas `Apollo/`→`Helios/` y `artemis/`→`Selene/`.
- 2026-07-19 · **Selene PR #4 MERGEADO** — fix del build de macOS (AGL + qyieldcpu). Validado por uso: el `Selene.app` del Mac es el cliente que decodifica HEVC por VideoToolbox en los tests multi-sesión. Cierra el pendiente que arrastraba `RETOMAR-validacion-macos.md` (archivo eliminado el 28-jul por obsoleto: su receta `brew install qt6` contradecía la buena, que es aqt).
- 2026-07-23 · Incidente de KDE (pantalla negra por `by-path` en `KWIN_DRM_DEVICES`). Escritorio de vuelta a NVIDIA.
- 2026-07-24 · **Wolf en pausa** tras el incidente. Headless suspendido.
- 2026-07-25 · Investigación del ecosistema: 10 memorias de doc+issues (Wolf, Apollo, Sunshine, Artemis, Moonlight) + skills por-proyecto. Método doc-first.
- 2026-07-26 · **Decisión: cerrar la brecha con Sunshine por MERGE COMPLETO**, no cherry-pick — es lo único que avanza el merge-base.
- 2026-07-26 · QA en el Deck: autoajuste de resolución round-trip validado; HEVC+HDR 10-bit validado; 4:4:4 rechazado por el host (causa: la brecha).
- 2026-07-26 · **PR unjordi/Helios#12 MERGEADO** (merge commit, NO squash): 561 commits, 102 conflictos, 5 bugs silenciosos atrapados. Merge-base `1a96d135` (2025-09-26) → `6f58be35` (2026-07-26). Detalle en [[sync-sunshine-2026-07]].
- 2026-07-27 · Desplegado `build-helios3`; el host **YA ANUNCIA 4:4:4** H.264/HEVC. Se REFUTÓ que `CUDA=OFF` lo bloqueara: los flags salen del probe real, no de qué se compiló.
- 2026-07-27 · Bug cazado en vivo: `terminate-on-pause=false` dejaba la app en pausa tras desconexión sucia y los undo NUNCA corrían → monitor atorado en 720p y **máquina DESBLOQUEADA**. **PR #13 MERGEADO** (default a `true`) + config en vivo.
- 2026-07-27 · Scripts renombrados `apollo-*` → `helios-*` (9 archivos + 9 referencias vivas). `/etc/sudoers.d/apollo-unlock` y `apollo.service` conservan el nombre viejo a propósito.
- 2026-07-27 · Arreglado el fallo SILENCIOSO de `set-resolution` (si el monitor no tenía la resolución pedida no hacía nada). Dos gotchas de `kscreen-doctor` documentados: sale exit 0 aunque el output/modo no exista, y colorea con ANSI rompiendo el grep del modo activo.
- 2026-07-27 · **PR #14 MERGEADO** — el log del host imprime `Chroma sampling`. Desplegado `Helios/build-helios4` (dentro del repo: arregla el build huérfano Y el version stamp, que eran el mismo bug).
- 2026-07-27 · Instalado el **aviso sonoro** global de Claude (hook `Notification`). Validado por Jordi. Preferencia dura: sale por el dispositivo DEFAULT, Claude no lo controla.
- 2026-07-27/28 · **Turno nocturno gamescope.** Medido que el Deck usaba solo el **44.9%** de su panel (857×536 de 1280×800) por doble juego de barras + doble reescalado. Con mode-set a 1280×720 y render nativo: **90% y 3.42× nitidez**. Corrección clave: el flicker de 240 Hz era de RESTAURAR a 240, no del mode-set. Desplegada la app `Big Picture (nítido)` conviviendo con la original.
- 2026-07-28 · **CRASH diagnosticado**: carrera en `proc_t::terminate()` (3 crashes en 2 días). Bug de UPSTREAM — Apollo y Sunshine tienen el mismo código sin mutex. **PR #15** abierto con el fix de 4 partes. Detalle en [[bug-terminate-race]].
- 2026-07-28 · Recon de cómo contribuir a Sunshine ([[contribuir-a-sunshine]]): la IA está PERMITIDA pero hay que declararla; el mismo fix ya murió como PR #3604 por ser grande y no tener test.
- 2026-07-28 · **Auditoría de memorias y cierre de slice.** Se creó esta bitácora y [[estado-proyecto]] como único backlog; se consolidaron los reportes paralelos de la raíz (`REPORTE-gamescope-nocturno.md`, `RETOMAR-validacion-macos.md`) y se eliminaron.
- 2026-07-28 · Auditoría cerrada: 0 .md sueltos en la raíz, 0 memorias huérfanas, 0 backlogs secundarios, 8 wikilinks corregidos (`project-streaming-helios-apollo` → `project_streaming_helios_apollo`, que era el slug real de la memoria global).
- 2026-08-05 · Jordi mató Wolf: estaba en loop consumiendo ~40% de TODO el CPU. Parqueado (ítem #18); al retomar, diagnosticar + posible PR upstream a Wolf.
- 2026-08-05 · Selene en la Mac no conecta a Helios: **403 permission denied** con monitor puesto. Helios verificado SANO (HTTP 200 local, 0 reinicios, puertos OK). Pista de Jordi: el log de Helios NO tiene líneas de esos minutos → el 403 probablemente NO lo emite Helios sino algo en el camino (sospechoso: caddy-proxy). Diagnóstico EN CURSO.
- 2026-08-05 · **403 de la Mac RESUELTO — era permisos por-cliente, no red ni proxy.** `MacBook` estaba emparejada (19-jul) con `perm = 50331648` = `PERM::_default` = `list|view`, SIN `launch`. Diagnóstico difícil porque esas negaciones se loguean con `BOOST_LOG(debug)` y el default es `info` → el 403 no dejaba NI UNA línea (se sospechó de caddy-proxy y del firewall; la pista buena fue de Jordi: *"si no tienes un log del último par de minutos, algo sigue mal"*). El Deck conectando bien fue el control experimental que lo confirmó. Jordi otorgó los permisos en la web UI y **validó en vivo**: sesión de 4 min con HDR + `hevc_nvenc` + 10-bit + **chroma 4:4:4**. De paso desemparejó `SteamDeckMoonlight`. → **PR #17** cambia `PERM::_default` a `list|view|launch|controller|mouse|kbd` (clipboard, file transfer, server_cmd, touch y pen siguen opt-in) + 2 tests que fijan ambos lados del contrato.
- 2026-08-05 · Verificado que **sin monitor físico Helios no captura**: la lista KMS sale vacía y da `Couldn't find monitor [0]` (no 403). Y **bug nuevo del CLIENTE**: Selene en la Mac no cierra tras el stream, hay que force-quitear — el host cerró limpio 2.5 s antes (ítem #23).
