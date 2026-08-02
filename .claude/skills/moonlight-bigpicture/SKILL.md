---
name: moonlight-bigpicture
description: Apuntar el acceso de Moonlight del Deck (cliente) directo a la app "Big Picture (gamescope)" del host Apollo en CachyOS — el "Steam Link que sí jala" (entra a la consola de Steam a la resolución nativa del Deck, fluido). Usar cuando Jordi diga "conecta Moonlight directo a Big Picture", "apunta el acceso de Moonlight a la consola", "que el Deck entre directo al Steam de la compu", o tras (re)configurar el host. Lado CLIENTE; el host se configura aparte (apollo_Configurar.sh, proyecto Juegos/PowerScripts; contexto de deploy real en memoria deploy-streaming-y-resolucion de este cerebro).
---

# Moonlight → "Big Picture (gamescope)" (cliente, Deck)

Apunta el acceso de Moonlight del **Deck** a la app **`Big Picture (gamescope)`** que vive en el host
**Apollo "Cachy"** (CachyOS). Esa app lanza una sesión **gamescope + `steam -gamepadui`** a la
resolución que pida el cliente → entra directo a la consola, nativo y fluido. (La vieja
`Steam Big Picture` abría Steam como ventana sobre el escritorio = inútil; ya no se usa.)

Contexto host (NO se toca desde el Deck): ver memoria [[deploy-streaming-y-resolucion]] (este cerebro,
HeliosSelene) y, en CachyOS, `scripts/linux/apollo_Configurar.sh` (proyecto Juegos/PowerScripts, fuera
de este repo). Relacionado: skill `add-nonsteam-app` (técnica shortcuts.vdf, proyecto Juegos).

## 0. Prerrequisitos (verificar primero)
- Deck **emparejado** con Apollo (host `Cachy`; si LAN no resuelve, `unjordi.pisa.mx`).
- La app existe en el host. Confirmar desde el Deck:
  ```bash
  flatpak run --command=moonlight com.moonlight_stream.Moonlight list Cachy
  ```
  Debe aparecer **`Big Picture (gamescope)`** (nombre EXACTO, con paréntesis). Si no, el host no
  está actualizado → correr `apollo_Configurar.sh` en CachyOS o avisar a Jordi.

## 1. Wrapper de resolución (ya existe)
El acceso usa **`~/juegos/.claude/artifacts/moonlight-auto.sh [HOST] [APP]`**, que detecta la pantalla
activa del Deck (dock 3440×1440 / portátil 1280×800) y pasa `--resolution WxH --fps` por CLI.
gamescope en el host renderiza a ESA resolución. Probar sin lanzar:
```bash
DRY_RUN=1 ~/juegos/.claude/artifacts/moonlight-auto.sh "Cachy" "Big Picture (gamescope)"
```
Debe imprimir el comando con `stream "Cachy" "Big Picture (gamescope)" --resolution WxH ...`.

## 2. Apuntar el acceso de Steam a la app nueva
El acceso "Moonlight" en Steam tiene `Exe=…/moonlight-auto.sh` y `LaunchOptions`. Solo hay que cambiar
el 2º argumento de LaunchOptions a **`"Big Picture (gamescope)"`**:

- LaunchOptions destino: `"Cachy" "Big Picture (gamescope)"`

Edición segura de `shortcuts.vdf` (mismo método que `add-nonsteam-app`):
1. **Steam CERRADO** (`pgrep -x steam` vacío; si no, `steam -shutdown` y esperar).
2. **Respaldar**: copiar `~/.steam/steam/userdata/<id>/config/shortcuts.vdf` a `shortcuts.vdf.bak-<fecha>`.
3. Parser Python (tipos VDF 0x00 mapa / 0x01 string / 0x02 int32 LE, fin 0x08): localizar el shortcut
   cuyo `AppName`/`Exe` contenga `moonlight-auto.sh`, poner `LaunchOptions = "Cachy" "Big Picture (gamescope)"`.
4. **Round-trip check** (parse→serialize == bytes originales salvo el campo cambiado) ANTES de escribir.
   Si no cuadra, ABORTAR y restaurar el backup.
5. Reabrir Steam y verificar el acceso.

> Alternativa rápida (si no quieres tocar el .vdf): cambiar el **default** del wrapper editando
> `moonlight-auto.sh` línea `APP="${2:-Steam Big Picture}"` → `APP="${2:-Big Picture (gamescope)}"`.
> Sirve solo si el acceso NO pasa el 2º argumento; si LaunchOptions ya manda el APP, gana ese.

## 3. Verificar en vivo
- Lanzar el acceso (o Game Mode). Debe **entrar directo a la consola de Steam** del host, a la
  resolución del Deck, sin barras.
- Salir: con mando, botón **STEAM → Exit Game**; con teclado, `Ctrl+Alt+Shift+Q`. Al desconectar, el
  host limpia solo (restaura el monitor y reabre su Steam de escritorio) y **termina la sesión**
  (`terminate-on-pause:true`).

## Gotchas
- **Nombre EXACTO** `Big Picture (gamescope)` (con espacio y paréntesis); si no, Moonlight no la
  encuentra y cae al escritorio o falla.
- La **1ª conexión** ya no debe fallar (el host arranca gamescope de inmediato). Si alguna vez falla y
  te bota al login del host, es el `apollo-lock` global al cortar; reintenta — suele entrar a la 2ª.
- **Flicker en el monitor del host** tras la sesión es problema del HOST (240Hz+HDR al filo de banda),
  no del Deck; documentado en [[deploy-streaming-y-resolucion]].
- El **stats overlay** de Moonlight se desactivó (`showperfoverlay=false`); editar `Moonlight.conf`
  siempre con Moonlight cerrado.
