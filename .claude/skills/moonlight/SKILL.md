---
name: moonlight
description: El PROTOCOLO Moonlight (moonlight-stream + la lib compartida moonlight-common-c) — el contrato del wire que Helios, Wolf y Selene deben hablar IDÉNTICO o se rompe la compatibilidad. Handshake RTSP, pairing PIN+cert, control ENet/AES-GCM (SS_ENC_CONTROL_V2 IV=seq), input, video RTP+FEC, códecs y extensiones Sunshine (SCM_*), + cambios de protocolo a vigilar (LTR #120). Carga al tocar moonlight-common-c, pairing, cifrado, códecs o cualquier compat host↔cliente.
---

# Moonlight — el CONTRATO del protocolo que Helios, Wolf y Selene deben hablar idéntico

> Este skill es el **guardián de la COMPATIBILIDAD entre los 3 forks**. `moonlight-common-c` es
> el submódulo compartido = literalmente el contrato del wire. Un cambio ahí, o una divergencia de
> cifrado/flags/códecs, impacta a los tres → o evolucionan alineados o se rompen. Ante cualquier duda
> sobre "qué debe hacer el host / qué espera el cliente", **la fuente de verdad es el código de
> `moonlight-common-c/src/`**, no la memoria.

## 1. Qué es

**Moonlight** = implementación open-source del protocolo **GameStream** de NVIDIA (streaming de
juegos/escritorio: 4K, HDR, 120 FPS). Host de referencia hoy = **Sunshine** (open-source, hecho *para*
Moonlight). Genealogía de Jordi: **Moonlight-Qt → Artemis → Selene** (cliente); **Sunshine → Apollo →
Helios** (host); **Wolf** (games-on-whales, host headless, ya listado oficialmente como host Moonlight).

**`moonlight-common-c`** = la lib C con el core GameStream client code, **compartida por TODOS los
clientes Y submódulo de Helios/Selene/Wolf**. Es el contrato del protocolo en código.

## 2. El CONTRATO del protocolo (lo que los 3 forks replican al bit)

**Puertos fijos** (`Limelight.h` `ML_PORT_*`): TCP **47984** (HTTPS pairing/control), **47989** (HTTP),
**48010** (RTSP). UDP **47998/47999/48000/48010** (**video/control/audio**/msg — `ConnectionTester.c`:
47998=video, 47999=control, 48000=audio; 48000 lleva también la ping RTSP de GFE 3.22), **48002** (guía internet).
**5353/UDP** = mDNS (descubrimiento LAN).

**Handshake / arranque** (`Limelight.h` `STAGE_*`, en orden):
`PLATFORM_INIT → NAME_RESOLUTION → AUDIO_STREAM_INIT → RTSP_HANDSHAKE → CONTROL_STREAM_INIT →
VIDEO_STREAM_INIT → INPUT_STREAM_INIT → CONTROL_STREAM_START → VIDEO_STREAM_START → AUDIO_STREAM_START
→ INPUT_STREAM_START`. Un fallo de encoders/KMS en el host revienta típicamente en las etapas
`*_STREAM_START` (síntoma "probing failed").

**RTSP handshake** (`RtspConnection.c`, puerto 48010, `CSeq` incremental):
`OPTIONS → DESCRIBE → SETUP×3 (audio/video/control) → ANNOUNCE → PLAY`. (OJO: `STAGE_MAX=12` es el
enum de *etapas de conexión*, NO el conteo de mensajes RTSP — no es un "handshake RTSP de 12 etapas".)
- **DESCRIBE** ← host devuelve SDP (config Opus multistream, canales/surround, puertos de servidor).
- **SETUP** → cliente fija transporte + rango de puerto (`X-GS-ClientPort=…`).
- **ANNOUNCE** → cliente manda su SDP de video como `application/sdp`; **aquí se negocian los
  feature-flags de cifrado** (`SdpGenerator.c`).
- Transporte **TCP estándar** o **ENet** (GFE viejo). RTSP puede ir cifrado con AES-GCM (feature nuevo,
  `corever=1`, ver §2 matriz).

**Pairing** (HTTP 47989 / HTTPS 47984): por **PIN** — el usuario mete en el cliente el PIN que muestra
el host; se intercambian y fijan **certificados self-signed** que autentican sesiones futuras. Gotcha
conocido del ecosistema: **pairing por CN del cert en LAN** (ver `deploy-streaming-y-resolucion.md`).

**Control stream** (`ControlStream.c`) — sobre **ENet BUNDLEADO obligatorio**:
- Corre sobre la **versión específica de ENet bundleada como submódulo** (parches IPv6 + retransmisión).
  ⚠️ **Linkear a otra libenet CRASHEA** al conectar con GFE reciente → Helios/Wolf deben compilar ESE
  ENet, jamás el del sistema.
- **`SS_ENC_CONTROL_V2` (=0x01, `Limelight-internal.h`) = AES-GCM** con **tag 16 bytes** e **IV 12 bytes
  derivado del `seq`** (número de secuencia monotónico, `unsigned int` del header del paquete). Header
  versionado `NVCTL_ENET_PACKET_HEADER_V2`. Se habilita SIEMPRE que el host lo soporte. *(Es lo ya
  minado en código: AES-GCM, IV=seq.)*

**Input** (`InputStream.c`): **SIEMPRE cifrado** (no opcional). Desde Gen 7 = **AES-GCM**, con
`remoteInputAesKey`/`remoteInputAesIv` (16 bytes) que **DEBEN coincidir con `rikey`/`rikeyid` de
`/launch` y `/resume`**. Teclado/ratón/gamepad (16 jugadores, force-feedback, motion), touch events
(`LI_FF_CONTROLLER_TOUCH_EVENTS`).

**Audio** (`AudioStream.c`): **Opus** multistream (STEREO/5.1/7.1, hasta 8 canales,
`MAKE_AUDIO_CONFIGURATION`). Cifrable con **AES-CBC** opcional (`CIPHER_FLAG_RESET_IV`).

**Video/Audio transporte**: **RTP/UDP** con colas de reordenamiento + **FEC Reed-Solomon**
(`RtpVideoQueue.c`, `RtpAudioQueue.c`, dir `reedsolomon/`). Depaquetizado en `VideoDepacketizer.c`;
frames IDR/P (`FRAME_TYPE_IDR/PFRAME`) con NALUs SPS/PPS/VPS al frente.

**Matriz de cifrado** (feature-flags negociados en `SdpGenerator.c` / `Limelight-internal.h`):
`SS_ENC_CONTROL_V2 0x01` · `SS_ENC_VIDEO 0x02` · `SS_ENC_AUDIO 0x04`. Cliente pide con `encryptionFlags`
(`ENCFLG_AUDIO/VIDEO/ALL`); se habilita solo lo que host **Y** cliente soportan. **Control + input van
cifrados SIEMPRE que se pueda; video/audio son OPCIONALES** (cifrarlos es caro en HW lento). Versión de
protocolo por query en `/launch`: `&corever=1` (v0 = video-enc + control-v2; v1 = **RTSP encryption**),
en `Connection.c`.

**Códecs** (`Limelight.h` `VIDEO_FORMAT_*` / `SCM_*`):
- **H.264** High (0x0001) + **High 4:4:4 8-bit** (ext. Sunshine).
- **HEVC** Main (0x0100), Main10 (HDR 10-bit), **RExt 4:4:4 8/10-bit** (ext. Sunshine).
- **AV1** Main8/Main10 + **High 4:4:4 8/10-bit** (ext. Sunshine). **AV1 requiere Sunshine** (GFE no).
- Máscaras: `MASK_10BIT 0xAA00`, `MASK_YUV444 0xCC04`. Los **`SCM_*_444` = "Sunshine extension"**
  explícita → **Helios/Wolf deben anunciarlas IDÉNTICAS o el cliente no ofrece 4:4:4/AV1/HEVC-RExt**.
- **HDR**: `SS_HDR_METADATA` + `LiGetHdrMetadata()`; colorspace (Rec601 default), range (Limited default).

## 3. ⚠️ Reglas DURAS de compat (no negociables)

- **NO tocar el ABI de `moonlight-common-c`.** Selene forkea el cliente pero respeta
  `STREAM_CONFIGURATION` (encryptionFlags, remoteInputAesKey/Iv, supportedVideoFormats) tal cual.
- **NO system-linkear ENet.** El ENet bundleado es API/ABI-incompatible con libenet upstream; linkear a
  otro **crashea** contra GFE reciente. Helios/Wolf compilan ESE ENet.
- **Mantener IDÉNTICO el esquema IV/GCM del control-v2** (AES-GCM, **IV=seq 12B + tag 16B**) entre los
  tres forks. Un desalineo rompe el control stream **EN SILENCIO** → aparece como "control stream
  establishment error" tipo qt #1300. Lección del **EVP storm de Wolf**: el cifrado que se desalinea no
  grita, se cae callado.
- **Anunciar los `SS_ENC_*` y `SCM_*` bien** o el cliente cae a claro / rechaza / no ofrece el códec.
- **Input y control cifrados no son opcionales**; la rikey del host DEBE ser la de `/launch`/`/resume`.

## 4. Cambios de protocolo A VIGILAR (el submódulo NO se puede congelar)

`moonlight-common-c` es **MUY activo** → los forks no pueden congelar el submódulo sin quedar atrás.
Al bumpear common-c: **rebasar con un diff de PROTOCOLO, no a ciegas** — hay bumps que solo mueven el
hash (deps: nanors, enet, SIMDe) y otros que **SÍ tocan el wire**.

- **#120 — LTR frames** (abierto 25-dic-2025) ⭐ **el cambio vivo más grande.** Reemplaza Reference
  Frame Invalidation por **Long-Term Reference frames**: el host marca frames LTR en metadata de
  transporte, el **cliente ACKea LTR por el control stream**, el host rota 2 slots LTR y ante pérdida
  re-encoda al LTR de recuperación en vez de pedir IDR. **Cambio coordinado host+cliente.** Ya
  aterrizando: el commit **"LTR ACK control message support"** está en árbol (primera pieza). Diseñado
  backward-compat (host viejo ignora ACKs; cliente viejo cae a RFI) pero **hay que PROBAR la compat**
  contra los 3 hosts. Soporta H.264/HEVC/AV1 y NVENC/VPL/AMF → Helios/Wolf pueden aprovecharlo.
- **#126 — RGB 4:4:4 + tone mapping HDR** (draft): encoding RGB 4:4:4 y mapeo HDR→SDR; toca negociación
  de colorspace (SDP/capabilities).
- **#113 — framerates fraccionarios** (SDP numerador/denominador): cambia la negociación de framerate;
  host y cliente deben coincidir.
- **#123 — protocolo de micrófono** (audio upstream Opus, **AES-128-CBC**, negociación RTSP con fallback
  a plaintext): canal nuevo, el host debe implementarlo para recibir mic. Relacionado **#142** arregla
  AES-CBC con mbedTLS 3.x (padding PKCS7 / IV) — **ojo si Helios/Wolf usan mbedTLS 3.x**.
- **#109** trackpad nativo (canal dedicado), **#137** TLV metadata de mando + LEDs DualSense, **#97**
  Intra Refresh (recuperación de error, convive con #120).
- **qt #1300 — control stream establishment error 2**: regresión en v6.0 (culpa al UDP 47999);
  workaround = volver a v5.0.0. Es la clase EXACTA de fallo que Selene puede heredar contra Helios/Wolf
  si el handshake de control diverge → síntoma canónico de desalineo de cifrado/control.

## 5. Estado de los upstreams

- **`moonlight-common-c`: MUY activo** (commits/PRs cada pocos días, deps al día, propuestas de
  protocolo nuevas). El protocolo sigue evolucionando.
- **`moonlight-qt`: mantenido pero release ESTANCADA** — última estable **v6.1.0 (17-sep-2024)**, casi
  un año sin corte (qt #1711 es el clamor de la comunidad por el estancamiento). Hay merges diarios
  (bumps de common-c y SDL DB) pero no corta versión.
- **La innovación pasa en los FORKS del host** (Apollo, Wolf, Vibepollo/Nonary), no en el upstream de
  release lento → refuerza la tesis "itch primero" de Helios/Selene.

## 6. Referencias

- **Código = contrato real:** `Helios/third-party/moonlight-common-c/src/` (idéntico en
  `Selene/moonlight-common-c/`): `Limelight.h`, `Limelight-internal.h`, `ControlStream.c`,
  `Connection.c`, `InputStream.c`, `SdpGenerator.c`, `RtspConnection.c`, `AudioStream.c`.
- **Investigación:** [[moonlight-doc-sintesis]] (síntesis de doc), [[moonlight-issues-forums]] (barrido
  de issues/PRs/foros, cambios de protocolo).
- **Contexto:** [[forks-helios-selene]], [[security-findings-2026-06.local]], [[deploy-streaming-y-resolucion]],
  [[diseno-headless-multisesion]] (Wolf).
- **Skills hermanos que deben RESPETAR este contrato:** `helios` (host), `selene` (cliente), `wolf`
  (backend headless).
