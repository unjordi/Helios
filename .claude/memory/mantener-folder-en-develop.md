---
name: mantener-folder-en-develop
description: Preferencia dura de Jordi (2026-07-25) — sus folders de trabajo visibles Helios/ y Selene/ deben estar SIEMPRE en develop y actualizados tras cualquier integración; es su superficie de QA.
metadata:
  type: feedback
---

Jordi: *"oye, ponme el folder [de trabajo] SIEMPRE en develop y actualizado plz!"* (2026-07-25).

**La regla:** tras **cualquier integración a `develop`** (mergear PRs), Claude deja los **folders de trabajo
VISIBLES de Jordi** (`/home/unjordi/code/HeliosSelene/Helios` y `.../Selene`) en la rama `develop` y
**actualizados** — sin que Jordi lo pida cada vez.

**El CÓMO (rutina tras mergear a develop):**
1. `git -C <repo> branch --show-current` → confirmar que está en `develop` (si no, `git -C <repo> checkout develop`).
2. `git -C <repo> status --short` → si hay cambios SIN commitear (que no sean dirs untracked de build como
   `build-helios/`), **NO pisar**: avisar a Jordi en vez de forzar. Un `build-*/` untracked NO bloquea un fast-forward.
3. `git -C <repo> pull --ff-only origin develop` (ff-only: su develop local nunca lleva commits propios, el trabajo
   vive en ramas/worktrees de feature — ver [[forks-helios-selene]]).
4. Si un submódulo cambió de pin (p.ej. `moonlight-common-c`): `git -C <repo> submodule update --init <path>` para
   sincronizar el working tree al pin nuevo (si no, el submódulo aparece "modificado").

**Por qué:** es el modelo **mini-develop** del cerebro global — el folder visible del dev es su superficie ESTABLE
de QA, "lo que le doy a revisar refleja todo lo integrado". Claude aísla en **worktrees de FEATURE** y **mergea hacia
develop**; **NUNCA saca el develop de Jordi en un worktree propio** (una rama solo puede estar checked-out en un
worktree a la vez, y esa rama la posee su folder visible). Tras integrar, pone su folder al día.

**Ojo (definición de LISTO):** poner el SOURCE al día en develop ≠ desplegado. El daemon Helios EN VIVO sigue
corriendo el binario ya compilado hasta que se recompile+redespliegue (+ re-`setcap`, ver CLAUDE.md). Los backports
integrados en develop NO están corriendo en producción hasta ese paso, que es decisión aparte de Jordi.

Relacionado: [[forks-helios-selene]].
