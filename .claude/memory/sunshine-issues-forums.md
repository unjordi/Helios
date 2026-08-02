---
name: sunshine-issues-forums
description: Barrido GitHub/foros de Sunshine (LizardByte, abuelo de Helios) — features, qué no hace, bugs abiertos a medio cocer (foco Wayland/NVIDIA-Blackwell/multi-sesión). Fechado 2026-07-25.
metadata:
  type: reference
---

# Sunshine (LizardByte) — barrido de issues y foros · 2026-07-25

**Linaje:** Helios = fork de Apollo = fork de **Sunshine** (`LizardByte/Sunshine`), el host
Moonlight de referencia. Todo lo que Sunshine hace bien o mal río arriba, Helios lo HEREDA salvo
que Apollo/Helios lo hayan tocado. Repo grande y muy activo; release estable de referencia en este
barrido: **v2026.516.143833** (2026-05-16). Contexto de Jordi: CachyOS/Wayland/KDE + NVIDIA RTX
5070 Ti (Blackwell) + iGPU AMD, captura **KMS** con `cap_sys_admin`, encoda **NVENC**.

> Nota de método: los conteos de comentarios/reacciones y los estados salen de la API de GitHub y de
> las páginas de issue a fecha 2026-07-25; algunos issues viejos pueden haber cambiado de estado
> desde entonces. Los issues del top se ordenaron por comentarios/reacciones.

---

## 1. Qué HACE bien / features maduras

- **Streaming HW-encoded H.264 / HEVC / AV1** a 4K120 con HDR; fallback automático de códec (si
  falla AV1, baja a HEVC/H.264). NVENC, VA-API, QuickSync, AMF, VideoToolbox soportados.
- **Latencia baja** bien configurado: sub-10 ms totales en LAN cableada (data comunitaria); pairing
  por PIN, control de gamepad/mouse/teclado, web UI de configuración.
- **Auto-cambio de resolución y modo HDR** en el host (originalmente Windows; en Linux es más
  frágil — ver §3/§5). Multi-plataforma (Windows/Linux/macOS).
- **Captura Wayland** ya existe por dos vías: `wlr-screencopy` (wlroots) y **XDG Desktop Portals**
  (GNOME/KDE, vía `portal::dbus_t`), además de **KMS** (con cap) y X11/NvFBC. v2026.516 metió
  timestamp de frame por el "ready timestamp" de Wayland y captura event-driven para el portal XDG.
- **Headless / monitor virtual en Wayland**: soporte "oficial-ish" en releases 2026 (aunque en la
  práctica sigue siendo terreno de gists y wrappers de comunidad — ver §2/§5).
- Comunidad y docs razonables (docs.lizardbyte.dev, DeepWiki, blog de releases).

## 2. Qué NO hace / fuera de scope / rechazado

- **Multi-sesión real / multi-usuario simultáneo NO es un objetivo de diseño.** Sunshine expone UNA
  sesión de escritorio. Correr N usuarios aislados requiere hacks (compositor headless wlroots por
  usuario, `vuinputd`, contenedores) y "los devices de Sunshine pisan la otra sesión". La respuesta
  canónica de la comunidad para multi-usuario aislado es **games-on-whales / Wolf** o contenedores
  — exactamente la decisión que ya tomaste con `wolf-deploy/`. Discussions #262 y #770.
- **Negociación por-cliente independiente RECHAZADA.** Issue **#3865 "Second client inherits 120 FPS
  stream from first client"** → **cerrado como *not planned*** (reportado 2025-05-10, RTX 5070 Ti).
  Si dos clientes se conectan, el segundo hereda FPS/settings del primero; no hay pipelines de
  encoding independientes por cliente. Confirma que multi-cliente simultáneo con settings distintos
  está fuera de scope.
- Virtual display en Linux **no es una feature de primera clase estable**: existe pero se apoya en
  drivers/compositores y setups a mano (Sway/Hyprland/GNOME headless, WOL, etc.).

## 3. A MEDIO COCER / bugs abiertos / dolor conocido  ⚠️ (lo clave para Helios)

### Wayland + captura (KMS / portal / KWin) — el terreno de Jordi
- **#4884 "XWayland (KWin Wayland) stuttering"** — *ABIERTO* (2026-03-22). **En CachyOS + KDE
  Plasma/KWin Wayland**, la mayoría de juegos bajo XWayland stutterean fuerte al streamear (probado
  con Cyberpunk 2077). **Afecta TANTO a portal XDG como a KMS**; sospechan cambios de KWin post-6.5/6.6.
  Workaround: `PROTON_ENABLE_WAYLAND=1` (juego nativo Wayland) elimina el stutter en casi todos, con
  glitches de render en algunos títulos. **Mismo stack que Jordi → alto riesgo de heredarlo.**
- **#3203 "Portrait screens in landscape seen as portrait with KMS"** — *ABIERTO*, 45 comentarios /
  8 👍 (el más comentado). Captura KMS en Wayland maneja mal monitores rotados (nacido en Steam Deck).
  Relevante si Jordi tiene monitor en pivote o layout mixto.
- **#3189 "Screen Tearing/VSync Issue in KDE Plasma Wayland / GNOME Wayland"** — *ABIERTO*. Tearing
  pese a VSync activado en escritorio y juego (KDE 6.1.5).
- **#2472 "NvFBC retrieves slightly outdated images"** — *stale/ABIERTO*, 28 comentarios. NvFBC usa
  flag NOWAIT → hasta 16 ms de retraso de frame a 60 fps (latencia extra). Ruta X11/NvFBC, no KMS.
- **#3953 "Flatpak does not work on KDE Wayland"** y **#2215 "0.22 crash on Wayland KDE Plasma"** —
  fricción histórica del combo KDE Wayland (menos relevante si compilas nativo como Helios).

### NVIDIA / Blackwell (RTX 50) / NVENC — el otro terreno de Jordi
- **CUDA no carga → NVENC cae a software.** Patrón MUY común en logs:
  `Cannot load libcuda.so.1` → `Could not dynamically load CUDA` → `Failed to create a CUDA device:
  Operation not permitted` → cae a `libx264`. En Blackwell se enreda con los **módulos de kernel
  ABIERTOS** (obligatorios en RTX 50): apps que no son compatibles con el open kernel module fallan
  CUDA aunque ffmpeg/contenedores encoden bien en la misma máquina. Reportado cruzado en NixOS
  (#272221, #305688) y foros NVIDIA. **Directamente pertinente al NVENC de la RTX 5070 Ti.**
- **#4567 "GPU performance drop tied to stream framerate on Linux"** — *ABIERTO* (2026-01-06).
  **Plasma 6 Wayland, RTX 5090, driver 580.119.02** (Blackwell). La utilización de GPU baja al subir
  el framerate de stream (70% a 144 fps vs 99% sin streamear); ocurre con **KMS y NvFBC**, y con
  **NVENC y software**. Reproducido en NixOS y Bazzite → no es config de una distro. Sin causa raíz.
  Blackwell + Wayland + cualquier captura = degradación de rendimiento sin explicar.
- **#5147 "capture frame rate significantly lower than in-game frame rate"** — *ABIERTO* (2026-05-18).
  Tras actualizar a v2026.516.143833 el FPS capturado deja de seguir al del juego. Reportado en
  **RTX 5080** (Win11) pero es regresión de esa versión — vigilar al bumpear Helios a esa base.
- **#5217 "v2026.516.143833 broke hardware enc on older nvidia cards"** — *ABIERTO* (2026-05-30).
  `NvEncOpenEncodeSessionEx() failed: NV_ENC_ERR_INVALID_VERSION`; cae a software. Afecta tarjetas
  viejas (Quadro P4000) — no la 5070 Ti, pero señala que **la v2026.516 tocó la ruta NVENC y rompió
  cosas**; PR #5451 relacionado. Precaución al rebasear Helios sobre esa release.
- **#3621 "DLSS frame generation 310.1/310.2 only streaming native frame rate"** — *ABIERTO*, 41
  comentarios. Con DLSS FG el cliente ve solo el framerate nativo (mitad). NVIDIA-específico.
- **#5217 / #5147 / #4567 juntos** pintan que la **v2026.516.143833 fue una release con regresiones
  de captura/NVENC** — la misma versión que trae el fix del auth-bypass (§ seguridad). Tensión real
  para Helios: quieres el fix de CVE pero esa base arrastra bugs de encoder.

### Headless / virtual display / NVIDIA
- **#2250 "KMS + Headless + Nvidia: Unknown Monitor connector type [Meta]"** — *ABIERTO*, 42
  comentarios / 16 👍, label *help wanted*. El monitor virtual de GNOME headless se reporta con
  connector "Meta" que el KMS de Sunshine no reconoce → falla la captura. Workaround: usar X11.
  Directamente el problema de "headless + NVIDIA + KMS" que motivó irte a Wolf.

### HDR
- **#3298 "HDR Streaming Very Dark After Plasma 6.2 Update"** — *ABIERTO*, 40 comentarios. Tras
  actualizar KDE Plasma el stream HDR sale muy oscuro / black crush. **KDE Wayland + HDR = frágil.**
- **#3965 "KDE - Prefer color accuracy - 4K - Low fps"** — para que HDR no se vea oscuro en el
  cliente hay que activar "Prefer color accuracy", lo que tira el FPS (~24). Trade-off sin resolver.

### Input / misc
- **#4273 "Incorrect remote desktop mode mouse mapping on Linux X11/i3wm"** — *ABIERTO*, label
  input:mouse. Regresión desde v2025.628.4510: mouse atrapado en el segundo cuadrante en modo
  remote desktop.
- **#4024 "Stream crashes directly after start"** y **#5147** — inestabilidad reportada tras
  updates recientes; síntoma de que el ritmo de releases mete regresiones.

## 4. Roadmap / dirección de LizardByte

- No hay un roadmap público formal muy detallado; la dirección se lee de releases y discussions.
- **2026: empujando captura Wayland y headless.** v2026.516.143833 mejoró el portal XDG
  (event-driven, timestamps de Wayland) y hay "headless monitor support on Wayland" en releases 2026.
- **Multi-usuario/multi-sesión NO está en el roadmap** (ver §2). LizardByte deja ese caso a
  games-on-whales/Wolf explícitamente en discussions.
- **Seguridad tomándose más en serio** tras varios CVE 2025-2026 (abajo): hay política de seguridad
  y advisories publicados, CSRF y auth-bypass parcheados.
- Cadencia de releases rápida con regresiones ocasionales (varios "broke X in version Y" abiertos) —
  la base se mueve rápido y no siempre estable.

### Seguridad / CVEs (Helios ya trackea el #1, aquí el panorama)
- **CVE-2026-32253 — Auth bypass (crítico).** El callback custom de verificación OpenSSL en
  `src/crypto.cpp` trata ciertos errores como éxito → certs no confiables acceden a endpoints HTTPS
  protegidos. Afecta TODO < v2026.516.143833; fix en v2026.516.143833. **Es el CVE que Helios ya
  tiene codificado como test rojo y por el que NO revertir al binario AUR.**
- **CVE-2025-53095 — CSRF en la web UI (CVSS 9.7, crítico).** La web UI no protegía contra CSRF →
  command injection como Administrator. Fix en v2025.628.4510.
- **CVE-2025-54081 — Unquoted Service Path (Windows).** SunshineService con path sin comillas →
  ejecución local como SYSTEM. Solo Windows; irrelevante para el deploy Linux de Jordi.
- Nota de contexto ya en memoria: los puertos abiertos de Helios a Internet son DISEÑO (acceso
  remoto), no falla — pero el auth debe estar sano, de ahí el peso del CVE-2026-32253.

## 5. Lo que Apollo/Helios probablemente HEREDAN como dolor

1. **Fragilidad captura Wayland + KWin (CachyOS/KDE).** El stutter de XWayland (#4884), el tearing
   (#3189) y el HDR oscuro (#3298) nacen del combo KDE Plasma/KWin Wayland — el stack EXACTO de Jordi.
   Helios no los arregla por ser fork; hay que vigilarlos/parcharlos.
2. **CUDA/NVENC en Blackwell con open kernel module.** El `libcuda ... Operation not permitted` y la
   degradación por framerate (#4567, RTX 5090 Blackwell) son de la generación de GPU + drivers, no de
   Sunshine — Helios los hereda tal cual. La `cap_sys_admin` que Jordi ya aplica (por rebuild) es
   parte de este mismo terreno de permisos.
3. **Headless + NVIDIA + KMS = pared (#2250 connector "Meta").** Justo lo que empujó la decisión de
   Wolf para multi-sesión. Sunshine/Helios NO cubren multi-usuario aislado; Wolf sí (ya decidido).
4. **Regresiones de la base v2026.516.143833.** Helios QUIERE esa release por el fix de
   CVE-2026-32253, pero arrastra #5147 (FPS de captura) y #5217 (NVENC roto en tarjetas viejas). Al
   rebasear Helios sobre upstream reciente, auditar la ruta NVENC/captura, no solo compilar verde.
5. **Sin negociación por-cliente (#3865 not planned) y sin multi-sesión** → confirma que el
   ecosistema Helios+Wolf+Selene es el camino correcto: Helios/Sunshine para 1 sesión de escritorio,
   Wolf para lo multi-usuario headless.

---

### Fuentes (consultadas 2026-07-25)
- API/issues GitHub `LizardByte/Sunshine`: #3203, #3298, #2472, #4273, #3621, #3189, #3953, #2215,
  #3965 (Wayland/KMS/HDR); #4884, #4567, #5147, #5217, #2250 (Wayland+NVIDIA/Blackwell/NVENC/headless);
  #3865 (multi-cliente, *not planned*); #4024, #4464.
- Discussions LizardByte #262, #770, #439, #245.
- CVEs: CVE-2026-32253 (auth bypass), CVE-2025-53095 (CSRF), CVE-2025-54081 (unquoted service path);
  advisories GHSA-39hj-fxvw-758m, GHSA-6p7j-5v8v-w45h; github.com/LizardByte/Sunshine/security.
- Blog releases: app.lizardbyte.dev (v2026.516.143833, 2026-05-16). DeepWiki Linux platform impl.
- NixOS nixpkgs #272221, #305688 (libcuda/NVENC). Foros NVIDIA (RTX 50/Blackwell Linux drivers),
  r/MoonlightStreaming (contexto latencia/AV1). techsngames Moonlight guide 2026.
