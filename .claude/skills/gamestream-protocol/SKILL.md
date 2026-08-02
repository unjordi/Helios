---
name: gamestream-protocol
description: El wire protocol NVIDIA GameStream a nivel BITS, tal como lo implementa moonlight-common-c (el submódulo compartido de Helios/Selene/Wolf) — headers de paquete, esquemas de IV por stream (control-v2 AES-GCM IV=seq 12B con bytes de dominio, video IV explícito, input GCM con ratchet NVIDIA, audio CBC), handshake RTSP etapa-por-etapa, FEC Reed-Solomon, tablas de tipo por generación, y el corte NVIDIA-original vs extensiones Sunshine/Apollo. Es la CAPA PROFUNDA del skill `moonlight` (que es el contrato de alto nivel). Carga al debuggear cifrado/EVP, el control stream, formatos de paquete, FEC, o al portar wire-level entre forks.
---

# GameStream (NVIDIA) — el wire protocol reverse-engineered, a nivel bits

> **Relación con el skill `moonlight`:** `moonlight` es el **contrato de ALTO NIVEL** (puertos, orden
> del handshake, qué feature-flag hace qué, cambios de protocolo a vigilar como #120 LTR). ESTE skill
> es la **CAPA PROFUNDA**: los headers de paquete exactos, los esquemas de IV por stream, y las citas
> archivo:línea de moonlight-common-c. Carga `moonlight` para "¿qué debe hacer el host?"; carga ESTE
> para "¿por qué se cae el cifrado / cómo es EXACTAMENTE el paquete?". No se duplican: uno es el mapa,
> el otro el plano eléctrico. **Fuente de verdad = el código.** La referencia completa vive en
> [[nvidia-gamestream-protocol]]. Rutas: `Helios/third-party/moonlight-common-c/src/` (idéntico en
> `Selene/moonlight-common-c/moonlight-common-c/src/`).

## 1. Qué ES GameStream (de dónde sale)

Protocolo propietario de NVIDIA para **GeForce Experience (GFE)** / SHIELD. NVIDIA nunca lo documentó
→ **Moonlight lo reverse-engineó** del tráfico. `moonlight-common-c` = "core GameStream client code"
(README.md). Sunshine apareció como host open-source que lo habla y lo **extendió**. Helios (host) y
Wolf (host) deben servir ESTE wire idéntico al que Selene (cliente) espera.

**Detección de sabor** (`Limelight-internal.h:86`): `IS_SUNSHINE()` = `AppVersionQuad[3] < 0`.
La **generación** (`AppVersionQuad[0]` = 3/4/5/7) selecciona tablas de paquete: Gen3/4 = TCP control
(GFE viejo), Gen5+ = ENet, Gen7 = GFE 3.x / Sunshine.

## 2. Puertos (proto POR puerto) — `ConnectionTester.c:53-79`

TCP **47984** HTTPS pairing · **47989** HTTP serverinfo/launch · **48010** RTSP.
UDP **47998** VIDEO(RTP+FEC) · **47999** CONTROL(ENet) · **48000** AUDIO(RTP) *+ ping RTSP que GFE 3.22
exige* · **48010** RTSP-sobre-ENet/mic. Los de A/V/control se re-confirman en RTSP SETUP.

## 3. Handshake RTSP — `RtspConnection.c`, SDP en `SdpGenerator.c`

`OPTIONS → DESCRIBE → SETUP(audio/video/control) → ANNOUNCE → PLAY`, `CSeq` incremental.
- **DESCRIBE** → el cliente parsea de la respuesta del host `x-ss-general.encryptionSupported` y
  `encryptionRequested` (`:1136-1140`) = las capacidades de cifrado.
- **SETUP** → `Transport: unicast;X-GS-ClientPort=50000-50001`; respuesta trae `X-SS-Ping-Payload`,
  `X-SS-Connect-Data`.
- **ANNOUNCE** → el cliente SUBE su SDP (`getSdpPayloadForStreamConfig`); **aquí se fija la
  encriptación efectiva y los códecs**. Header literal `s=NVIDIA Streaming Client` (herencia GFE).
- `useEnet` (Gen5-7, `AppVersionQuad[2]<404`) usa `rtspru://` sobre ENet en vez de TCP.

## 4. Encriptación — UNA key, muchos IV (el CRUX del "EVP storm")

**Todo el streaming cifra con `StreamConfig.remoteInputAesKey` (16B)** = el `rikey` de `/launch`/`/resume`.
Lo que cambia es el esquema de IV. Motor: `PlatformCrypto.c` (OpenSSL EVP / mbedTLS).

- **Control-v2** (`SS_ENC_CONTROL_V2 0x01`, `ControlStream.c:546-661`): AES-GCM, header
  `{u16 type=0x0001; u16 len; u32 seq}` + **tag 16B** + ciphertext. **IV 12B** = `seq` LE en `iv[0..3]`
  + **bytes de dominio** anti-colisión: cliente `iv[10]='C',iv[11]='C'`; host `'H','C'`. Old-style
  NVIDIA = **IV 16B** con `iv[0]=(uint8_t)seq` (cast truncante, hay que imitarlo). Se activa en
  `APP_VERSION_AT_LEAST(7,1,431)`.
- **RTSP-enc** (`corever=1`, `RtspConnection.c:92-140`): GCM, IV 12B, dominio `'C'/'H','R'`, tag 16B en
  header BE.
- **Input** (`InputStream.c:160-291`): Gen7 = GCM con `currentAesIv` 16B que **ratchea desde los
  últimos 16B del ciphertext previo** (quirk NVIDIA obligatorio). Pre-Gen7 = CBC+PKCS7. Con control
  encriptado, input va en claro por el control stream y ESE lo cifra.
- **Video** (`Video.h:15-19`): `ENC_VIDEO_HEADER {u8 iv[12]; u32 frameNumber; u8 tag[16]}` — **IV
  explícito por paquete** (host→cliente, masivo).
- **Audio** (`AudioStream.c:178-192`): AES-**CBC**, IV 16B = `BE32(avRiKeyId+seq)`, RESET_IV+FINISH.

**Por qué "EVP storm":** GCM en OpenSSL (`PlatformCrypto.c:33-35,142-228`) sólo re-inicializa el
contexto si `!initialized || RESET_IV` (RESET_IV **sólo** al cambiar el LARGO de IV); **cambiar la key
sobre un contexto vivo NO está soportado** → por eso hay `encryptionCtx`/`decryptionCtx` separados. Si
un fork mezcla contextos, cambia 12B↔16B sin RESET_IV o reusa la key mal → EVP falla en cascada y el
control stream **cae EN SILENCIO** (`PltDecryptMessage` devuelve false, se descarta el paquete). Ese
desalineo es la lección: **el cifrado que se desalinea no grita, se cae callado**.

Negociación (`SdpGenerator.c:276-303`): control-v2 siempre que el host lo soporte; video/audio sólo si
host Y cliente los piden (`ENCFLG_*`); si el host los REQUIERE y el cliente no, se cifra igual con
warning.

## 5. Video RTP + FEC Reed-Solomon — `Video.h`, `RtpVideoQueue.c`, `reedsolomon/rs.c`

RTP 12B fijos (`{u8 header; u8 ptype; u16 seq; u32 ts; u32 ssrc}`) + `NV_VIDEO_PACKET`
(`{u32 streamPacketIndex; u32 frameIndex; u8 flags; u8 rsvd; u8 multiFecFlags; u8 multiFecBlocks;
u32 fecInfo}`). Flags `SOF/EOF/CONTAINS_PIC_DATA`. **FEC = Reed-Solomon** (lib `nanors` vendored),
recovery desde Gen5. SDP: `fec.enable=1` siempre, `repairPercent` 5%(4K)/20%, `minRequiredFecPackets=2`,
`bllFec.enable=0`. **FEC trabaja en chunks de 16B** → `packetSize` múltiplo de 16 (`Connection.c:293-295`).

## 6. Control channel — canales ENet + tablas por generación — `ControlStream.c`

Canales ENet (`Limelight-internal.h:57-67`): 0=generic, 1=urgent(IDR/RFI), 2=kbd, 3=mouse, 4=pen,
5=touch, 8=serverctl, 0x10-1F=gamepad, 0x20-2F=sensor. Tipos de paquete por gen en
`packetTypesGen3/4/5/7/7Enc` (`:149-238`). **RFI**: `connectionDetectedFrameLoss` → tuplas por canal
urgente → si desborda, IDR completo. **#120 LTR** lo reemplazará (ver skill `moonlight` §4).

## 7. NVIDIA-original vs extensiones (el corte que Helios/Wolf clonan)

- **GFE original:** base, tablas Gen3/4 (TCP), Gen5/7 (ENet), IV old-style 16B, input CBC/GCM, H264/HEVC,
  RFI. `sendMessageEnet` fuerza RELIABLE con GFE.
- **Sunshine** (gated `IS_SUNSHINE()`): `SS_ENC_CONTROL_V2`/`_VIDEO`/`_AUDIO`, RTSP-enc, SDP `x-ss-*`/
  `x-ml-*`, códecs 4:4:4/AV1/HEVC-RExt (`SCM_* "Sunshine extension"`), tipos control `0x5500-0x5503`
  (rumble triggers/motion/RGB LED/adaptive triggers), FEC-status upstream, HDR metadata, touch/pen.
- **Apollo** (→ Helios), en `packetTypesGen7Enc`: Execute Server Cmd `0x3000`, Set Clipboard `0x3001`,
  File-transfer nonce `0x3002` (`:234-236`).

## 8. Invariantes DUROS (no cambiar sin romper los 3 forks)

1. **ABI de common-c intocable** (`STREAM_CONFIGURATION`, key/iv de 16B, supportedVideoFormats).
2. **ENet bundleado obligatorio** — otra libenet CRASHEA contra GFE reciente (README.md L9).
3. **Esquema IV/GCM idéntico** en los 3 forks (control-v2 12B+dominio+tag16B; video IV explícito;
   input ratchet; audio CBC IV=seq). Un byte de desalineo = caída silenciosa tipo qt #1300.
4. **UNA key, contextos EVP separados**; RESET_IV sólo al cambiar largo de IV; nunca cambiar key en
   contexto vivo.
5. **FEC chunks de 16B** → packetSize múltiplo de 16; restar `ENC_VIDEO_HEADER` con video-enc on.
6. **`IS_SUNSHINE()` gatea las extensiones** — anunciarlas mal → cliente cae a claro / no ofrece 4:4:4/AV1.

## 9. Referencias

- **Código = contrato:** `Connection.c`, `ControlStream.c`, `PlatformCrypto.{c,h}`, `RtspConnection.c`,
  `SdpGenerator.c`, `VideoStream.c`, `Video.h`, `InputStream.c`, `AudioStream.c`, `RtpVideoQueue.c`,
  `ConnectionTester.c`, `reedsolomon/rs.c`, `Limelight.h`, `Limelight-internal.h`, `README.md`.
- **Memorias:** [[nvidia-gamestream-protocol]] (referencia completa con todas las citas),
  [[moonlight-doc-sintesis]], [[moonlight-issues-forums]].
- **Skills:** `moonlight` (contrato alto nivel — carga junto a este), `helios`/`wolf` (hosts que sirven
  este wire), `selene`/`sunshine`.
