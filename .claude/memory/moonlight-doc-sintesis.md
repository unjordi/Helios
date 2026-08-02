---
name: moonlight-doc-sintesis
description: Síntesis de doc de Moonlight (moonlight-stream: qt + common-c, abuelo de Selene) — el PROTOCOLO Moonlight (pairing/RTSP/control/RTP/encriptación), arquitectura del cliente, compatibilidad. Referencia crítica del ecosistema Helios+Wolf+Selene.
metadata:
  type: reference
---

# Moonlight — síntesis de documentación (el ABUELO de Selene y del protocolo Helios/Wolf)

> Investigación 2026-07-25. Fuentes citadas al pie. Lo CLAVE (lo que Helios, Wolf y Selene
> deben hablar IDÉNTICO) es la §2 (PROTOCOLO). El resto es contexto.

## 1. Qué ES Moonlight y su ecosistema

**Moonlight** es la implementación open-source del **protocolo GameStream de NVIDIA** para
streaming de juegos/escritorio remoto (hasta 4K, HDR, 120 FPS). Nació para hablar con
**GeForce Experience** (GFE, ahora descontinuado por NVIDIA) y hoy su host de referencia es
**Sunshine** (host open-source hecho *para* Moonlight). Es la raíz genealógica de Jordi:
**Moonlight-Qt → Artemis → Selene** (cliente) y el protocolo que **Helios** (fork Sunshine/Apollo)
y **Wolf** (host headless de games-on-whales, ya listado como host compatible en la web oficial)
deben implementar del lado servidor.

**Clientes del ecosistema** (todos comparten `moonlight-common-c`):
- **moonlight-qt** (PC: Windows/macOS/Linux, Steam Link, RPi, ARM, RISC-V, Jetson) ← base de Selene.
- **moonlight-android**, **moonlight-ios** (iOS/tvOS/iPadOS), **moonlight-chrome** (ChromeOS).
- Homebrew: Switch, PS Vita, Wii U, Xbox, LG webOS, Samsung Tizen.

**`moonlight-common-c`** = la lib C con el "core GameStream client code" COMPARTIDO por todos los
clientes. **Es el submódulo que Helios/Selene comparten** → es literalmente el contrato del protocolo.

## 2. EL PROTOCOLO MOONLIGHT (lo que los 3 forks deben respetar) ⭐

### 2.1 Puertos (fijos — `Limelight.h` ML_PORT_*)
- **TCP:** 47984 (HTTPS pairing/control), 47989 (HTTP), 48010 (RTSP).
- **UDP:** 47998, 47999, 48000, 48010 (**video/control/audio**/mensajería — verificado en
  `moonlight-common-c` `ConnectionTester.c:22-40,66-71`: 47998=video, 47999=control, 48000=audio;
  48000 lleva además la ping RTSP que GFE 3.22 exige); **48002** también en la guía de internet.
  **5353/UDP** = mDNS (descubrimiento en LAN).

### 2.2 Handshake / secuencia de arranque (`Limelight.h` STAGE_*, en orden)
`PLATFORM_INIT → NAME_RESOLUTION → AUDIO_STREAM_INIT → RTSP_HANDSHAKE → CONTROL_STREAM_INIT →
VIDEO_STREAM_INIT → INPUT_STREAM_INIT → CONTROL_STREAM_START → VIDEO_STREAM_START →
AUDIO_STREAM_START → INPUT_STREAM_START`. Un fallo de encoders/KMS en el host suele reventar en
las etapas *_STREAM_START.

### 2.3 Pairing (HTTP/HTTPS + certificados)
- El cliente entra por **HTTP 47989 / HTTPS 47984**. Emparejamiento por **PIN**: el usuario mete en
  Moonlight el PIN que muestra el host; se intercambian y fijan **certificados** (self-signed) que
  autentican en sesiones futuras. (Gotcha ya conocido en el ecosistema de Jordi: **pairing por CN del
  cert en LAN**, ver `deploy-streaming-y-resolucion.md` stage #4.)

### 2.4 RTSP handshake (`RtspConnection.c`, puerto 48010)
Secuencia de mensajes con `CSeq` incremental: **OPTIONS → DESCRIBE → SETUP×3 (audio/video/control)
→ ANNOUNCE → PLAY**. (OJO: `STAGE_MAX=12` en el enum es el conteo de *etapas de conexión*, NO el número
de mensajes RTSP — no confundir con un "handshake RTSP de 12 etapas".)
- **DESCRIBE** ← host devuelve SDP (config Opus multistream, canales/surround, puertos de servidor
  vía Transport).
- **SETUP** → cliente fija transporte + rango de puerto cliente (`X-GS-ClientPort=...`).
- **ANNOUNCE** → cliente manda su SDP de video (capacidades) como `application/sdp`; **aquí se
  negocian los feature-flags de encriptación** (`SdpGenerator.c`).
- Soporta transporte **TCP estándar** y **ENet** (GFE viejo). **RTSP puede ir cifrado con AES-GCM**
  (feature nuevo, ver 2.7).

### 2.5 Control stream (ENet + AES-GCM) — `ControlStream.c`
- Corre sobre la **versión ESPECÍFICA de ENet bundleada como submódulo** (parches de IPv6 y
  retransmisión). ⚠️ **Linkear a otra libenet CRASHEA** al conectar con GFE reciente → Helios/Wolf
  deben usar exactamente ese ENet.
- **Encriptación control v2 (`SS_ENC_CONTROL_V2`, `Limelight-internal.h`=0x01):** AES-GCM con
  **tag de 16 bytes**, **IV de 12 bytes derivado del `seq`** (número de secuencia monotónico, un
  `unsigned int` en el header del paquete). Header versionado `NVCTL_ENET_PACKET_HEADER_V2`. Se
  habilita SIEMPRE que el host lo soporte ("low overhead"). *(Esto es lo que Jordi ya vio en código:
  AES-GCM con IV=seq, `SS_ENC_CONTROL_V2`.)*

### 2.6 RTP video/audio + input
- **Video/Audio** por RTP/UDP con colas de reordenamiento/FEC (`RtpVideoQueue.c`,
  `RtpAudioQueue.c`, corrección de errores **Reed-Solomon** → dir `reedsolomon/`). Depaquetizado en
  `VideoDepacketizer.c`; frames marcados IDR/P (`FRAME_TYPE_IDR/PFRAME`) con NALUs SPS/PPS/VPS al frente.
- **Audio:** **Opus** multistream; configs `STEREO`, `5.1`, `7.1` (`MAKE_AUDIO_CONFIGURATION`,
  hasta 8 canales). Audio cifrable con **AES-CBC** (`AudioStream.c`, `CIPHER_FLAG_RESET_IV`).
- **Input (`InputStream.c`):** **remote input SIEMPRE cifrado** (no opcional). Desde **Gen 7
  AES-GCM**; usa `remoteInputAesKey`/`remoteInputAesIv` (16 bytes) que DEBEN coincidir con `rikey`/
  `rikeyid` de las peticiones **`/launch`** y **`/resume`**. Soporta teclado/ratón/gamepad (16
  jugadores, force-feedback, motion), **touch events** (`LI_FF_CONTROLLER_TOUCH_EVENTS`).

### 2.7 Matriz de encriptación (feature-flags negociados — `Limelight-internal.h` / `SdpGenerator.c`)
`SS_ENC_CONTROL_V2 0x01` · `SS_ENC_VIDEO 0x02` · `SS_ENC_AUDIO 0x04`. El cliente pide con
`encryptionFlags` (`ENCFLG_AUDIO/VIDEO/ALL`); se habilita solo lo que host Y cliente soportan.
Racional documentado: **video/audio cifrado es caro** en HW lento, por eso es opcional; **control e
input van cifrados siempre** que se pueda. Versión de protocolo por query param en `/launch`:
`&corever=1` (v0 = video-enc + control-v2; v1 = **RTSP encryption**) — `Connection.c`.

### 2.8 Códecs / formatos de video (`Limelight.h` VIDEO_FORMAT_* y SCM_*)
- **H.264** High (`0x0001`) + **High 4:4:4 8-bit** (extensión Sunshine).
- **HEVC** Main (`0x0100`), **Main10** (HDR 10-bit), **RExt 4:4:4 8/10-bit** (ext. Sunshine).
- **AV1** Main8/Main10 + **High 4:4:4 8/10-bit** (ext. Sunshine). **AV1 requiere Sunshine** (GFE no).
- Máscaras: `MASK_10BIT 0xAA00`, `MASK_YUV444 0xCC04`. Los **`SCM_*_444` están marcados
  explícitamente "Sunshine extension"** → Helios los hereda; Wolf debe anunciarlos igual o el cliente
  no ofrece 4:4:4.
- **HDR:** `SS_HDR_METADATA` + `LiGetHdrMetadata()`; colorspace (Rec601 default) y color range
  (Limited default / Full).

## 3. Arquitectura del cliente Qt (base de Selene)
- **Qt 6.7+** (Qt 5.12+ en Linux) + **FFmpeg 4.0+**; render/entrada vía **SDL**; submódulos
  `moonlight-common-c` + motor mDNS. GPL-3.0.
- **Decode HW** por plataforma (Windows/macOS/Linux): DXVA/D3D11VA, **VideoToolbox**, **VAAPI**,
  NVDEC, etc. (la lib expone `CAPABILITY_DIRECT_SUBMIT`, `CAPABILITY_PULL_RENDERER`,
  `REFERENCE_FRAME_INVALIDATION_{AVC,HEVC,AV1}`, `SLICES_PER_FRAME`).
- Features cliente: HDR, **YUV 4:4:4**, 7.1 surround, multitouch (hasta 10 puntos en Sunshine),
  gamepad 16 jugadores + rumble/motion, modo juego (captura de puntero) vs escritorio remoto,
  passthrough de atajos (Alt+Tab) al host.

## 4. Compatibilidad con hosts
- **GeForce Experience** — protocolo original, **descontinuado** por NVIDIA (Gen ≤7 en el código).
- **Sunshine** — host de referencia open-source; añade las extensiones 4:4:4, AV1, multitouch, etc.
- **Apollo** — fork de Sunshine (padre de **Helios**).
- **Wolf** (games-on-whales, Docker/headless) — **listado oficialmente como host Moonlight** en
  moonlight-stream.org.
- Regla de oro: como todos hablan el MISMO `moonlight-common-c`, **host y cliente evolucionan
  ALINEADOS** o se rompe compatibilidad (ya en el CLAUDE.md del workspace).

## 5. Limitaciones / features "a medio cocer" / caveats
- **ENet bundleado es OBLIGATORIO** (API/ABI incompatible con libenet upstream) → no se puede
  system-link; Helios/Wolf deben compilar ESE ENet.
- **Encriptación de video/audio es OPCIONAL y negociada** (no todos los hosts la soportan; HW lento
  la apaga). El control-v2 e input sí van cifrados. → si Wolf/Helios no anuncian bien
  `SS_ENC_VIDEO/AUDIO`, el cliente cae a claro o rechaza.
- **RTSP encryption (v1/`corever`)** es lo más nuevo del stack de cifrado → verificar que los 3 forks
  la manejen consistente (o quedarse en v0).
- **4:4:4 / AV1 / HEVC-RExt = extensiones Sunshine** (no estándar GFE): si un host no las anuncia
  (`SCM_*_444`), el cliente no las ofrece. Wolf/Helios deben anunciarlas idénticas.
- **HDR + 10-bit** dependen de metadata (`SS_HDR_METADATA`) y del pipeline de decode del cliente;
  frágil entre plataformas.
- **Pairing por cert self-signed / CN en LAN** = fuente conocida de fricción (ver memoria de deploy).

## 6. Relevancia para Helios / Wolf / Selene (qué NO romper)
- **Selene (cliente):** al forkear moonlight-qt, NO tocar el ABI de `moonlight-common-c` ni el ENet
  bundleado; respetar `STREAM_CONFIGURATION` (encryptionFlags, remoteInputAesKey/Iv, supportedVideoFormats).
- **Helios / Wolf (hosts):** deben responder el handshake RTSP idéntico, anunciar los mismos
  feature-flags (`SS_ENC_*`, `SCM_*`), cifrar control-v2 con **AES-GCM IV=seq(12B)+tag(16B)**, input
  con **AES-GCM Gen7** usando la rikey de `/launch`/`/resume`, y servir los puertos fijos. Cualquier
  divergencia en flags/cifrado/códecs = "probing failed" o caída de compatibilidad.
- El submódulo `moonlight-common-c` es el **contrato compartido**: un cambio ahí impacta a los 3 →
  evolucionar alineados (regla ya escrita en `HeliosSelene/CLAUDE.md`).

---
### Fuentes
- **Código local (contrato real):** `Helios/third-party/moonlight-common-c/src/` — `Limelight.h`,
  `Limelight-internal.h`, `ControlStream.c`, `Connection.c`, `InputStream.c`, `SdpGenerator.c`,
  `RtspConnection.c`, `AudioStream.c`, `README.md`. (Idéntico en `Selene/moonlight-common-c/`.)
- **Web:** moonlight-stream.org · github.com/moonlight-stream/moonlight-qt (README) ·
  github.com/moonlight-stream/moonlight-common-c (README) · wiki moonlight-docs (Setup Guide).
- **Memorias relacionadas:** `forks-helios-selene.md`, `deploy-streaming-y-resolucion.md`,
  `diseno-headless-multisesion.md` (Wolf).
