---
name: sync-sunshine-2026-07
description: Reporte COMPLETO del merge Helios <- Sunshine (561 commits, PR unjordi/Helios#12, 2026-07-26) — qué cedió cada lado archivo por archivo, los 5 bugs silenciosos encontrados, las exclusiones deliberadas (nanors, common-c, web UI), el hallazgo de que CUDA=OFF bloquea el 4:4:4, y la lista de QA en vivo pendiente.
metadata:
  type: project
---

# Sync Helios <- Sunshine (561 commits) — reporte de cesiones y QA pendiente
Rama: feat/sync-sunshine-2026-07 · 2026-07-26

## QA EN VIVO PENDIENTE (el daemon NO se tocó en este slice)
1. **[ALTA] Autodetección de GPU cambió.** `resolve_render_device()` ahora barre `/dev/dri/card*` y elige
   el que tiene un conector DRM_MODE_CONNECTED, en vez de asumir `renderD128` (Sunshine #4961 `32100783`).
   En la caja híbrida (RTX 5070 Ti + iGPU AMD) **puede cambiar qué GPU se usa para VAAPI/Vulkan**.
   Escape hatch: `adapter_name` en la config sigue teniendo prioridad.
2. **[ALTA] Interacción con el ritual de `setcap`.** Entra `has_/drop_elevated_privileges(all_caps)`
   (CAP_SYS_ADMIN + CAP_SYS_NICE): Sunshine ahora SUELTA caps tras usarlas. Si la captura KMS falla con
   "probing failed", el sospechoso es el ORDEN drop-vs-init-KMS, no la cap perdida.
3. **[MEDIA] Match de display por nombre de conector** (`4b745485`, `a55d2d99`) — debería arreglar
   Apollo #1543 (streamea el monitor equivocado). Verificar que sigue tomando el LG ULTRAGEAR+ por HDMI-A-1.
4. **[MEDIA] 4:4:4** — el soporte entra (`39c9e845`, `e4740f06`) pero va por la ruta **CUDA/cuda-gl**;
   el daemon vivo compila con `SUNSHINE_ENABLE_CUDA=OFF`. Evaluar flip a CUDA=ON como paso aparte.
5. **[BAJA] Pacing de captura** cambia a NTSC fraccional exacto (`bba6c6ca`), reemplazando los hacks de
   Apollo ("Stablize encode timing", "capture time elástico"). Verificar framepacing en vivo.
6. **[Windows, sin compilar aquí] `display_wgc.cpp`**: WGC pasa de `framerate x2` a 4ms (250 Hz) fijo.

## CESIONES Y CONSERVACIONES (por grupo)

### Linux / macOS
- `vaapi.cpp`: **cedió Sunshine entero.** Su selector genérico de rate-control (whitelist Intel/AV1 +
  `vaapi_rc`/`vaapi_quality` + fallback CBR->CQP + VAConfigAttribEncQualityRange) cubre el caso AMD mejor
  que las 9 lineas de Apollo que forzaban CBR/VBR. Duplicarlo habria dado doble `av_dict_set("rc_mode")`.
- `wlgrab.cpp`: **cedió Sunshine** (timestamp real del frame de Wayland vs `now()` post-captura).
  ⚠️ **BUG DE AUTO-MERGE CORREGIDO**: git resolvió MAL la primera `snapshot()` sin marcar conflicto —
  metió las DOS asignaciones seguidas + una variable huérfana. Se limpió a mano.
- `inputtino_gamepad.cpp`: **INTEGRADO** — wrapper multi-seat de Sunshine (`inputtino_name_for_seat`)
  con los strings "Helios" conservados. Con seat0 los nombres no cambian.
- `linux/misc.cpp`, `macos/misc.mm`: **INTEGRADO** — placeholders de clipboard de Apollo + bloque nuevo
  de Sunshine (`find_render_node_with_display`, `resolve_render_device`, drop de privilegios).
- Verificado con `g++-14 -fsyntax-only` real contra el árbol mergeado (4 TUs de Linux).
- ⚠️ `misc.cpp` necesita `third-party/lizardbyte-common` (dep nueva vía CPM) → **el build DEBE ser un dir
  NUEVO reconfigurado desde cero**, no reusar caches (coherente con el gotcha de build-helios2).

### Windows (⚠️ NADA de esto se compiló — el build de verificación es Linux/gcc-14)
- `misc.h`/`misc.cpp` UTF helpers: **INTEGRADO.** Sunshine movió `from_utf8`/`to_utf8` a `utf_utils`, pero
  la API vieja la usa el camino de **Virtual Display** (`process.cpp:280,350,557,784`, `main.cpp:439`).
  Se conservan ambas.
- `resolve_command_string()` URL: **conservó Apollo** (`rundll32.exe url.dll,FileProtocolHandler`, commit
  deliberado `81de679f`). El lado de Sunshine era idéntico a la base salvo el rename → no había nada que
  integrar. **Divergencia consciente:** una app de URL no pasa por el override per-user de
  `HKEY_CLASSES_ROOT`, así que resuelve contra el usuario invocante, no el impersonado. Merece decisión
  del dueño de permisos-por-cliente.
- `display_base.cpp`: **conservó Apollo** — la línea de Sunshine (`namespace bp = boost::process::v1`)
  habría sido un alias muerto con include ya removido = error de compilación seguro.
- `input.cpp`: **conservó Apollo** el shim MinGW. Sunshine lo borró como efecto colateral de un bump de
  toolchain (`875ad1d1`); quitarlo rompe builds con mingw viejo, mantenerlo solo arriesga redeclaración benigna.
- `tools/audio.cpp`: **cedió Sunshine** — el lado de Apollo estaba MUERTO (asignaba `wstring` a `string`,
  no podía compilar) y Sunshine además arregla un bug real (`prop.pwszVal` vs `prop.pszVal` casteado).
- `tools/sunshinesvc.cpp`: **INTEGRADO** — forma `constexpr auto` de Sunshine con el valor de Apollo
  `"ApolloService"` (hardcodeado en `entry_handler.cpp:144` y en los `.bat` de instalación).

## FOLLOW-UPS QUE SALEN DE ESTE MERGE (backlog, NO se hacen aquí)
- **Rebrand pendiente en Windows:** el servicio sigue siendo `"ApolloService"` (`sunshinesvc.cpp`,
  `entry_handler.cpp:144`, los `.bat`) y `get_host_name()` cae a `"Sunshine"s`. Cambio multi-archivo
  coordinado con los instaladores → backlog, no drive-by.
- **`tools/utils.cpp` / `utils.h` quedaron huérfanos** (audio.cpp era su único consumidor) → borrar en follow-up.
- **Adoptar el uplift de web UI de Sunshine** (#5225/#5166/#5158): se conservó la UI de Apollo en bloque
  porque choca con las páginas de permisos/apps. Slice futuro.
- **Co-bump de `moonlight-common-c` Helios+Selene** al mismo pin: sigue pendiente y sigue siendo cambio
  de protocolo coordinado (no se tomó el bump de Sunshine porque es otro repo).
- **Evaluar `SUNSHINE_ENABLE_CUDA=ON`** para habilitar 4:4:4 por HW en NVIDIA.
- **`input.cpp` MinGW**: una build real de Windows dirá si el shim sobra.

### Config / main / system_tray
- **Auditoría de claves: CERO opciones perdidas.** 103 claves de Apollo ∪ 94 de Sunshine = **112**, y el
  parser mergeado reconoce exactamente 112. Entran las 9 nuevas de Sunshine (`bind_address`,
  `csrf_allowed_origins`, `nvenc_split_encode`, `packetsize`, `vaapi_blbrc`, `vaapi_quality`, `vaapi_rc`,
  `vk_rc_mode`, `vk_tune`).
- 20 de 24 hunks de `config.cpp` cedieron a Sunshine por ser puramente cosméticos (doc comments,
  `#ifndef DOXYGEN`, `::std::`->`std::`).
- `server_cmd_t`/`server_cmds`/`state_cmds`: **conservó Apollo** (el lado de Sunshine era solo un doc comment).
- `log_config_settings`: **INTEGRADO** — redacción de secretos + parámetro `save` de Sunshine, conservando
  la conversión `utf8ToAcp` de consola de Apollo en Windows.
- `system_tray.cpp`: **INTEGRADO** — threading a `std::jthread` de Sunshine (elimina `tray_thread_running`/
  `tray_thread_should_exit`) conservando el título **"Open Helios (name:port)"** y la supresión de menú
  `hide_tray_controls`. Índices de menú reverificados.
- Verificado con `g++-14 -fsyntax-only` real: `config.cpp`, `main.cpp`, `system_tray.cpp` EXIT 0.

### 🐛 BUG CORREGIDO (venía de upstream Sunshine, nos pegaba MÁS a nosotros)
`input_t`: Sunshine promovió `key_rightalt_to_key_win` a miembro pero **no añadió su entrada al
inicializador agregado** → off-by-one que existe hoy en upstream. En upstream casi no se nota; en NUESTRO
árbol Apollo añade dos campos al final, así que los defaults quedaban corridos:
`native_pen_touch=false`, **`enable_input_only_mode=TRUE`** (clientes input-only permitidos por defecto —
inaceptable en un host expuesto a Internet), `forward_rumble=false`.
**Fix:** se añadió la entrada faltante `false, // key_rightalt_to_key_win`. Reverificados los
inicializadores de `video_t`, `audio_t` y `sunshine_t`.

### Cambios de comportamiento a saber (config)
- **Formato del log de config cambió** (gana Sunshine): antes `config: [name] -- [value]`, ahora
  `config: 'name' = value`. Cualquier grep de logs con el formato viejo se rompe.
- **Redacción de secretos activa pero casi vacía:** `config::redacted_config` solo contiene
  `csrf_allowed_origins`, que NO es un secreto. Las claves sensibles de Helios (`credentials_file`,
  pairing/OTP) **no se redactan**. Liga con el hallazgo previo de secretos en logs → decidir qué poblar.
- `log_config_settings(vars, false)` desde `main.cpp` ya no registra en `modified_config_settings`
  (gate `save` intencional de Sunshine, pero es cambio real vs Apollo).
- **`PROJECT_FQDN` sigue siendo `"dev.lizardbyte.app.Sunshine"`** (`CMakeLists.txt:20`) y ahora se compila
  en el binario y se pasa a `tray_set_app_info()` en Linux → es la identidad de app del tray/desktop.
  **Rebrand gap** (alimenta también `SUNSHINE_TRAY_PREFIX`, el .desktop y los metainfo).


### Video / process / audio / platform-common
- **`video.h` `config_t`: INTEGRADO con cuidado de LAYOUT.** Base = orden de Sunshine (con `framerateX100`
  insertado EN MEDIO, para el pacing NTSC fraccional) y los campos de Apollo (`encodingFramerate`,
  `input_only`) appendeados AL FINAL. Clave: los inicializadores POSICIONALES ya auto-mergeados en
  `video.cpp` (5 sitios) encajan con el orden de Sunshine — con el orden de Apollo habrían quedado
  mal mapeados silenciosamente. Cruce verificado: `windows/display_base.cpp:736` sigue leyendo `framerateX100`.
- **`video.cpp` pacing: INTEGRADO** — cede Sunshine en `minimum_fps_target`/`max_frametime` (#4967 canónico)
  y se conserva encima el pacing de Apollo (`encode_frame_threshold`, `frame_variation_threshold`) porque el
  cuerpo del loop auto-mergeado depende de esas variables. **Se añadió un fallback
  `config.encodingFramerate > 0 ? ... : framerate*1000`: los nuevos config de probe de Sunshine dejan
  `encodingFramerate == 0` → habría sido DIVISIÓN POR CERO.**
- `last_encoder_probe_supported_yuv444_for_codec`: **cedió Sunshine** (`{}` en vez del `{true,true,true}`
  optimista de Apollo) — con el probe real de 4:4:4 de Sunshine, el default optimista solo enmascaraba.
- `input_only`: **conservó Apollo** (bloque completo de sesión input-only + overload `refresh_displays`).
- `platform/common.h`: **INTEGRADO** — clipboard de Apollo + `has_/drop_elevated_privileges` de Sunshine.
  ⚠️ `SERVICE_NAME`/`SERVICE_TYPE` pasan de MACRO a `constexpr auto` en `namespace platf` (obligatorio:
  `entry_handler.cpp` ya los usa cualificados). `platf::set_env`/`unset_env` desaparecen (Sunshine los movió
  a `lizardbyte::common`).
- `process.h`/`process.cpp`: **conservó Apollo en bloque** (UUIDs, driver de vDisplay, `ctx_t` completo,
  `execute(const ctx_t&)`, `terminate(bool,bool)`, `migrate_apps`, parse con nlohmann) + se adoptó de
  Sunshine `DEFAULT_APP_IMAGE_PATH` movido a `process.h` (lo necesita `confighttp.cpp`).
- `audio.cpp/h`: **INTEGRADO** — gate `config::audio.auto_capture` de Apollo + firma de 6 args de
  `microphone()` de Sunshine; `__padding`/`input_only` de Apollo conservados.
- `nvenc_config.h`: **INTEGRADO** — `split_frame_encoding` (Sunshine) + `intra_refresh` (Apollo).
- `input.h`: **conservó Apollo** la firma con `const crypto::PERM&` (permisos por-cliente, 2 call-sites en `stream.cpp`).
- Verificación: cross-diff del set de declaraciones/campos contra AMBOS padres → cero pérdidas.

### Build system
- **`build-deps` tuvo que CEDER a Sunshine** (pin `a9a9277`, rama `master`) — el intento de conservar la
  rama `dist` de Apollo hizo fallar el configure con `Vulkan headers not found in build-deps submodule`:
  el `linux.cmake` nuevo espera `third-party/build-deps/third-party/FFmpeg/Vulkan-Headers/include`.
  Requiere `git submodule update --init --recursive` (build-deps tiene submódulos anidados).
- **`cmake` configure: EXIT 0** con gcc-14, Release, CUDA=OFF, DOCS/TESTS=OFF, WERROR=OFF.
  Detectados: glad (egl+gl), libdrm 2.4.134, libcap 2.78, libevdev 1.13.6, libva 1.24.0, systemd/udev 261.

### Branding pendiente (NO tocado — decisión de producto)
- `platf::SERVICE_NAME = "Apollo"`, `tools/sunshinesvc.cpp` = `"ApolloService"`, el unit `apollo.service`,
  `PROJECT_FQDN = "dev.lizardbyte.app.Sunshine"` (CMakeLists.txt:20) y `get_host_name()` cae a `"Sunshine"`.
  Renombrarlos es un cambio coordinado multi-archivo (incluye instaladores y el unit de systemd en vivo)
  → **backlog**, no drive-by en este merge.


### nvhttp / stream / crypto / rtsp
- **`crypto.cpp` `openssl_verify_cb`: conservó Apollo/Helios VERBATIM** — el fix CVE-2026-32253 (`ebc73d10`)
  quedó byte-idéntico a `develop`. **El `switch` de Sunshine que fuerza aceptar `CERT_HAS_EXPIRED` /
  `CERT_NOT_YET_VALID` NO entró** (habría sido un RETROCESO de seguridad). El resto del archivo cede a
  Sunshine (OpenSSL 4.x, `x509_name_t`).
- **🔴 `stream.cpp` `IDX_*`: INTEGRADO — evitó una corrupción SILENCIOSA de protocolo.** Se tomó el estilo
  `constexpr int` de Sunshine pero con los VALORES de Apollo: el `packetTypes[]` auto-mergeado conserva las
  extensiones de Apollo en 15/16/17 (`0x3000` exec-server-cmd, `0x3001` clipboard, `0x3002` file-transfer-nonce),
  así que `IDX_SET_ADAPTIVE_TRIGGERS` de Sunshine es **18, no 15**. Tomar la numeración de upstream tal cual
  habría mapeado adaptive-triggers ENCIMA del opcode de ejecutar-comando-en-el-host. Sin error de compilación.
- **`nvhttp.cpp` kill-switch de clientes (#4771/#5138): INTEGRADO, no cedido.** El flag `enabled` de Sunshine
  es ortogonal a la máscara `PERM` de Apollo → se añadió `bool enabled = true` a `crypto::named_cert_t`,
  persistido en `save_state`/`load_state` y expuesto en `get_all_clients`. Las funciones de upstream usaban
  semántica por valor y no habrían compilado con el contenedor `shared_ptr<named_cert_t>` de Apollo → reescritas.
- `nvhttp.cpp` callback TLS: **INTEGRADO** — firma de Apollo (`req->userp = named_cert_p`, necesaria para
  permisos por-cliente en `/serverinfo` y `/applist`) + el gate `is_client_enabled()` de Sunshine.
- `make_launch_session`: **conservó Apollo** (4 args, lógica de display-mode/perm/cmds por `named_cert_p`);
  de Sunshine solo `client_cert` y `continuous_audio`.
- `rtsp.h` `launch_session_t`: **INTEGRADO** — campos de Apollo + los 3 nuevos de Sunshine.
- Fixes de upstream verificados como aterrizados: `ExternalIP` en serverinfo (#5043), `get_codec_mode_flags()`
  con la lógica corregida `==3||==5` / `==4||==5` de HEVC/AV1 10-bit (#4965), `IsHdrSupported` `==3`->`>=3`,
  guards `EMPTY_PROPERTY_TREE_ERROR_MSG` (#4390), pairing por stdin (#4912), `net::get_bind_address()` (#4481),
  `platf::set_thread_name` (#4605), `std::jthread` (#5398).
- Verificado con `g++-14 -std=c++20 -fsyntax-only` real: `crypto.cpp`, `httpcommon.cpp`, `stream.cpp`,
  `nvhttp.cpp`, y como consumidores `rtsp.cpp` y `process.cpp`.

### Build / docs / README
- `compile_definitions/linux.cmake`: **cedió Sunshine** (byte-idéntico). Sunshine movió el tray a
  `third-party/tray` como target Qt `tray::tray`; el bloque de Apollo referenciaba `tray_linux.c`, que YA NO
  EXISTE. **Efecto colateral que rompió el build y hubo que arreglar a mano:** `SUNSHINE_TRAY_PREFIX` dejó de
  definirse y `src/system_tray.cpp` seguía usándolo → 108 errores en cascada. Corregido apuntando los 4 íconos
  al web dir (`WEB_DIR "images/logo-apollo.svg"` etc.), que sí existen como SVG.
- `packaging/linux.cmake`: **INTEGRADO** — forma de Sunshine (instalar como `${PROJECT_FQDN}.svg` + copia al
  web dir) con fuente `apollo.svg`. Sunshine dejó de registrar los íconos de estado en el tema hicolor (Qt6 los rechaza).
- `windows.cmake`: **INTEGRADO + revert de un rename de Apollo** — Apollo había renombrado
  `SUNSHINE_ICON_PATH`->`PROJECT_ICON_PATH`, pero `packaging/windows_wix.cmake` (auto-mergeado a la versión de
  Sunshine) usa el nombre viejo → se volvió al nombre de Sunshine apuntando a `apollo.ico`.
- `windows_nsis.cmake`: **INTEGRADO** — flujo nuevo `sunshine-setup.ps1` + re-inyección de SOLO lo que el .ps1
  no hace (install/uninstall de SudoVDA). Se dropeó `uninstall-gamepad.ps1` (referencia muerta ya en Apollo).
- `tools/CMakeLists.txt`: **cedió Sunshine** (`${TOOL_SOURCES}`), obligatorio para que `audio-info` linkee con `utf_utils`.
- `boost.json`: **cedió Sunshine** (Boost 1.89.0) — obligado por `Boost_Sunshine.cmake` que lo fija con `EXACT`.
- `README.md`: **conservó el de Helios** + 3 injertos técnicos (mínimos de OS actualizados; backends nuevos de
  captura Linux **KWin ScreenCast** y **XDG Portal**, ambos ON por default, y **Vulkan Video**; deps de Arch).

## ✅ REFUTADO EN VIVO (2026-07-27): `CUDA=OFF` **NO** bloquea el 4:4:4
> ⚠️ **La sección de abajo quedó DESMENTIDA por el despliegue.** Se conserva porque documenta el
> razonamiento (leído del código) y por qué falló; lo que vale es esta corrección.

Con `build-helios3` (CUDA=**OFF**) desplegado y corriendo, el host **SÍ anuncia 4:4:4**:
`ServerCodecModeSupport = 2032385 (0x1F0301)` = `SCM_H264 | SCM_HEVC | SCM_HEVC_MAIN10 |
SCM_AV1_MAIN8 | SCM_AV1_MAIN10 | ` **`SCM_H264_HIGH8_444 | SCM_HEVC_REXT8_444 | SCM_HEVC_REXT10_444`**.
Único que NO anuncia: **AV1 4:4:4** (log: `av1_nvenc: YUV 4:4:4 not supported` — límite del propio NVENC
para AV1, no del flag de build).

**Por qué falló el razonamiento estático:** los flags NO se derivan de qué se compiló, sino del resultado
REAL del probe de encoders — `get_codec_mode_flags()` (`src/nvhttp.cpp:1040-1070`) lee
`video::last_encoder_probe_supported_yuv444_for_codec[]`. Y el probe **pasó** para H.264 (idx 0) y HEVC
(idx 1) por la ruta NVENC-sin-CUDA (`GPU→RAM→GPU`). O sea: `gl_cuda_vram_t` es la ruta 4:4:4
**zero-copy**, no la ÚNICA ruta 4:4:4. **Lección de método: leer el código dice qué se compiló; solo
ejecutarlo dice qué CAPACIDAD acabó anunciando.**

**Consecuencia para el backlog:** el flip a `CUDA=ON` deja de ser "para desbloquear 4:4:4" (ya está
desbloqueado) y pasa a ser **optimización de rendimiento** (zero-copy, evitar el rebote por RAM) +
NvFBC. Baja de prioridad. Falta el QA en vivo de Jordi con Selene para confirmar que el 4:4:4 **de
verdad streamea** (anunciarlo ≠ funcionar).

### (razonamiento original, DESMENTIDO — conservado como registro)
La memoria del proyecto tenía razón A MEDIAS. Verificado leyendo el código mergeado:
- **NVENC NO depende de ese flag** — va por FFmpeg (`h264_nvenc`/`hevc_nvenc`/`av1_nvenc`). Con CUDA=OFF sigue
  encodeando (warning benigno, sin zero-copy). Eso seguía siendo cierto.
- **PERO el flag gatea `SUNSHINE_BUILD_CUDA` -> `src/platform/linux/cuda.{h,cpp,cu}` + NvFBC, y ahí vive
  `gl_cuda_vram_t`, que es EXACTAMENTE la ruta del 4:4:4 nuevo** (`cuda.cpp:473-520`: acepta
  `AV_PIX_FMT_YUV444P`/`YUV444P16LE`, `is_yuv444`, `create_yuv444_target`).
- ~~**Conclusión: con `SUNSHINE_ENABLE_CUDA=OFF` NO hay 4:4:4 por NVENC en Linux, ni NvFBC.**~~ ❌ **FALSO**
  (ver corrección arriba: el host anuncia H.264/HEVC 4:4:4 con CUDA=OFF). Lo que SÍ sigue en pie: sin el
  flag no hay **NvFBC** ni la ruta **zero-copy**. VAAPI conserva sus perfiles `VAProfileHEVCMain444(_10)`
  (ruta iGPU AMD); Vulkan Video no toca 4:4:4.
- ~~**Para que Jordi vea el 4:4:4 hay que compilar con `SUNSHINE_ENABLE_CUDA=ON`.**~~ ❌ **FALSO** — ya lo ve
  con CUDA=OFF. El TIP del README que decía que el flag habilitaba NVENC sí era falso y eso sigue corregido.

## Dependencias de sistema para CachyOS (todas YA presentes en esta máquina, verificado)
`qt6-base`, `qt6-svg` (backend Qt del tray nuevo, **sustituye a `libayatana-appindicator`**), `pipewire`
(lo exigen `SUNSHINE_ENABLE_KWIN` y `SUNSHINE_ENABLE_PORTAL`, **ambos ON por default**), `shaderc`/`glslc`
(compila `rgb2yuv.comp` a SPIR-V, `FATAL_ERROR` si falta), `vulkan-icd-loader`, `python-jinja`, `libnotify`.
⚠️ `SUNSHINE_ENABLE_VULKAN=ON` es el default y exige el submódulo ANIDADO
`build-deps/third-party/FFmpeg/Vulkan-Headers` → un clon sin `--recurse-submodules` profundo muere en configure.


### confighttp (el más divergente — 40 hunks)
La divergencia era demasiado profunda para ir hunk por hunk (Sunshine reescribió el archivo entero: API por
const-ref, capa CSRF, `getPage` genérico, API expuesta en el header para tests unitarios). Se **reconstruyó
sobre la estructura de Sunshine re-aplicando CADA feature de Apollo encima**.
- `authenticate()`: **INTEGRADO** — cookie de sesión de Apollo **+** HTTP Basic de Sunshine. Solo-cookie
  reprobaba 4 tests de la suite nueva; solo-Basic mataba la página de login.
- Fallo de auth navegador-vs-API: **INTEGRADO** con un helper `wants_html()` — `Accept: text/html` → 307 a
  `/login` (UX de Apollo); cualquier otra cosa → 401 (lo que exigen upstream y sus tests).
- **Subsistema CSRF: cedió Sunshine**, y se aplicó también a TODOS los endpoints mutantes que son solo de
  Apollo (`reorderApps`, `deleteApp`, `launchApp`, `disconnect`, `getOTP`, `quit`).
- `get_client_id`: **INTEGRADO** — prefiere la cookie de sesión hasheada, luego el usuario Basic, luego IP.
  La versión de Sunshine habría degradado a per-IP bajo auth por cookie.
- `saveApp`/`deleteApp`: **conservó Apollo** (modelo por UUID vía `proc::migrate_apps`). Se descartó el
  `DELETE /api/apps/(n)` por índice de Sunshine: los índices están superados por los UUIDs.
- `updateClient`: **INTEGRADO** — `nvhttp::update_device_info` de Apollo (permisos granulares, do/undo cmds,
  display_mode, virtual display) **+** el toggle `enabled` de Sunshine con `terminate_sessions_by_cert`.
- `savePin`: **INTEGRADO** — cuerpo de Apollo + validación 0000-9999 de Sunshine + el `return;` que falta tras
  `bad_request` (bug de upstream).
- Entran nuevas de Sunshine: `browseDirectory`, `getCover`, `getViGEmBusStatus`/`installViGEmBus`, `getAsset`.
- Verificado con `g++-14 -std=c++20 -fsyntax-only` real (rc=0).
- Riesgos: `/logout` es cosmético (la página de Sunshine cierra sesión envenenando credenciales Basic; con
  cookies no limpia `sessionCookie` → falta un `/api/logout` real). El `WWW-Authenticate` en el 401 puede
  disparar el diálogo Basic del navegador en XHR tras expirar la sesión (aceptado para pasar los tests de upstream).

## RESULTADO DEL BUILD
`cmake` configure EXIT 0 + `make -j` completo: **`sunshine` linkeado (42.5 MB), 0 warnings, target `web-ui` OK**.
Toolchain: gcc-14, Release, `BUILD_DOCS=OFF BUILD_TESTS=OFF BUILD_WERROR=OFF SUNSHINE_ENABLE_CUDA=OFF`.
Dir de build NUEVO `build-helios3/` (no se reusó cache, por el gotcha de rutas horneadas).
**El daemon en vivo NO se tocó**: sigue corriendo `build-helios2/sunshine` sin reinicios, y ningún archivo de
`~/.config/sunshine` fue modificado (verificado por mtime).


## ✅ Cierre del slice — todo lo que salía de aquí YA SE RESOLVIÓ
**PR #12 mergeado con merge commit `8653a268`** (NO squash: un squash habría aplanado el merge sin
segundo padre y el merge-base habría vuelto a 2025-09-26, deshaciendo el objetivo entero. Jordi:
*"te doy toda la razón: este merge NO va con squash"*).
**Verificado:** merge-base `1a96d135` (2025-09-26) → **`6f58be35`**, `develop..sunshine/master` = **0**.

Lo que este slice dejó pendiente se despachó el 27-jul (registro en [[bitacora]]):
- **Desplegar para QA** → hecho: `build-helios3` y luego `Helios/build-helios4`.
- **Flip a `CUDA=ON` para el 4:4:4** → ⚠️ **la premisa era FALSA.** Este archivo afirmaba que era "lo
  ÚNICO que falta para que el 4:4:4 esté disponible". **No lo es:** el host anuncia 4:4:4 con
  `CUDA=OFF` (ver la sección de refutación arriba). El flip quedó como optimización de rendimiento y
  bajó de prioridad → [[estado-proyecto]].
- **Limpieza de ~7.4 MB** de dirs sin trackear que Sunshine eliminó
  (`packaging/linux/flatpak/deps/shared-modules`, `third-party/googletest`, `nv-codec-headers`,
  `nvapi-open-source-sdk`) → sigue sin ejecutar; es cosmético y requiere OK.

> **Pendientes vivos: solo en [[estado-proyecto]].** Este archivo es el registro del merge, no un backlog.
