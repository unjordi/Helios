---
name: bug-terminate-race
description: CRASH recurrente de Helios (SIGSEGV en proc_t::terminate) por carrera de datos — terminate() es alcanzable desde 4+ hilos sin ningún mutex y recorre un ITERADOR MIEMBRO. Bug de UPSTREAM (Sunshine y Apollo lo tienen igual), no del fork. Con evidencia de 3 crashes en 2 días.
metadata:
  type: project
---

# CRASH: carrera de datos en `proc_t::terminate()` (2026-07-28)

**Severidad: alta.** Tira el daemon completo en medio de una sesión, y con él se pierden los undo
prep-cmds — incluido `helios-lock.sh`, o sea que **la máquina puede quedar DESBLOQUEADA** (host expuesto
a Internet).

## Evidencia

Crashes registrados con `build-helios4` (`coredumpctl list`):

| Fecha | Señal | Contexto |
|---|---|---|
| 2026-07-27 21:55:10 | **SIGSEGV** | sesión Steam Big Picture |
| 2026-07-27 22:08:23 | **SIGTRAP** | "Hang detected! Session failed to terminate in 10 seconds" |
| 2026-07-28 08:50:42 | **SIGSEGV** | sesión "Big Picture (nítido)", backtrace completo capturado |

Backtrace del 28-jul (hilo `nvhttp::47984`):

```
#0  proc::proc_t::terminate(bool, bool)                      <-- SEGV (SEGV_MAPERR)
#1  stream::session::join(session_t&)
#2  rtsp_stream::terminate_sessions()
#3  nvhttp::cancel(Response, Request)                        <-- endpoint HTTP /cancel
#4  SimpleWeb::ServerBase<nvhttp::SunshineHTTPS>::write(...)
...
#13 std::thread ... nvhttp::start()                          <-- hilo del servidor HTTPS
```

**Síntoma delator en el log:** el undo corre **DOS VECES**.
```
08:50:36.544  Terminating app [Big Picture (nítido)] when all clients are disconnected.
08:50:36.748  Executing Undo Cmd: [helios-bigpicture-undo.sh]
08:50:37.544  Executing Undo Cmd: [helios-bigpicture-undo.sh]   <-- otra vez
08:50:40.929  Executing Undo Cmd: [helios-lock.sh]
08:50:42      dumped core
```

## Mecanismo

`proc_t::terminate()` (`src/process.cpp:729`) recorre **estado MIEMBRO**, no una copia local:

```cpp
for (; _app_prep_it != _app_prep_begin; --_app_prep_it) {
    auto &cmd = *(_app_prep_it - 1);
    ...
    child.wait();          // BLOQUEANTE: la ventana de carrera dura segundos
}
```

`_app_prep_it` se comparte y se muta sin protección. **No hay UN SOLO `std::mutex` /
`lock_guard` / `scoped_lock` en todo `src/process.cpp`** (verificado con grep).

Y `terminate()` es alcanzable desde **al menos 4 hilos distintos**:

| Origen | Archivo |
|---|---|
| Hilo del **system tray** (4 sitios) | `src/system_tray.cpp:98,137,144,456` |
| Hilo HTTPS de **config web** (3 sitios) | `src/confighttp.cpp:894,1184,1235` |
| Hilo HTTPS de **nvhttp** vía `/cancel` → `rtsp_stream::terminate_sessions()` | `src/rtsp.cpp:792` |
| Ruta de **sesión/proceso** | `src/process.cpp:90,1678` |

**El escenario que se dispara solo:** al cerrar el stream, el cliente manda `/cancel` por HTTP
*mientras* la ruta de desconexión ya está terminando la app. Dos `terminate()` concurrentes recorren
el mismo iterador → los undo se ejecutan dos veces y el iterador se pasa de `_app_prep_begin` → SEGV.
El `child.wait()` bloqueante mantiene la ventana abierta **segundos**, así que no es una carrera
estrecha: es fácil de pegar.

## ⚠️ Es un bug de UPSTREAM, no del fork

Verificado contra los dos ancestros:

```
mutex/lock_guard/scoped_lock en src/process.cpp:
  Apollo upstream : 0
  Sunshine master : 0
  Helios develop  : 0

el bucle con iterador miembro existe idéntico en:
  Apollo   src/process.cpp:719
  Sunshine src/process.cpp:328
```

→ **NO lo introdujo el merge de los 561 commits.** Afecta a todo el ecosistema Sunshine.
Candidato a PR upstream (LizardByte/Sunshine), no solo a Helios.

## Arreglo propuesto (NO implementado — requiere OK de Jordi)

1. **Un `std::recursive_mutex` miembro** tomado en `terminate()`, `launch()` y `pause()`. Recursivo
   porque `pause()` llama a `terminate()` cuando `terminate_on_pause` está activo.
2. **Idempotencia:** un flag `_terminating` (o comparar `_app_prep_it == _app_prep_begin`) para que la
   segunda llamada concurrente salga sin repetir los undo.
3. **Iterar sobre una copia local** en vez del miembro, para que dos llamadas nunca compartan cursor.
4. **Timeout por comando** en el bucle de undo (esto además arregla el `SIGTRAP` "Hang detected" del
   22:08): hoy un prep-cmd colgado tumba el daemon y se salta los undo de seguridad.

Los puntos 1-3 atacan el SIGSEGV; el 4 ataca el SIGTRAP. Son el mismo bucle.

## Mitigación ya aplicada (parcial, del lado config)

En `apps.json` se reordenó el undo de "Steam Big Picture" para que el comando riesgoso de Steam corra
AL FINAL (`timeout 10 setsid -f steam steam://close/bigpicture`). Eso reduce la probabilidad de colgar
el bucle, **pero no elimina la carrera** — el crash del 28-jul pasó con la app "nítido", que ni siquiera
tiene el comando de Steam en su undo.

Ver también [[deploy-streaming-y-resolucion]] (bug del undo colgado, 27-jul).
