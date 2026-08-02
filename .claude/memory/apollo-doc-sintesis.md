---
name: apollo-doc-sintesis
description: Síntesis de la documentación de Apollo (ClassicOldSong/Apollo, padre de Helios) — qué añade sobre Sunshine, arquitectura, features, limitaciones. Referencia del ecosistema Helios+Wolf+Selene.
metadata:
  type: reference
---

# Apollo (ClassicOldSong/Apollo) — síntesis de su documentación

> Padre directo de **Helios** (el fork de Jordi). Apollo = fork de **Sunshine** (LizardByte).
> Cadena: Sunshine → Apollo → Helios (host); su cliente hermano es Artemis → Selene.
> Fuentes citadas al pie de cada sección: repo local `Helios/` (que ES Apollo rebrandeado) +
> web upstream `github.com/ClassicOldSong/Apollo` y su wiki.

## 1. Qué ES Apollo
Un **host de game/desktop streaming self-hosted** que habla el **protocolo Moonlight**: baja latencia,
resolución nativa del cliente, capacidades de "cloud gaming server", con **encoding por hardware en
AMD, Intel y NVIDIA** (encoding por software disponible como fallback) y una **web UI** para
configuración y pairing de clientes desde cualquier navegador. Es el host para clientes Moonlight —
en el ecosistema de ClassicOldSong el cliente emparejado es **Artemis** (Moonlight Noir/Android).
Fuente: `Helios/README.md` L5; README upstream de Apollo.

## 2. El DELTA sobre Sunshine (lo más importante)
Sunshine ya hacía captura+encode+protocolo Moonlight. Lo que Apollo AÑADE encima:

1. **Virtual Display integrado con HDR** — crea un display virtual que **auto-empareja
   resolución/refresh/HDR del cliente**, se crea al arrancar el stream y se destruye al terminar.
   En **Windows** usa el driver **SudoVDA** (Virtual Display Adapter) y asigna una **identidad FIJA
   por cliente** (EDID/serial estable) → Windows recuerda la config de display de cada dispositivo
   nativamente. Es la feature estrella de Apollo. **Windows-only hoy; Linux "planned"** (ver §5).
   Fuente: `Helios/README.md` L13, L77-87; README upstream.

2. **Sistema de permisos por-cliente** (granular). Bits de permiso documentados en la wiki:
   - *Action:* `List Apps`, `Launch Apps` (contiene View Apps)
   - *Operation:* `Clipboard Set`, `Clipboard Read`, `Server Command`
   - *Input:* `Controller Input`, `Touch Input`, `Pen Input`, `Mouse Input`, `Keyboard Input`
   - *Viewing:* `View Streams` (contiene List Apps)
   Regla clave: **el PRIMER cliente emparejado recibe TODOS los permisos**; los siguientes solo
   `View Streams` + `List Apps` por defecto → hay que **conceder permisos manualmente** en la web UI
   (p.ej. `Launch Apps`, mouse/teclado) o el nuevo cliente ve *Permission Denied*. Caso de uso:
   compartir juego con un amigo, o control de screen-time para niños.
   Fuente: wiki `Apollo/wiki/Permission-System`; `Helios/README.md` L34-35.

3. **Input-only mode** — modo de solo-input (control remoto sin/con restricción de streams).
   Fuente: `Helios/README.md` L12; README upstream.

4. **Clipboard sync** host↔cliente (gobernado por los permisos Clipboard Set/Read).
   Fuente: `Helios/README.md` L10.

5. **Comandos on-connect / on-disconnect** — ejecutar comandos cuando un cliente conecta/desconecta
   (ej. **auto pause/resume** de juegos). Se apoya en el patrón do/undo de `global_prep_cmd`
   (heredado de Sunshine) exponiendo variables por-sesión.
   Fuente: `Helios/README.md` L11; `docs/configuration.md` L217-236 (`global_prep_cmd`).

6. **Dual-GPU + headless mode** — soporte "seamless" para laptops dual-GPU: fijar `Adapter Name` al
   dGPU + activar `Headless mode` en la pestaña Audio/Video, **sin dummy plug**.
   Fuente: `Helios/README.md` L104; README upstream.

7. **Config del display virtual** (opciones nuevas en la web UI/config, más allá de Sunshine):
   `isolated_virtual_display_option` (aísla el VD / cambia su posición), y las de HDR sobre VDD
   (`dd_hdr_option`, `dd_wa_hdr_toggle_delay` — workaround de color HDR incorrecto en VDD).
   Fuente: `Helios/docs/configuration.md` L1043-1059, L1227-1280.

## 3. Arquitectura y features clave (heredadas + extendidas)
- **Encoders**: NVENC (NVIDIA), VA-API/QuickSync (Intel), VCE/VA-API (AMD), + software. H.264 / HEVC
  Main+Main10 (HDR) / AV1 8/10-bit (HDR) — advertidos por capability flags.
- **Captura (Linux)**: selector en `src/platform/linux/misc.cpp` `display()` prueba
  **NvFBC → Wayland (wlr-screencopy) → KMS (kmsgrab) → X11**. En KDE Wayland el default es
  **wlr-screencopy** (por nombre de output); KMS es fallback (el que usa el deploy real de Jordi con
  `cap_sys_admin`). Fuente: `Helios/docs/design/virtual-display-linux.md` §4.
- **Web UI** en `https://localhost:47990`, primer arranque pide usuario/contraseña; pairing por PIN.
- **Config** en `src/config.{h,cpp}` (`video_t`); multi-instancia soportada (wiki).

## 4. Compatibilidad con el protocolo Moonlight y clientes
Apollo **habla Moonlight estándar** → compatible con cualquier cliente Moonlight, pero las features
propias (VD on-demand, controles de resolución) lucen mejor con **Artemis** (Android, con controles de
virtual-display integrados) y con **Selene** (el cliente de Jordi). El soporte de VD se **anuncia por
capability flag** en `nvhttp.cpp`; hoy ese flag está dentro de `#ifdef _WIN32` → clientes contra un host
**Linux** ven *"server doesn't support virtual displays"*. Host y cliente deben evolucionar ALINEADOS
(comparten `moonlight-common-c`) o se rompe el handshake. Caveat de la FAQ: en headless el capability
probing ocurre en la **primera conexión**, no al arranque.
Fuente: `Helios/docs/design/virtual-display-linux.md` §2, §4; wiki FAQ.

## 5. Limitaciones / caveats / lo experimental (que la doc admite)
- **Virtual Display es Windows-only**; en **Linux es no-op**: `virtual-display: true` no hace nada y el
  capability nunca se anuncia. `create_settings_manager()` devuelve **`nullptr`** en Linux. El stopgap
  actual es un **`gamescope` dedicado capturado por KMS** (con efectos secundarios: cambia el modo del
  monitor físico, flicker de 240 Hz+HDR por re-drivear el link físico, scripts de `kscreen-doctor`).
  Fuente: `docs/design/virtual-display-linux.md` §1-2.
- **HDR es messy/experimental**: si se ve oscuro/lavado normalmente es el cliente tone-mapeando SDR;
  SDR da color más estable. En Windows requiere Win11 23H2+ (24H2 recomendado); en Linux/KDE la captura
  HDR es sensible a la versión del compositor (regresiones dark-HDR tras updates de Plasma).
  Fuente: `Helios/README.md` L89-100 (Apollo issue #164).
- **Caveats de VD (FAQ)**: NUNCA rotar la pantalla en un VD (rompe el resolution-matching); las entradas
  de Virtual Display **deshabilitan todos los comandos** como safety; Pixel y otros pueden no soportar
  resolución nativa sin ajuste manual. Fuente: wiki FAQ.
- **Trabajo a medio cocer upstream (issues abiertos que Helios rastrea)**: VD en Linux
  (#1161 diseño, **PR #1477** EDID-override experimental de `AdivonSlav`), OpenGL-sobre-VD (#1414),
  headless VM (#1427). El maintainer de Apollo prefiere "backends pluggables"; la solución sin tocar
  kernel es **EDID-override vía debugfs** (reusa kmsgrab, sin módulo de kernel, necesita un connector
  DRM físicamente desconectado libre — no sirve para VMs headless).
  Fuente: `docs/design/virtual-display-linux.md` §3, §9; `Helios/README.md` L19.

## 6. Relevancia para el ecosistema de Jordi (Helios + Wolf + Selene)
- **Helios = host real** (Apollo rebrandeado) corriendo en CachyOS + NVIDIA Blackwell, NVENC, captura
  KMS con `cap_sys_admin`. Todo el delta de Apollo (permisos por-cliente, clipboard, comandos
  connect/disconnect, input-only, dual-GPU/headless) es lo que Helios HEREDA y quiere mantener.
- **El foco propio de Helios** = lo que Apollo dejó a medias: **virtual display NATIVO en Linux/Wayland**
  (paridad con SudoVDA de Windows), adoptando PR #1477 (EDID-override) + un hook `pre_probe_cmd`
  pluggable (patrón Sunshine #4762) que **formaliza el hack gamescope/kscreen de Jordi** como feature
  de primera clase. Es exactamente la frustración #1/#2 del plan de forks.
- **Wolf = headless multi-usuario** cubre justo el caso que EDID-override NO puede (sin connector libre,
  multi-sesión aislada) → complementa a Helios, no compite. Apollo/Helios sirve el
  "remote-desktop / continuar donde lo dejé" (sesión NO aislada); Wolf sirve el multi-user aislado.
- Caveat operativo ya conocido: al revertir Helios al binario AUR `apollo` se pierde el fix
  CVE-2026-32253 — no revertir sin ese parche.

---
Archivo: `/home/unjordi/code/HeliosSelene/.claude/memory/apollo-doc-sintesis.md`
