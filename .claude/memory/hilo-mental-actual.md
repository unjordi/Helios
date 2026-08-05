# Hilo mental actual
> Se SOBRESCRIBE (no se appendea). Última actualización: **2026-08-05 ~14:35** · repo `Helios` · rama `develop` · nivel **ligero**.

## En qué estamos AHORA
Cerrando el slice del **403 de Selene→Helios desde la Mac**, que quedó **RESUELTO y validado en vivo por
Jordi**. Lo único abierto de este slice es esperar el CI del PR #17 (ya mergeado a `develop`).

## Decisión abierta / lo que razonamos
Ninguna decisión pendiente en este slice. Lo que sigue es trabajo nuevo, ya escrito en
[[estado-proyecto]] (#21, #22, #23).

## Siguiente paso concreto
1. Confirmar que el CI de `develop` (`b8dbd103`) queda **verde**. Corre en background el watch del run
   `31043596507`. Si sale rojo, fix-forward por rama → PR (ahora sí bloqueado por el check requerido).
2. Commitear las memorias de este slice por rama → PR (este archivo, [[bitacora]], [[estado-proyecto]]).

## Hilos sueltos / no olvidar
- ⚠️ **El context del check requerido lleva el `&amp;` literal.** Ver [[estado-proyecto]] ítem ~~4~~.
  Arreglar el nombre del job sin actualizar la protección bloquea TODOS los merges a develop.
- **`.claude/memory/.contexto-aviso` está TRACKEADO** y es estado de hook por-máquina: ensucia cada
  commit de memoria. Candidato a gitignorear (no lo toqué, no me lo pidieron).
- Worktrees vivos: `scratch-chroma/` (`feat/log-chroma-sampling`) y `scratch-race/`
  (`fix/terminate-race`, PR #15). Builds por barrer: `build-helios3/` (1 GB, huérfano).
- Sigue intacto de sesiones previas: **revisar JUNTOS el fix de `terminate()`** antes de upstream
  (`~/code/ajenos/Sunshine`, ramas locales `fix/terminate-race` y `fix/enforce-cert-expiry`, sin push),
  corregir el encuadre del "CVE" (**no** es auth bypass), y el test de `test_process.cpp`.

## RESUELTO HOY (no reabrir)
- **El 403 era `PERM::_default` sin `launch`**, no caddy, ni firewall, ni el monitor. La Mac estaba
  emparejada desde el 19-jul con `perm = 50331648` (`list|view`). Jordi otorgó permisos en la web UI y
  **validó en vivo**: 4 min de sesión con HDR + `hevc_nvenc` + 10-bit + **4:4:4**.
- **Por qué no había log:** esas negaciones usan `BOOST_LOG(debug)` (`nvhttp.cpp:1376`) y
  `min_log_level` default es `info` (`config.cpp:887`). La pista de Jordi (*"si no tienes un log del
  último par de minutos, algo sigue mal"*) era correcta; mi lectura inicial de ella no.
- **Sin monitor físico NO se puede streamear** — lista KMS vacía → `Couldn't find monitor [0]`. Es un
  problema DISTINTO del 403 y así quedó separado.
- **El host NO tiene la culpa del cuelgue al cerrar** (ítem #23): `CLIENT DISCONNECTED` 14:16:23 +
  cadena de undo completa en 2.5 s + daemon `active` con 0 reinicios.
- **Required status checks ACTIVADOS** en `develop` con OK explícito de Jordi.
