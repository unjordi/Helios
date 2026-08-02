---
name: moonlight-issues-forums
description: Barrido GitHub/foros de Moonlight (qt + common-c, abuelo de Selene) — features, bugs a medio cocer, y cambios de PROTOCOLO que afectan compatibilidad host-cliente. Fechado 2026-07-25.
metadata:
  type: reference
---

# Moonlight (moonlight-qt + moonlight-common-c) — barrido de issues/PRs/foros

Fecha del barrido: **2026-07-25**. Fuentes: GitHub API de `moonlight-stream/moonlight-qt` y
`moonlight-stream/moonlight-common-c` (issues abiertos por comentarios, PRs abiertos/cerrados,
commits recientes), + búsqueda web (Reddit/docs/blogs). Moonlight-qt = cliente Qt del que
descienden Artemis y **Selene**; `moonlight-common-c` = submódulo del protocolo Moonlight que
comparten cliente (Selene) y hosts (Helios/Apollo, Wolf). **Todo cambio aquí impacta la
compatibilidad de los 3 forks.**

> Nota de acceso: la API pública de GitHub y las páginas de issues fueron accesibles vía WebFetch.
> Algunos hilos largos (#1711, #1300) devolvieron solo el post inicial + metadata, no todos los
> comentarios de maintainers; se anota donde el detalle quedó incompleto.

---

## 1. Qué HACE bien / features maduras del cliente Qt

- **Multiplataforma real**: Windows, macOS, Linux (X11 y Wayland), Steam Link, Steam Deck,
  Raspberry Pi, y ramas embedded (moonlight-embedded). El binario Qt es el "canónico" de escritorio.
- **Códecs**: H.264, HEVC y **AV1** con decode por hardware según plataforma (NVDEC, VideoToolbox,
  VA-API, D3D11VA, MediaCodec…). AV1 ya es de primera clase en el cliente.
- **HDR**: HDR10 streaming, incluido HDR con **software decoding** desde v6.1.0. YUV **4:4:4**
  experimental (v6.1.0) para texto/escritorio nítido.
- **Encriptación de control**: soporta el **control stream cifrado AES-GCM (`SS_ENC_CONTROL_V2`)**
  contra Sunshine v0.22+ — es el mismo terreno donde vive el control stream de Wolf (AES-GCM,
  IV=seq truncado) y el de Helios/Apollo. Ver §4.
- **Gamepad**: SDL GameControllerDB embebido (se actualiza seguido: PRs #1949/#1937 jul-2026),
  rumble, gamepad-as-mouse, hotkeys de salida, hasta 4 mandos.
- **Red/latencia**: FEC Reed-Solomon (nanors), pacing, stats de latencia en overlay, buenos
  defaults de bitrate.

## 2. Qué NO hace / limitaciones conocidas

- **No hay E2E encryption "completa" como opción de UI propia** más allá de lo que negocia con el
  host: el request explícito (#1258) se cerró sin feature dedicada; en la práctica la E2E llega
  vía Sunshine v0.22+ (video+audio+control+setup). Android tuvo E2E antes que Qt.
- **HDR en Linux es frágil** (ver §3): depende de Vulkan/DRMKMS/Wayland y del compositor.
- **Sin super-resolución / upscaling AI** en stable (está como PR gigante #1557, milestone v7.0).
- **Sin trackpad nativo / pen pressure completo** aún (PRs en vuelo, §4).
- **Cadence lenta**: última release estable **v6.1.0 = 17-sep-2024**; casi un año sin release nueva
  al momento del barrido (§5).

## 3. A MEDIO COCER / bugs abiertos / dolor conocido (CLAVE)

### Pairing / conexión con hosts (crítico para Selene↔Helios/Wolf)
- **qt #1300 — "control stream establishment error 2"** (abierto 6-jun-2024). **Regresión en v6.0**:
  falla al establecer el control stream contra Sunshine en LAN con puertos abiertos (mensaje culpa
  al UDP 47999). Workaround del reporter: volver a **v5.0.0**. Es exactamente la clase de fallo que
  Selene puede heredar contra Helios/Wolf si el handshake de control cambia. Estado: sin fix oficial.
- Histórico: fallos de pairing contra hosts no-GFE (Sunshine v0.20 y anteriores) que se arreglaron
  en clientes embedded (moonlight-embedded v2.6.2). Patrón recurrente: los hosts no-GFE
  (Sunshine/Apollo/Wolf) exponen bugs de compat que GFE no tenía.

### AV1 / decoders por plataforma
- **qt #1296 — "Weird decoding glitch with AV1"** (abierto 31-may-2024, milestone v6.0.1). **Solo AV1**
  (H.264/HEVC ok): latencia de decode salta de 1-1.5ms a **60-80ms** en escenas estáticas (mapa de
  Diablo 4) a 1440p120; se cura moviendo la cámara o bajando a 60fps. Cliente Intel N100 (12ª gen)
  vs Sunshine nightly. Bug de decode del lado cliente.
- **qt #1237 — Hardware decoding crashea en ASUS ROG Ally** (AMD 780M): H.264/H.265 se van a negro o
  crashean, forzando software decode. 19 comentarios.
- **qt #546 — L4T decode unit queue overflow** (Jetson TX2/Xavier): a 4K la latencia salta a ~150ms;
  JetPack nuevo segfaultea. Label: bug.
- **qt #1941 — Steam Link crashea al conectar USB Audio Device.**

### HDR en Linux (racimo grande, todos abiertos)
- **#1608** HDR washed-out en Intel UHD 730 con Vulkan (arreglable con DRMKMS pero mata performance).
- **#1310** HDR no funciona en Ubuntu 24.04 Desktop.
- **#1356** HDR toggle grayed-out en iGPU Intel (Vulkan reporta no soportar ST.2084 PQ).
- **#1577** HDR no se activa solo en KDE Wayland.
- **#1875 / brillo Wayland** imagen "sobreexpuesta"/demasiado brillante en Wayland tras update; X11 lo
  evita. **#1454** HDR passthrough con colores lavados.
- Patrón: el renderer Vulkan + Wayland + drivers Intel/AMD es el punto flaco de HDR en Linux — muy
  relevante porque el deploy de Jordi es CachyOS/KDE-Wayland con AMD iGPU + NVIDIA.

### Estabilidad / stutter (los issues más comentados, históricos)
- **#159** stutter periódico en macOS (156 comentarios), **#753** stutter M1 Pro (48c),
  **#1152** jitter extremo en Steam Deck hasta hacer alt-tab (44c), **#954** freezes intermitentes
  ligados a HAGS del host (25c). Son crónicos y multi-causa (red + decode + compositor).

### Input / UX
- **#128** sin ajuste de sensibilidad de mouse (24c, enhancement). **#812** el hotkey de salida por
  mando también abre settings. **#1247** falta pen pressure. **#879** scaling roto en Steam Deck
  Gaming Mode con display externo (Wayland). **#619** el proceso no sale al perder conexión (Linux).

## 4. Cambios de PROTOCOLO recientes o pendientes en `moonlight-common-c` (CRÍTICO para compat)

Estado del submódulo compartido. Los hosts (Helios/Apollo, Wolf) tienen que implementar el LADO
servidor de cualquiera de estos para no romper a Selene, y viceversa.

**Ya mergeado / en árbol (jul-2026):**
- **"LTR ACK control message support"** — commit reciente en common-c que añade el mensaje de control
  para ACK de frames LTR. Es la **primera pieza del protocolo LTR** (ver abajo) entrando al árbol.
- Mantenimiento pesado de deps que mueve el hash del submódulo (afecta a Selene al hacer bump):
  nanors (#143/#145), enet (#144), fixes de compilación 3DS/Xbox NXDK (#140), SIMDe. moonlight-qt ya
  bumpeó common-c dos veces en julio (qt #1932/#1940/#1948). **Selene debe rebasar su submódulo con
  cuidado**: estos bumps cambian el hash sin cambiar el protocolo, pero el LTR ACK sí lo cambia.

**Propuestas ABIERTAS que cambiarían el protocolo (a medio cocer):**
- **common-c #120 — LTR frames para superar RFI** (abierto **25-dic-2025**). Reemplaza Reference Frame
  Invalidation por **Long-Term Reference frames**: el host marca frames LTR en metadata de transporte,
  el cliente **ACKea LTR por el control stream**, el host rota 2 slots LTR y ante pérdida re-encoda
  refiriendo al LTR de recuperación en vez de pedir IDR. **Exige cambios coordinados host+cliente**
  pero se diseñó backward-compatible (host viejo ignora ACKs; cliente viejo cae a RFI). H.264/HEVC/AV1
  y NVENC/VPL/AMF lo soportan → **Helios/Wolf podrían aprovecharlo**. NO menciona explícitamente
  compat con Sunshine/Apollo/Wolf. **Vigilar**: es el cambio de protocolo vivo más grande.
- **common-c #126 — RGB colorspaces + tone mapping explícito** (draft, 5c): encoding RGB 4:4:4 y
  mapeo HDR→SDR. Toca negociación de colorspace (SDP/capabilities).
- **common-c #113 — framerates fraccionarios** (SDP con numerador/denominador). Cambia la negociación
  de framerate; host y cliente deben coincidir.
- **common-c #123 — protocolo de micrófono** (audio upstream Opus, **AES-128-CBC**, negociación RTSP con
  fallback a plaintext). Canal nuevo → host debe implementarlo para recibir mic. Relacionado: #142
  arregla AES-CBC con mbedTLS 3.x (padding PKCS7 / IV) — **ojo si Helios/Wolf usan mbedTLS 3.x**.
- **common-c #109 — trackpad nativo**: canal dedicado con gestos multitouch (reemplaza la conversión a
  mouse). Requiere soporte en el host.
- **common-c #137 — TLV metadata en llegada de mando + LEDs (DualSense)**: passthrough de firmware
  DualSense y forwarding de estado de LED (player LED, mic LED). Extiende el canal de input.
- **common-c #97 — Intra Refresh** y **#120** son ambos sobre recuperación de error; conviven.
- **common-c #93** subir timeout de primer frame (10s→30s), **#132** desactivar WLAN AutoConfig en
  streaming (Windows), **#98** fix IPv6-only/Tailscale 464XLAT, **#100** atomics de
  ConnectionInterrupted/LBQ (thread-safety). No cambian el wire pero afectan robustez de conexión.

**Encriptación de control (lo que ya vimos en Wolf):**
- El control stream cifrado **`SS_ENC_CONTROL_V2` (AES-GCM)** es el estándar actual contra Sunshine
  v0.22+; cubre teclado/mouse/gamepad y ahora mic (#123). Coincide con lo minado de Wolf (AES-GCM,
  **IV = número de secuencia truncado**). El request de "E2E completa" (#1258) se cerró: no hubo
  feature separada, la E2E la aporta el host. **Implicación para los 3 forks**: Selene ya habla
  AES-GCM control v2; Helios (de Apollo/Sunshine) y Wolf deben mantener EXACTAMENTE el mismo esquema
  de IV/GCM o el control stream se rompe silenciosamente.

## 5. Dirección del proyecto (¿activo? cadencia)

- **moonlight-common-c: MUY activo** (jul-2026): commits/PRs cada pocos días, deps al día, y
  propuestas de protocolo nuevas (LTR #120 de dic-2025, RGB #126, mic #123). El protocolo Moonlight
  sigue evolucionando → los forks NO pueden congelar el submódulo sin quedar atrás en features/fixes.
- **moonlight-qt: mantenido pero con release estancada.** Última estable **v6.1.0 (17-sep-2024)**;
  el master acumula ~1 año de cambios sin cortar release. **qt #1711 — "Publish a release in 2025/2026"**
  (milestone v6.2, 25 comentarios) es un clamor de la comunidad por ese estancamiento. Actividad de
  merge diaria existe (bumps de common-c y SDL DB en julio-2026), pero el corte de versión no llega.
  *(El detalle de por qué se traba lo dan comentarios de maintainers que el fetch no capturó completo.)*
- **Ecosistema divergente**: hay forks del host (Vibepollo/Nonary, Apollo, Wolf) empujando features
  que moonlight-qt stable no tiene → refuerza la tesis "itch primero" de Helios/Selene: la innovación
  está pasando en los forks, no en el upstream de release lento.

---

## Implicaciones directas para Helios/Selene/Wolf

1. **LTR (#120 + commit LTR-ACK)** es EL cambio de protocolo a vigilar: si el common-c de Selene lo
   incorpora, Helios/Wolf necesitan el lado servidor (o al menos ignorar ACKs limpiamente). Backward-
   compat está diseñada pero hay que probarla contra los 3 hosts.
2. **Bumps del submódulo mueven el hash sin tocar wire** (deps) o **SÍ tocan wire** (LTR-ACK, futuros
   mic/RGB/framerate). Selene debe rebasar common-c con un diff de protocolo, no a ciegas.
3. **AES-GCM control v2 / IV-por-secuencia**: mantener idéntico entre Selene, Helios y Wolf; ya
   documentado en Wolf. Un desalineo de IV/GCM = "control stream establishment error" tipo #1300.
4. **HDR Linux (Vulkan/Wayland/KDE) es zona minada** justo en el stack de Jordi (CachyOS/KDE-Wayland,
   AMD iGPU + NVIDIA): #1608/#1356/#1577/#1875 son candidatos a reproducir en Selene.
5. **AV1 decode glitches (#1296) y crashes por GPU (#1237/#546)**: heredables por Selene; probar AV1
   contra Helios/Wolf en el hardware real antes de declararlo listo.
