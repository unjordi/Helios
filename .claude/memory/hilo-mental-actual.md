# Hilo mental actual
> Se SOBRESCRIBE (no se appendea). Última actualización: **2026-07-28 ~10:30** · workspace HeliosSelene (NO-git) · Helios en `develop` @ `e2e46b22` · nivel **COMPLETO**.
>
> **Este archivo es memoria de trabajo VOLÁTIL: solo "de qué va ESTO ahora mismo".**
> Qué SIGUE → [[estado-proyecto]]. Qué PASÓ → [[bitacora]]. El porqué → la memoria temática.
> Si aquí empieza a crecer histórico o backlog, está mal: se poda.

## En qué estamos AHORA
**Revisar JUNTOS el fix de la carrera de `terminate()` antes de mandarlo upstream.** Jordi se lleva la
sesión a VSCode para eso. Su encuadre: *"hay muchísima resistencia a los PR y bugfixes hechos con
ayuda de agentes… de este proyecto no he revisado nada de tu código"*.

Todo está preparado y **nada empujado a upstream**:
- `~/code/ajenos/Sunshine` — dos ramas **locales**, sin fork ni push:
  `fix/terminate-race` (`ed90cf18`) y `fix/enforce-cert-expiry` (`c199348a`). Build verde, 0 warnings.
- `unjordi/Helios` **PR #15** (`fa099827`) — el mismo fix en nuestro fork, build verde.

## Decisión abierta / lo que razonamos
1. **El encuadre del "CVE".** Mi lectura del código dice que **NO es auth bypass**: cada `X509_STORE`
   lleva UN cert pareado, un cert desconocido cae en `default: return ok` con `ok=0` y se rechaza, y
   TLS exige además la llave privada. Lo real es que **no se aplica la expiración**. Si Jordi coincide,
   hay que corregir el commit `ebc73d10` de Helios y [[security-findings-2026-06.local]].
2. **Declaración de IA.** Sunshine la permite y la EXIGE declarar. Creo que corresponde **Heavy**.
   La prosa del PR tiene que ser de Jordi — mis mensajes de commit actuales son justo el "informe
   científico" que al maintainer le choca.
3. **¿Test antes de mandarlo?** Sin test repetimos el destino del PR #3604. Ver [[contribuir-a-sunshine]].

## Siguiente paso concreto
Abrir el diff de `fix/terminate-race` en VSCode y revisarlo línea por línea. Orden propuesto:
el diff → el encuadre del cert → el test → quién firma qué.

## Hilos sueltos / no olvidar
- **`build-race/` y `scratch-race/`** (worktree + build dir de este trabajo) siguen en el workspace.
  También `scratch-chroma/` y `scratch-helios-b2/`, que son de slices ya cerrados → candidatos a barrer.
- El **daemon en vivo** es `Helios/build-helios4` y está sano; no se tocó en este slice.
- Los reportes que estaban sueltos en la raíz se consolidaron a memoria el 28-jul y se movieron a
  `.claude/.trash/` (no borrados). En la raíz solo queda `CLAUDE.md` y `gamescope-comparacion.png`.
