---
name: artemis-issues-forums
description: Barrido GitHub/foros de Artemis (wjbeckett/artemis, padre de Selene) — features, bugs a medio cocer, y estado de vida del proyecto. Fechado 2026-07-25.
metadata:
  type: reference
---

# Artemis (`wjbeckett/artemis`) — barrido GitHub + foros · 2026-07-25

Padre upstream de **Selene** (nuestro fork cliente de Moonlight-Qt). Barrido de issues,
PRs, commits y foros hecho el 2026-07-25 vía API de GitHub y WebSearch.

## Estado del repo (snapshot API, 2026-07-25)
- Descripción: "GameStream client for PCs (Windows, Mac, Linux, and Steam Link)". Licencia GPL-3.0.
- Rama base: **`develop`**. Creado 2025-07-11. **355 stars, 28 forks, 7 watchers.** No archivado.
- **`pushed_at` = 2025-09-11**; el `updated_at`=2026-07-25 es solo metadata (stars/comentarios), NO código.
- **22 issues abiertos.** Solo **1 PR abierto** (#64).

## 1. Qué HACE bien (delta sobre Moonlight-Qt)
Fork de Moonlight-Qt que porta al cliente de PC las features "Artemis" que ya existían en el
Artemis de Android, para emparejar con Apollo (host). Por el README, en fases:
- **Fase 1 (completa):** Clipboard Sync, Server Commands (comandos custom en el host Apollo/Sunshine),
  **OTP Pairing** (pairing por one-time-password), **Quick Menu** (overlay in-stream).
- **Fase 2 (completa):** refresh rates fraccionales (90/120Hz custom), **escalado de resolución
  client-side**, selección de **virtual display** desde el cliente.
- **Fase 3 (completa):** lanzamiento de apps por **UUID** con fallback a app-IDs legacy, branding
  Artemis + protocolo `art://`, builds de dev automáticos por plataforma.
- **Fase 4 (en progreso, MAYORMENTE SIN HACER):** "server-side permission viewing" (hecho);
  **orden custom de apps (planned)** e **input-only streaming mode (planned)** — nunca aterrizaron.
- Plataformas declaradas: Windows (x64/ARM64), macOS (universal), Linux (AppImage, Flatpak, Steam Deck).

## 2. Qué NO hace / fuera de scope
- Orden custom de apps e **input-only / remote-control mode**: prometidos en Fase 4, **sin implementar**.
- **YUV 4:4:4 solo con Sunshine/Apollo** (no GameStream de NVIDIA) — y encima crashea (ver #55).
- Steam Link: en la descripción pero sin builds ("¿Steam link builds?" #59, sin responder).
- Per-host profiles/settings: pedido (#67), la versión Android lo tiene, el cliente PC no.

## 3. A MEDIO COCER / bugs abiertos (lo CLAVE)
Prioriza estos al mantener Selene — ninguno tiene fix mergeado en upstream:

- **PR #64 — `fix: use deep copy for SSL key to prevent use-after-free`** (abierto 2026-05-24, autor
  externo `7ayun`, **SIN mergear**). Fix de memory-safety en el manejo de la SSL key; el autor dice
  que replica una corrección ya aplicada en moonlight-qt upstream. **use-after-free real, sin revisar
  → candidato #1 a portar a Selene.**
- **#55 — WIN: ArtemisQT crashea al activar YUV 4:4:4** (abierto 2025-12-08, Windows). Solo funciona
  con Sunshine; sin root-cause ni fix. Crash duro por formato de color.
- **#23 — AV1 decode no funciona (build Flatpak)** (abierto 2025-08-06, **20 comentarios**, el hilo más
  vivo). Falla en Steam Deck/SteamOS/CachyOS/Nobara/Bazzite: `bwrap: Can't mkdir /app/lib/GL:
  Read-only file system` + `ldconfig failed 256` = restricción del sandbox Flatpak. El binario portable
  funcionaba (sin HDR); Flatpak nunca. Owner consiguió hardware de prueba pero **sin fix** (últ. act.
  2026-02-17).
- **#60 — Crashes aleatorios en Windows 11** (RTX 3060 Mobile, abierto 2026-01-24, sin respuesta).
- **#62 — No arranca en Linux Mint: falta `SDL2_ttf`** (2026-02-22). Dep no bundleada. (Eco de nuestra
  propia receta headless de Selene, que también tuvo que traer SDL2_ttf por prefix privado.)
- **#66 — El portable amd64 no guarda settings al reiniciar** (2026-06-14); el installer sí. Persistencia
  rota en el build portable.
- **#65 — Refresh rates fraccionales no se aceptan** (2026-06-14): la UI no toma decimales tipo 59.94
  — justo la feature estrella de Fase 2 a medio cocer.
- **#51 — Clipboard file-sync no funciona** (2025-11-16): la sync de archivos del portapapeles no jala
  pese a estar activada. Otra feature de Fase 1 incompleta.
- **#61 — No compila en rk3588 con ffmpeg-rockchip** (ARM SoC, 2026-02-02): linkeo/qmake.
- **#58 — Install issues en Ubuntu 24.04** (marcado "solved" por el user, compat Qt/GL/libs).
- **#56 — Reserved key commands** (feature request, teclas reservadas para el sistema local).

## 4. ESTADO DE VIDA — efectivamente INACTIVO (confirmado con fechas)
Jordi sospechaba que está muerto: **confirmado como dormido/abandonado en la práctica.**
- **Último commit: 2025-08-31** (merge PR #44, ajustes de build AppImage/Flatpak). **~11 meses sin
  código al 2026-07-25.**
- El maintainer **`wjbeckett` sigue respondiendo esporádicamente pero NO commitea.** En **#49 "You
  still working on this?"**: contestó **2025-11-07** "I am, or I intend to. I have just been busy with
  work and home life". Desde entonces, cero commits.
- El hilo lo confirma: **2026-06-17** un user nota "the last update was 10 months ago"; **2026-07-10**
  otro (`xCISACx`) reporta que la versión actual "crashes a lot, which makes it sadly unusable" y que
  abrió un issue sin recibir respuesta.
- Señal fuerte: un **PR de seguridad (use-after-free, #64) lleva desde mayo 2026 sin mergear.** Un
  maintainer activo no deja pudrir un fix de memory-safety 2 meses.
- **Veredicto:** intención declarada de seguir, pero **de facto inactivo** — no cierra issues, no
  mergea PRs, no commitea desde ago-2025. Para Selene: no esperes fixes de upstream; portamos nosotros.

## 5. Relación con Moonlight-Qt upstream
- Artemis = fork directo de **`moonlight-stream/moonlight-qt`** (Moonlight Team), rebrandeado, con las
  features Artemis encima para emparejar con Apollo (host de ClassicOldSong).
- Divergencia: Artemis añade clipboard/server-commands/OTP/quick-menu/virtual-display/UUID-launch; el
  core de decode/render sigue siendo el de Moonlight-Qt.
- **PRs de Moonlight-Qt pendientes de portar:** el fix del SSL key use-after-free (#64) explícitamente
  "ya está en moonlight-qt upstream" y aquí sigue sin aplicar → hay drift de seguridad/fixes entre
  Artemis y su upstream Moonlight que Selene debería reconciliar (`git fetch upstream` desde
  moonlight-qt, no solo desde artemis, que está congelado).

## Fuentes
- API repo/issues/PRs/commits: `https://api.github.com/repos/wjbeckett/artemis` (barrido 2026-07-25).
- Issues citados: #23, #49, #51, #55, #56, #58, #59, #60, #61, #62, #65, #66, #67; PR #64.
- README `develop`: `https://github.com/wjbeckett/artemis/blob/develop/README.md`.
- Contexto Apollo↔Artemis: `github.com/ClassicOldSong/Apollo/issues/937`; alternativeto.net.
