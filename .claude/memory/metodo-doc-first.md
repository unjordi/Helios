---
name: metodo-doc-first
description: Lección de método (confirmada por Jordi 2026-07-25) — en el ecosistema Helios+Wolf+Selene, el barrido de doc+issues del upstream va PRIMERO; ya está destilado en los 5 skills por-proyecto, arrancar por ahí.
metadata:
  type: feedback
---

En proyectos de **upstream maduro** (Wolf, Sunshine, Moonlight, Apollo, Artemis — con documentación
extensa y cientos de issues), el **barrido de doc oficial + issues/foros va PRIMERO**, antes de
reverse-engineerear el código o debuggear empíricamente a ciegas.

**Por qué:** en la saga de Wolf de julio se hizo al revés — se diagnosticó la "tormenta EVP", el
abandono histórico y la arquitectura de perfiles desde el CÓDIGO + pruebas en vivo, y la
documentación (`docs/modules/*.adoc`) se leyó tarde. Costó vueltas evitables: la doc de Wolf
respondía directo lo de multi-sesión/perfiles/lobbies, y los issues (p.ej. **#265** teardown
zero-copy) explicaban dolores que reprodujimos a mano. Jordi lo confirmó: *"me encanta! debimos
hacerlo antes"*.

**Cómo aplicarlo:** ya no hace falta re-investigar — el know-how quedó **destilado en 5 skills**
(`.claude/skills/`: `wolf`, `helios`, `sunshine`, `selene`, `moonlight`) sobre 10 memorias de
referencia (`*-doc-sintesis.md` + `*-issues-forums.md`, indexadas en MEMORY.md). **Arranca por el
skill del proyecto que vas a tocar**; baja al código/pruebas solo para lo que el skill no cubra o
para validar. Es la encarnación concreta del "Paso 0: INVENTARIO de lo que ya existe" del cerebro global.

Relacionado: [[forks-helios-selene]], y los skills del ecosistema.
