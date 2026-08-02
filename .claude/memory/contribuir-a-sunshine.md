---
name: contribuir-a-sunshine
description: Cómo acepta contribuciones externas LizardByte/Sunshine — política de IA (permitida, hay que DECLARARLA), la receta para que un PR de bugfix no muera, las trampas que matan PRs (el mismo fix ya murió como PR #3604), y las manías del maintainer. Cárgalo ANTES de preparar cualquier PR upstream.
metadata:
  type: reference
---

# Contribuir a `LizardByte/Sunshine` (recon 2026-07-28)

Investigación de solo lectura sobre el repo y sus PRs/issues. Clone en `~/code/ajenos/Sunshine`.
Todo lo de aquí tiene evidencia citada; lo que no pude verificar está al final.

## Veredicto

**Muy receptivo y rápido — si el PR es PEQUEÑO.** 178 PRs de externos mergeados en 12 meses, **68% de
merge, mediana 1.4 días**; muchos entran sin un solo comentario humano. Pero es bimodal: **≤3 archivos
→ mediana 0 días; >3 archivos → 6 días o nunca.**

**El riesgo real no es el rechazo, es la parálisis.** Cuello de botella de una persona
(ReenigneArcher mergeó 100 de los últimos 100), que lo admite: *"It isn't stale, I'm just one person."*

## 🤖 La política de IA: PERMITIDA, y hay que DECLARARLA

Sunshine no tiene `CONTRIBUTING.md` propio; la fuente es `LizardByte/.github` →
<https://docs.lizardbyte.dev/latest/developers/contributing.html#ai-usage>.

- **La plantilla de PR del org trae un checkbox obligatorio**: `None / Light / Moderate / Heavy`.
  *"Heavy: AI generated most or all of the code changes"* **es una opción marcable, no un rechazo.**
- De los PRs de externos **mergeados** (n=114): None 65, Light 21, **Moderate 21, Heavy 7**.
- **Lo prohibido es la PROSA generada**: descripción del PR, cuerpo del issue, y responder reviews con
  un LLM. La regla con baneo (`COMMUNITY_RULES.md` #19) cubre **solo issues/posts, no código**.
- En sus palabras (#5429): *"**using AI is no problem.** I just do not want to have a discussion with
  LLMs in PR reviews or read scientific reports for PR descriptions."*
- El proyecto usa IA él mismo: hay `AGENTS.md` en la raíz y ReenigneArcher mergea PRs suyos marcados
  "Heavy". El contraejemplo que define la regla es el **PR #4209** (asistido por IA, mergeado):
  declaró el uso, probó en hardware real y contestó cada review él mismo.

→ **Marcar Heavy y escribir la prosa a mano.** El estigma es real (los PRs con label `ai` mueren 72%)
pero no es veto; la detección es **manual, a ojo del estilo de la prosa**.

## ⚠️ El precedente que hay que no repetir: PR #3604

**El mismo fix de la carrera de `terminate()` ya se escribió** (FrogTheFrog, enero 2025):
`recursive_mutex` + eliminar los iteradores miembro. **Murió sin UN SOLO review humano en 7 meses**,
cerrado por el bot de stale. No fue criterio técnico:

- **+491/−294 en 6 archivos** (metió un refactor `app_t` RAII — más limpio, y por eso murió)
- Sonar reprobó con **9 issues nuevos**
- Codecov **bajó** (`process.h` −12.50%)

**Nuestra ventaja no es mejor código:** es (a) un crash reportado **con backtrace**, (b) ≤3 archivos,
(c) un test que sube cobertura, (d) Sonar limpio, (e) un revisor Linux invocado por nombre.

## Checklist para el PR

**Antes de escribir código**
1. **Abre un ISSUE primero** con backtrace y reproducciones. Solo el 27% de los PRs mergeados
   referencian issue, pero es la diferencia entre "refactor especulativo" y "arregla un crash
   reportado" — justo lo que le faltó a #3604. Molde: **issue #5378**. **Escríbelo con tus palabras**:
   un issue que huela a IA se bloquea y cierra.
2. **PR contra `master`.** No hay `nightly` ni `develop`. Base equivocada = rebote.

**El código**
3. **Solo `src/process.cpp` + `src/process.h`.** No tocar `confighttp.cpp`, `nvhttp.cpp`, `stream.cpp`.
4. **No reestructurar.** Es lo que mató a #3604.
5. **`std::recursive_mutex`, no `std::mutex`** — `terminate()` es reentrante y llama a
   `system_tray::update_tray_stopped()` síncronamente; el **issue #4199** fue un deadlock tray↔teardown.
6. **Cuidado con el move-assign:** `refresh()` hace `proc = std::move(*proc_opt)`; un mutex *miembro*
   borra el move por defecto (`KITTY_DEFAULT_CONSTR_MOVE_THROW`) → **no compila**. Usar `static inline`.
7. **Bloquear también `execute()` y `refresh()`**, no solo `terminate()`.

**Los gates que matan PRs**
8. **Test en `tests/unit/test_process.cpp`** (Google Test; `GLOB_RECURSE`, no hay que registrar nada).
   `process.cpp` está al **0.74%** — casi cualquier test sube la cobertura.
9. **Sonar: 0 issues nuevos** (gate ≤0, y **sí corre en forks**).
10. **Doxygen en el HEADER, no en el `.cpp`.** Fricción nº1 (#3417, dos veces, la segunda como
    `CHANGES_REQUESTED`). No es gusto: `WARN_AS_ERROR = FAIL_ON_WARNINGS` + `WARN_IF_UNDOCUMENTED` con
    `BUILD_DOCS=ON` por defecto → **código nuevo sin documentar rompe el CI**.
11. **clang-format:** `uv sync --locked && uv run --locked --no-sync lb-update-clang-format`.
12. **No tocar el changelog** (autogenerado) ni traducciones que no sean `en`.

**Al abrir**
13. **Título conventional commits** (`fix(process): ...`). `semantic.yml` valida **solo el título**, y
    mergean con squash usando título+número → **tu título es el commit permanente**.
14. **Plantilla del org ENTERA**, sin borrar secciones ni los comentarios `<!-- -->`. Rebote nº1:
    16 apariciones de *"Please update the PR to use the correct template."*
15. **No hace falta CLA ni DCO** — Sunshine está en la lista de exentos del org.

**Después**
16. **No dejarlo en draft** (*"Typically I don't really review anything that's in draft"*).
    **No hacer force-push** tras el review (#3905: *"Please no more force pushing."*).
17. **Reclutar un revisor Linux.** ReenigneArcher **no usa Linux** y lo dice: *"we need buy in from the
    contributors that do use Linux"* (#4667). Nombres que él invoca: **psyke83, Kishi85, neatnoise,
    Dregu, andygrundman**.
18. **Mantenerlo vivo:** 90 días sin actividad → `stale` → 10 días → cerrado. **Los 40 PRs con label
    `stale` están todos cerrados sin mergear.**

## Issues y PRs relacionados con nuestro bug

**Ningún issue abierto lo describe** → el issue es información nueva.

| # | Estado | Relevancia |
|---|---|---|
| **#3604** | Cerrado por el bot | El mismo fix, muerto sin review. Referenciarlo como *"subconjunto mínimo de #3604"* |
| **#3417** | Cerrado por el bot | Predecesor con review. De aquí sale la regla del doxygen |
| #1538 | **MERGEADO** (cgutman) | Precedente positivo: las carreras en `process.cpp` sí entran cuando son pequeñas |
| **#4461** | **ABIERTO** | Do/Undo rompe la conexión — plausiblemente el mismo root cause visto desde fuera. Enlazarlo |
| #4199 | Cerrado | Deadlock tray↔teardown: peligro de diseño para el fix |

## Lo que NO se verificó

- **La lista de maintainers** (`/collaborators` da 403). Que ReenigneArcher es el gatekeeper es sólido;
  llamar "maintainer" a FrogTheFrog es **inferencia**.
- **Qué checks son BLOQUEANTES.** El propio `master` tiene 4 en rojo, así que el rojo es normal.
- **Causalidad de la label `ai`** — los ratios son reales, pero no se comprobó si murieron POR la IA.
- **Ninguna predicción** de si este PR se acepta. Hay tasas base, no promesas.
