---
name: estado-proyecto
description: EL BACKLOG VIVO de HeliosSelene — la ÚNICA fuente de verdad de qué sigue. Aquí empiezas siempre. Cada ítem apunta a la memoria temática que tiene el detalle; el detalle NO se duplica aquí.
metadata:
  type: project
---

# Estado del proyecto — qué sigue

> **Regla:** este archivo es el **único backlog**. Si un pendiente vive en otro lado, está mal.
> Las memorias temáticas guardan el **detalle y el porqué**; aquí solo el ítem, su estado y a dónde ir.
> Lo que YA PASÓ va a [[bitacora]], no aquí. Última curación: **2026-07-28**.

## 🔴 Esperando a Jordi (bloquean trabajo)

| # | Ítem | Detalle en |
|---|---|---|
| 1 | **Revisar juntos el fix de `terminate()`** antes de cualquier cosa upstream. Dos ramas LOCALES listas en `~/code/ajenos/Sunshine` (`fix/terminate-race`, `fix/enforce-cert-expiry`), sin fork ni push. | [[bug-terminate-race]], [[contribuir-a-sunshine]] |
| 2 | **El "CVE-2026-32253" está mal encuadrado.** No es auth bypass (cada X509_STORE lleva UN cert pareado; un cert desconocido se rechaza; TLS exige la llave privada). Lo real: **no se aplica la expiración**. Hay que corregir el commit `ebc73d10` de Helios y [[security-findings-2026-06.local]], que hoy afirman el bypass. | [[bug-terminate-race]] §cert |
| 3 | **¿Retirar la app vieja de gamescope o conviven?** Hoy conviven "Big Picture (gamescope)" y "Big Picture (nítido)". | [[deploy-streaming-y-resolucion]] |
| 4 | **¿Activar required status checks** en la protección de `develop`? Sin ellos el `--auto` mergea sin esperar al CI (pasó con el PR #13). | — |

## 🟡 Pendiente de QA en vivo (solo Jordi puede declararlo)

| # | Ítem | Detalle en |
|---|---|---|
| 5 | **`Big Picture (nítido)`** — mode-set + render nativo. Medido 90% del panel y 3.42× nitidez vs 44.9%/1.00×. ⚠️ Primera vez que gamescope entra con mode-set: vigilar si vuelve el **flicker de 240 Hz**. | [[deploy-streaming-y-resolucion]] |
| 6 | **Steam Big Picture**: que el control salga **Xbox** (ya no PlayStation) y que al cerrar **restaure resolución + bloquee pantalla**, sobre todo **con Steam ya cerrado** (la condición que colgaba). | [[deploy-streaming-y-resolucion]] |
| 7 | **PR #15 (fix de la carrera)** — build verde, 0 warnings. Falta cerrar varias sesiones desde Selene sin crash. | [[bug-terminate-race]] |
| 8 | **HDR + 4:4:4 en el Deck** ya validados por uso; el log del host ahora imprime `Chroma sampling`. | [[deploy-streaming-y-resolucion]] |

## 🟢 Helios — trabajo identificado

| # | Ítem | Prioridad | Detalle en |
|---|---|---|---|
| 9 | **Test en `tests/unit/test_process.cpp`** para el fix de terminate. **Requisito para upstream** (`process.cpp` está al 0.74% de cobertura; sin test repetimos el destino del PR #3604). | Alta si vamos upstream | [[contribuir-a-sunshine]] |
| 10 | **#1477 Virtual Display en Linux.** Es el camino elegante del gamescope: sin mode-set, sin barras, 100% del panel. Bloqueado hoy — Helios solo captura *outputs*, no ventanas, y este KWin no expone `/VirtualOutputs`. Exigiría `vkms` o EDID override. | Media, varias sesiones | [[deploy-streaming-y-resolucion]] |
| 11 | **Borrar `build-helios3/`** (1 GB, huérfano y no puede compilar). Es el rollback #1 del override; se va cuando `build-helios4` esté validado. | Baja, higiene | CLAUDE.md |
| 12 | **Flip a `SUNSHINE_ENABLE_CUDA=ON`.** Ya NO desbloquea el 4:4:4 (refutado); solo zero-copy + NvFBC. | Baja | [[sync-sunshine-2026-07]] |

## 🔵 Selene — trabajo identificado

| # | Ítem | Detalle en |
|---|---|---|
| 13 | **Job de AppImage en CI** — hoy no produce artifact de Linux. | [[selene-targets-empaquetado]] |
| 14 | **Smoke test en RP6 (aarch64)** cuando Jordi recupere el equipo. | [[selene-targets-empaquetado]] |
| 15 | **El auto-select de códec debería preferir el decode por HW del cliente** (eligió AV1 en el Deck, que lo hace por software teniendo HEVC por HW). | [[deploy-streaming-y-resolucion]] |
| 16 | **Co-bumpear `moonlight-common-c`** al mismo pin en Helios y Selene (hoy desalineados). Todo bump = cambio de protocolo COORDINADO. | [[backport-moonlightqt-a-selene]] |
| 20 | **Selene en Wayland NATIVO en los handhelds** (decidido con Jordi el 2026-07-26). | [[selene-targets-empaquetado]] |

## 🟣 Producto / diseño

| # | Ítem | Detalle en |
|---|---|---|
| 17 | **Handshake de capacidades host→cliente** (JSON de lo disponible, para no adivinar). **Restricción dura de Jordi:** extensión ADITIVA y retrocompatible, estilo `SCM_*`. | [[deploy-streaming-y-resolucion]] |
| 18 | **Wolf: reintentar.** En pausa desde el 24-jul tras el incidente de KDE. | [[diseno-headless-multisesion]] |
| 19 | **El baile de instancia única de Steam** (cerrar el Steam de escritorio y esperar ~15 s) es lo que impide que se sienta "Steam Link". Wolf lo resuelve con contenedores. Cambio de arquitectura. | [[deploy-streaming-y-resolucion]] |
