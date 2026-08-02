---
name: nvidia-gamestream-protocol
description: Referencia PROFUNDA del wire protocol NVIDIA GameStream tal como lo implementa moonlight-common-c (el submódulo compartido de Helios/Selene) — puertos, handshake RTSP etapa-por-etapa, pairing PIN+cert, control ENet + AES-GCM (IV=seq, el esquema del "EVP storm"), video RTP+FEC Reed-Solomon, audio Opus/AES-CBC, input AES-GCM, y el corte NVIDIA-original vs extensiones Sunshine/Apollo. Con citas archivo:línea.
metadata:
  type: reference
---

# NVIDIA GameStream — el wire protocol REVERSE-ENGINEERED (nivel bits)

> Investigación 2026-07-25 leyendo el CÓDIGO REAL de `moonlight-common-c` (contrato compartido de
> los 3 forks). Esto va MÁS PROFUNDO que [[moonlight-doc-sintesis]] (que es el resumen de alto
> nivel): aquí están los headers de paquete, los esquemas de IV, las tablas de tipos por generación y
> las citas archivo:línea. **Fuente de verdad = el código, no esta nota.** Rutas relativas a
> `Helios/third-party/moonlight-common-c/src/` (idéntico en `Selene/moonlight-common-c/moonlight-common-c/src/`).

## 0. Origen — de dónde SALE este protocolo

**GameStream** es el protocolo propietario que NVIDIA creó para **GeForce Experience (GFE)** /
SHIELD (streaming de PC→SHIELD). NVIDIA nunca lo documentó: **Moonlight lo reverse-engineó** del
tráfico GFE↔SHIELD. `moonlight-common-c` es "the core GameStream client code" (README.md L1-3)
compartido por todos los clientes Moonlight. Cuando NVIDIA descontinuó GFE, **Sunshine** apareció
como host open-source que habla GameStream, y **extendió** el protocolo (encriptación moderna,
AV1, 4:4:4, HDR metadata…). Genealogía de Jordi: host `Sunshine→Apollo→Helios`, cliente
`Moonlight-Qt→Artemis→Selene`, más `Wolf` (host headless). Todos hablan ESTE protocolo o se rompen.

**Detección de "sabor" de host** (`Limelight-internal.h:86`): `IS_SUNSHINE()` = `AppVersionQuad[3] < 0`.
Sunshine/Apollo/Helios ponen un 4º componente de versión negativo → el código habilita extensiones.
La "generación" (`AppVersionQuad[0]`: 3/4/5/7) selecciona tablas de paquete distintas — GFE viejo era
Gen3/4 (TCP control), Gen5+ usa ENet, Gen7 = GFE 3.x / Sunshine.

## 1. Mapa de puertos (con protocolo POR puerto) — `ConnectionTester.c:53-79`, `Limelight.h:867-883`

| Puerto | Proto | Uso |
|---|---|---|
| **47984** | TCP | **HTTPS** — pairing seguro + `/serverinfo` firmado |
| **47989** | TCP | **HTTP** — `/serverinfo`, `/applist`, `/launch`, `/resume` (query params) |
| **48010** | TCP | **RTSP** (GFE 3.x). También `rtspru://` (RTSP-sobre-ENet) en algunas versiones |
| **47998** | UDP | **VIDEO** (RTP+FEC). `ML_ERROR_NO_VIDEO_TRAFFIC` culpa a este puerto (`ConnectionTester.c:37-40`) |
| **47999** | UDP | **CONTROL** (ENet). Fallo de `STAGE_CONTROL_STREAM_START` → este puerto (`ConnectionTester.c:25-26`) |
| **48000** | UDP | **AUDIO** (RTP). También la ping de RTSP: *GFE 3.22 exige ping en 48000 para completar el handshake RTSP* (`ConnectionTester.c:22-23`) |
| **48010** | UDP | RTSP-sobre-ENet / mic |
| 5353 | UDP | mDNS (descubrimiento LAN, fuera de common-c) |

Los puertos de video/control/audio se re-confirman en el RTSP SETUP (`VideoPortNumber`,
`ControlPortNumber`, `AudioPortNumber` se pueblan desde las respuestas — `Connection.c:266-269`), pero
los defaults well-known son los de arriba. **STREAM_CFG_AUTO** decide local vs remoto por RFC1918
(`Connection.c:388-412`) y capea `packetSize` a 1024 (IPv4) / 1184 (IPv6) en remoto.

## 2. Secuencia de arranque (11 etapas) — `Limelight.h:369-381`, orquestada en `Connection.c:332-527`

`PLATFORM_INIT → NAME_RESOLUTION → AUDIO_STREAM_INIT → RTSP_HANDSHAKE → CONTROL_STREAM_INIT →
VIDEO_STREAM_INIT → INPUT_STREAM_INIT → CONTROL_STREAM_START → VIDEO_STREAM_START →
AUDIO_STREAM_START → INPUT_STREAM_START`. `LiStopConnection()` (`Connection.c:69-144`) deshace en
orden inverso. Un fallo de encoders/KMS en el host revienta en las etapas `*_STREAM_START`
(el "probing failed" de Helios). Nombres estables: `LiGetStageName()`.

## 3. Handshake RTSP — `RtspConnection.c`, `SdpGenerator.c`

Secuencia con `CSeq` incremental (`RtspConnection.c:80`, `currentSeqNumber++`):
**OPTIONS → DESCRIBE → SETUP(×3: audio/video/control) → ANNOUNCE → PLAY** (`performRtspHandshake`,
~`RtspConnection.c:1037-1300`).

- **Target/transport** (`RtspConnection.c:936-973`): `useEnet` = Gen5-7 con `AppVersionQuad[2] < 404`;
  usa esquema `rtspru://` sobre ENet en vez de TCP. `controlStreamId` = `"streamid=control/13/0"`
  (7.1.431+) o `"streamid=control/1/0"`.
- **OPTIONS** (`:521`) / **DESCRIBE** (`:537`): DESCRIBE devuelve el SDP del host. De ahí el cliente
  parsea las **capacidades de encriptación** (`:1136-1140`):
  `x-ss-general.encryptionSupported` → `EncryptionFeaturesSupported`,
  `x-ss-general.encryptionRequested` → `EncryptionFeaturesRequested`. (Si no están → 0 = GFE puro.)
- **SETUP** (`:561`, uno por stream): `Transport: unicast;X-GS-ClientPort=50000-50001` (Gen6+;
  "GFE no le importa qué puerto digas pero necesita uno", `:577-582`). La respuesta trae
  `X-SS-Ping-Payload` (`:1189,1246`) y `X-SS-Connect-Data` (`:1287`) → SS_PING de audio/video y
  `ControlConnectData`.
- **ANNOUNCE** (`sendVideoAnnounce`, `:625`): el cliente SUBE su SDP (`application/sdp`) generado por
  `getSdpPayloadForStreamConfig()`. **Aquí se fija la encriptación efectiva y los códecs** (§5, §6).
- **PLAY** (`:604`): arranca.

**SDP del cliente** (`SdpGenerator.c:255-543`) — atributos clave que el host DEBE entender:
`x-nv-video[0].clientViewportWd/Ht/maxFPS/packetSize/initialBitrateKbps`, `x-nv-vqos[0].fec.enable=1`
(FEC SIEMPRE on para el secuenciamiento RTP, `:386`), `x-nv-video[0].dynamicRangeMode` (HDR),
`x-nv-video[0].encoderCscMode` = `(colorSpace<<1)|colorRange`. Sólo-Sunshine (`IS_SUNSHINE()`,
`:269-312`): `x-ml-general.featureFlags` (client caps), `x-ss-general.encryptionEnabled`,
`x-ss-video[0].chromaSamplingType` (1=YUV444). El header SDP se firma como `"s=NVIDIA Streaming
Client"` (`:550`) — herencia literal de GFE.

## 4. Pairing (PIN + certificados) — HTTP/HTTPS, FUERA de common-c

El pairing NO vive en moonlight-common-c (lo hace el cliente Qt / el host). Flujo GameStream clásico
de 4 fases sobre `/pair` (HTTP 47989 → HTTPS 47984): el cliente genera un **cert self-signed**, pide
`getservercert` (fase 1, manda su cert + salt), el host muestra un **PIN** de 4 dígitos; ambos derivan
una **AES key = SHA(salt + PIN)** y corren un **challenge/response** de 3 rondas
(clientchallenge → serverchallengeresp → clientpairingsecret) que prueba que ambos conocen el PIN sin
transmitirlo, e intercambia/fija los certs para autenticar sesiones futuras (mTLS en 47984). La
**`remoteInputAesKey`** que usa TODO el cifrado de streaming (§5) NO sale del pairing: se genera fresca
por sesión y viaja como `rikey`/`rikeyid` en `/launch` y `/resume` (§5.4). Gotcha del ecosistema de
Jordi: pairing por **CN del cert en LAN** (ver `deploy-streaming-y-resolucion.md`).

## 5. Encriptación — el CORAZÓN (y el origen del "EVP storm")

**Toda la criptografía de streaming usa UNA sola clave de 128 bits**: `StreamConfig.remoteInputAesKey`
(16 bytes, `Limelight.h:101`) = el `rikey` de `/launch`/`/resume`. Lo que cambia entre streams es el
**esquema de IV**. Motor cripto: `PlatformCrypto.c` (OpenSSL EVP o mbedTLS), API en `PlatformCrypto.h`.

### 5.1 Los feature-flags (`Limelight-internal.h:48-54`)
`SS_ENC_CONTROL_V2 0x01` · `SS_ENC_VIDEO 0x02` · `SS_ENC_AUDIO 0x04`. Negociados en
`SdpGenerator.c:276-303`: control-v2 se habilita SIEMPRE que el host lo soporte ("low overhead");
video/audio sólo si host Y cliente los piden (`StreamConfig.encryptionFlags` = `ENCFLG_*`,
`Limelight.h:33-36`). **Si el host REQUIERE video/audio-enc y el cliente no lo pidió, se cifra igual
con warning** (`:284-289, :295-300`). Versión de protocolo: `LiGetLaunchUrlQueryParameters()` →
`"&corever=1"` (`Connection.c:537-541`): v0 = video-enc + control-v2; **v1 = RTSP encryption**.

### 5.2 Control stream — AES-GCM, IV=seq (⭐ EL CRUX del EVP storm) — `ControlStream.c`
Header de paquete cifrado (`ControlStream.c:26-32`): `NVCTL_ENCRYPTED_PACKET_HEADER` =
`{ u16 encryptedHeaderType=0x0001; u16 length; u32 seq; }` seguido de **tag GCM 16B** + ciphertext
(un `NVCTL_ENET_PACKET_HEADER_V2` + payload cifrados). El **`seq` monotónico ES el IV**.

**El corte que importa** (`encryptControlMessage`, `:546-589` / `decryptControlMessageToV1` `:592-661`):
- **`SS_ENC_CONTROL_V2`** (moderno): **IV de 12 bytes** = `seq` en little-endian en `iv[0..3]`, y
  **bytes de dominio** para que jamás colisione el IV entre direcciones/streams:
  `iv[10]='C', iv[11]='C'` cliente→host; `iv[10]='H', iv[11]='C'` host→cliente. 12B = ideal para GCM.
- **Old-style (NVIDIA)**: **IV de 16 bytes**, `iv[0] = (uint8_t)seq` (¡cast truncante! "es lo que hace
  NVIDIA, hay que imitarlo", `:567`), resto 0.

`encryptedControlStream = APP_VERSION_AT_LEAST(7,1,431)` (`:330`). El header V2→V1 se re-empaqueta
in-place tras descifrar (`:655-658`). **Canales ENet** (`Limelight-internal.h:57-67`): 0=generic,
1=urgent (IDR/RFI), 2=keyboard, 3=mouse, 4=pen, 5=touch, 8=serverctl, 0x10-0x1F=gamepad,
0x20-0x2F=sensor.

**Por qué es el "EVP storm":** en OpenSSL (`PlatformCrypto.c:142-228`) el contexto GCM se
**re-inicializa completo** sólo con `!initialized || CIPHER_FLAG_RESET_IV` (para poder cambiar el
LARGO de IV); si no, sólo re-setea el IV (`EVP_EncryptInit_ex(ctx,NULL,NULL,NULL,iv)`). Reglas DURAS
escritas en `PlatformCrypto.c:33-35`: "para GCM el IV puede cambiar entre mensajes SIN RESET_IV;
RESET_IV sólo se necesita si cambia el LARGO del IV; **cambiar la KEY entre llamadas sobre un mismo
contexto NO está soportado**". Por eso hay `encryptionCtx` y `decryptionCtx` SEPARADOS
(`ControlStream.c:121-122`). Si un fork mezcla contextos, cambia de 12B↔16B sin RESET_IV, o reusa la
key en el contexto equivocado → OpenSSL tira errores de EVP en cascada y el control stream **cae en
silencio** (no hay excepción, sólo `PltDecryptMessage` devuelve false y se descarta el paquete,
`:1238-1242`). Ese es el desalineo que hay que evitar entre Helios/Wolf/Selene.

### 5.3 RTSP encryption (corever=1) — AES-GCM, IV=seq, tag 16B — `RtspConnection.c:92-140`
Header `ENC_RTSP_HEADER` = `{ u32 typeAndLength(BE, bit 0x80000000=ENCRYPTED); u32 sequenceNumber(BE);
u8 tag[16]; }` + ciphertext. IV de 12B mismo esquema que control pero con **dominio RTSP**:
`iv[10]='C',iv[11]='R'` cliente; `iv[10]='H',iv[11]='R'` host (`:130-131, :204-205`).

### 5.4 Input — AES-GCM (Gen7+) / AES-CBC (pre-Gen7) — `InputStream.c`
La clave es la misma `remoteInputAesKey`. **Input SIEMPRE cifrado, no opcional.**
- Gen7+ (`encryptData`, `:160-176`): AES-GCM, tag 16B prepend al ciphertext. IV = `currentAesIv`
  (**16 bytes**, inicializado desde `StreamConfig.remoteInputAesIv`, `:97`).
- **Quirk NVIDIA obligatorio** (`:282-291`): tras cada paquete de gamepad, NVIDIA usa **los últimos 16
  bytes del ciphertext previo como IV del siguiente** ("creo que es un buffer overrun de su lado, pero
  hay que imitarlo"). Un host debe reproducir ESTE ratchet o se desincroniza el input.
- Pre-Gen7 (`:184-191`): AES-CBC con PKCS7 (`CIPHER_FLAG_PAD_TO_BLOCK_SIZE`).
- Cuando `encryptedControlStream` (7.1.431+), el input va **en claro por el control stream** y ESE lo
  cifra con GCM (`:238-252`) — no doble cifrado.

### 5.5 Video — AES-GCM, IV transmitido en el paquete — `Video.h:15-19`, `VideoStream.c:186-224`
`ENC_VIDEO_HEADER` = `{ u8 iv[12]; u32 frameNumber; u8 tag[16]; }` (16-múltiplo para que el bloque FEC
siga siendo múltiplo de 16). Como el video es host→cliente y masivo, **el IV viaja explícito en cada
paquete** (no derivado de seq). El cliente descifra con GCM (`VideoStream.c:215-220`) tras descartar
frames viejos ANTES de descifrar (ahorro de CPU, `:211-213`). El header cifrado le resta a
`packetSize` (`SdpGenerator.c:323-327`).

### 5.6 Audio — AES-CBC, IV derivado del seq+keyid — `AudioStream.c:178-192`
Sólo si `AudioEncryptionEnabled`. AES-**CBC** (no GCM), IV de 16B = `BE32(avRiKeyId + rtp->sequenceNumber)`
en los primeros 4 bytes, con `CIPHER_FLAG_RESET_IV | CIPHER_FLAG_FINISH`.

## 6. Video RTP + FEC Reed-Solomon — `Video.h`, `RtpVideoQueue.c`, `reedsolomon/rs.c`
- **RTP header** (`Video.h:40-46`): `{ u8 header; u8 packetType; u16 seq; u32 timestamp; u32 ssrc; }`,
  12B fijos (`FIXED_RTP_HEADER_SIZE`), hasta 16 con extensión (`FLAG_EXTENSION 0x10`).
- **Payload NV** (`Video.h:25-33`): `NV_VIDEO_PACKET` = `{ u32 streamPacketIndex; u32 frameIndex; u8
  flags; u8 reserved; u8 multiFecFlags; u8 multiFecBlocks; u32 fecInfo; }`. Flags: `SOF 0x4`, `EOF 0x2`,
  `CONTAINS_PIC_DATA 0x1`. Soporta **multi-FEC blocks** por frame.
- **FEC** = **Reed-Solomon** (`reedsolomon/rs.c`, vendored — es la lib `nanors`; `reed_solomon_init()`
  en `RtpVideoQueue.c:20`). Recuperación sólo desde **Gen5** (`:253`). Negociado en el SDP:
  `x-nv-vqos[0].fec.enable=1` siempre, `fec.repairPercent` (5% en 4K, 20% normal, `SdpGenerator.c:225-229`),
  `fec.minRequiredFecPackets=2` y `bllFec.enable=0` en 7.1.431+ (BLL-FEC dinámico se apaga para caer al
  FEC legacy, `:210-217`). **La FEC sólo trabaja en chunks de 16 bytes** → `packetSize` se redondea
  hacia abajo a múltiplo de 16 (`Connection.c:293-295`). El recovery mode se apaga porque cambiar el %
  de FEC a mitad de frame rompe la cola RTP (`SdpGenerator.c:248-250`).

## 7. Reference-frame invalidation → LTR (common-c #120)
Recuperación de pérdida clásica = **RFI (Reference Frame Invalidation)**: el cliente detecta pérdida
(`connectionDetectedFrameLoss`, `ControlStream.c:443`), encola tuplas start/end
(`queueFrameInvalidationTuple`, `:407`) y las manda por el canal urgente (1); si se desborda pide IDR
completo (`LiRequestIdrFrame`, `:433`). Caps: `CAPABILITY_REFERENCE_FRAME_INVALIDATION_{AVC,HEVC,AV1}`
(`Limelight.h:248-273`). RFI se **desactiva en 4K con GFE** (artefactos, `Connection.c:325-330`).
**#120 (LTR)** reemplaza esto por Long-Term Reference frames — cambio de protocolo coordinado host+cliente
(ver skill `moonlight` §4). Vigilar al bumpear el submódulo.

## 8. Códecs — `Limelight.h` (cliente `VIDEO_FORMAT_*`) vs (host `SCM_*`)
Cliente pide en `supportedVideoFormats` (`:221-237`): H264 `0x0001`, HEVC Main `0x0100`, Main10 `0x0200`,
AV1 Main8 `0x1000`/Main10 `0x2000`; **4:4:4**: H264_HIGH8_444 `0x0004`, HEVC_REXT8/10_444 `0x0400/0x0800`,
AV1_HIGH8/10_444 `0x4000/0x8000`. Máscaras `MASK_10BIT 0xAA00`, `MASK_YUV444 0xCC04`.
Host anuncia `serverCodecModeSupport` (`SERVER_INFORMATION`, `:533`) con `SCM_*` (`:502-518`).
**HDR**: `SS_HDR_METADATA` (`:935-956`) + `LiGetHdrMetadata()`; la metadata HDR viaja en el mensaje HDR
del control stream y sólo Sunshine la manda (`ControlStream.c:1274-1290`).

## 9. NVIDIA-original vs extensiones Sunshine/Apollo (el corte que Helios/Wolf deben clonar)

**NVIDIA/GFE original:** base GameStream; tablas de paquete Gen3/4 (TCP control, `ControlStream.c:149-184`),
Gen5/7 (ENet, `:185-220`); encriptación old-style (IV 16B truncado); input AES-CBC pre-Gen7 / AES-GCM
Gen7; H.264/HEVC; RFI. `sendMessageEnet` fuerza `RELIABLE` con GFE (`:697-699`).

**Extensiones Sunshine** (todas gated por `IS_SUNSHINE()`):
- `SS_ENC_CONTROL_V2` (GCM IV-12B), `SS_ENC_VIDEO`, `SS_ENC_AUDIO`, RTSP encryption (`corever=1`).
- Atributos SDP `x-ss-*` / `x-ml-*`, client feature flags `ML_FF_FEC_STATUS`/`ML_FF_SESSION_ID_V1`
  (`Limelight-internal.h:88-90`).
- Códecs 4:4:4 / AV1 / HEVC-RExt (`SCM_*` marcados "Sunshine extension", `Limelight.h:505-511`).
- Tipos de control **sólo en tabla `packetTypesGen7Enc`** (`ControlStream.c:221-238`): rumble triggers
  `0x5500`, set motion `0x5501`, RGB LED `0x5502`, adaptive triggers `0x5503` (DualSense).
- FEC status upstream (`SS_FRAME_FEC_STATUS`, `Video.h:56-68`, ptype `0x5502`), SS_PING payloads, HDR
  metadata en el mensaje HDR, teclado no-normalizado (`SS_KBE_FLAG_NON_NORMALIZED`), H-scroll,
  touch/pen/controller-touch, controller arrival, 16 gamepads.

**Extensiones Apollo** (heredadas por Helios) — también en `packetTypesGen7Enc`:
Execute Server Command `0x3000`, Set Clipboard `0x3001`, File transfer nonce request `0x3002`
(`ControlStream.c:234-236`).

## 10. Gotchas & invariantes — lo que NUNCA se cambia sin romper cross-fork

1. **ABI de `moonlight-common-c` es intocable.** `STREAM_CONFIGURATION` (encryptionFlags,
   remoteInputAesKey/Iv[16], supportedVideoFormats) es contrato binario. Selene forkea el cliente sin
   tocarlo.
2. **ENet BUNDLEADO obligatorio** (README.md L9): la versión con parches IPv6+retransmisión es
   **API/ABI-incompatible** con libenet del sistema; runtime-linkear otra **crashea** contra GFE
   reciente. Helios/Wolf compilan ESE ENet, jamás el del sistema.
3. **Esquema IV/GCM idéntico** entre los 3 forks: control-v2 = **IV 12B (seq LE en iv[0..3] +
   dominio en iv[10..11]) + tag 16B**; old-style = IV 16B truncado. Video = IV explícito de 12B en el
   header. Input Gen7 = GCM con el **ratchet de "últimos 16B del ciphertext previo"**. Audio = CBC con
   IV = `BE32(avRiKeyId+seq)`. Un solo byte de desalineo → el stream cae **en silencio** (síntoma tipo
   qt #1300 "control stream establishment error"). Es literalmente el EVP storm.
4. **UNA sola key (`remoteInputAesKey`) para TODO**; contextos EVP encrypt/decrypt **separados**; nunca
   cambiar la key sobre un contexto vivo; `RESET_IV` sólo al cambiar el LARGO del IV
   (`PlatformCrypto.c:33-35`).
5. **FEC en chunks de 16B** → `packetSize` múltiplo de 16 (`Connection.c:293-295`); no cambiar % de FEC
   a mitad de frame (recovery mode off).
6. **`packetSize` con overhead de encriptación**: restar `sizeof(ENC_VIDEO_HEADER)` cuando video-enc
   está on (`SdpGenerator.c:323-327`).
7. **Tablas de tipo por generación** (`ControlStream.c:149-238`): un host que anuncia AppVersion Gen7
   con 4º componente negativo DEBE hablar `packetTypesGen7Enc` (encriptado) o el cliente no lo entiende.
8. **`IS_SUNSHINE()` gatea todas las extensiones** — anunciarlas mal → el cliente cae a claro, rechaza,
   o no ofrece 4:4:4/AV1.

---
### Fuentes (código local = contrato real)
`Helios/third-party/moonlight-common-c/src/`: `Limelight.h`, `Limelight-internal.h`, `Connection.c`,
`ControlStream.c`, `PlatformCrypto.{c,h}`, `RtspConnection.c`, `SdpGenerator.c`, `VideoStream.c`,
`Video.h`, `InputStream.c`, `AudioStream.c`, `RtpVideoQueue.c`, `ConnectionTester.c`,
`reedsolomon/rs.c`, `README.md`. (Idéntico en `Selene/moonlight-common-c/moonlight-common-c/src/`.)
### Memorias relacionadas
[[moonlight-doc-sintesis]] (resumen alto nivel), [[moonlight-issues-forums]] (cambios de protocolo #120
LTR etc.), [[forks-helios-selene]], [[security-findings-2026-06.local]], [[deploy-streaming-y-resolucion]],
[[diseno-headless-multisesion]] (Wolf). Skill par: `gamestream-protocol` (deep) junto a `moonlight`
(contrato de alto nivel).
