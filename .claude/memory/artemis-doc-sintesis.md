---
name: artemis-doc-sintesis
description: Síntesis de la documentación de Artemis (wjbeckett/artemis, padre de Selene) — qué añade sobre Moonlight-Qt, arquitectura, features, estado/limitaciones. Referencia del ecosistema Helios+Wolf+Selene.
metadata:
  type: reference
---

# Artemis (wjbeckett/artemis) — síntesis de documentación

> Padre directo de **Selene** (el cliente de Jordi). Selene = fork casi-idéntico rebrandeado de
> Artemis; el README de Selene ES el de Artemis con nombres cambiados, así que la doc local de
> `Selene/` refleja fielmente a Artemis. Fuentes citadas al pie.

## 1. Qué ES Artemis
Cliente **cross-platform de game streaming**, fork de **Moonlight-Qt** (moonlight-stream). Su
propósito declarado: *"llevar las features avanzadas de **Artemis Android** (de ClassicOldSong, el
fork de moonlight-android que acompaña a Apollo) al escritorio"*. Es decir, Artemis-Qt es el puente
que porta a desktop (Win/Mac/Linux/Steam Deck) las extensiones de protocolo que Apollo introdujo y
que solo existían en el cliente Android. Habla el **protocolo Moonlight/GameStream** + las
extensiones de Apollo. GPL-3.0. (`Selene/README.md`, `Selene/docs/DEVELOPMENT.md`)

## 2. EL DELTA sobre Moonlight-Qt (lo que AÑADE) — lo importante
Moonlight-Qt aporta el core (decode HW, H.264/HEVC/AV1, HDR, 7.1, multitouch, gamepad ×16). Artemis
le suma, sobre todo para hablar con **Apollo/Sunshine**:
- **Clipboard Sync** bidireccional — HTTP `actions/clipboard?type=text`; auto-sync al iniciar/reanudar
  stream y al perder foco; anti-loop por hash SHA-256 del contenido; límite 1 MB. Apollo-only.
- **Server Commands** — ejecutar comandos custom en el host; requiere permiso `server_cmd`. Apollo-only.
- **OTP Pairing** — pairing por One-Time Password: extiende el PIN estándar con parámetro `&otpauth=`,
  hash `SHA256(pin + salt + passphrase)`, PIN de 4 dígitos. Apollo-only.
- **Quick Menu** — overlay in-stream para acceder a controles sin salir del juego
  (teclado `Ctrl+Alt+Shift+\`; gamepad `Select+L1+R1+Y`).
- **Fractional Refresh Rate** — refresh custom client-side (90 Hz, 120 Hz…).
- **Resolution Scaling** — escalado de resolución client-side (rendimiento).
- **Virtual Display Control** — elegir si usar el display virtual del host.
- **UUID-Based App Launching** — identificación moderna de apps para Apollo/Sunshine, con **fallback
  automático** a IDs legacy si no hay UUID.
- **Permission Viewing** — ver permisos server-side del cliente (flags `clipboard_set/read`,
  `file_upload/download`, `server_cmd`).
- **Rebranding visual** — íconos oficiales de Artemis + compat con protocolo `art://` de Apollo Android.
- **Development Builds** — CI que hornea builds de todas las plataformas con changelog automático.
- **Optimización Steam Deck / Embedded Mode** — UI para handhelds, render GPU-eficiente, touch/gamepad.
- **Mejora propia de detección AV1/HDR** (no en moonlight-qt): fallback `AV1_MAIN10 → AV1_MAIN8`,
  override `FORCE_AV1_SUPPORT=1`, y el fix de que varios renderers reclamaban HDR sin implementar
  `setHdrMode()` (PlVk/D3D11VA). (`Selene/AV1_DETECTION_ANALYSIS.md`)

Casi todo el delta con "Apollo-only" **solo aplica contra hosts Apollo/Helios**; contra Sunshine puro
o GameStream muchas de estas features quedan inertes por diseño (el host no expone los endpoints).

## 3. Arquitectura y plataformas
- Base Qt6 (Qt 6.7+, 6.8+ recomendado) + FFmpeg + SDL2/SDL2_ttf + OpenSSL + Opus; build con `qmake6`
  sobre `artemis.pro`. Submódulo compartido **`moonlight-common-c`** (mismo que Helios/Moonlight).
- Componentes nuevos: `app/backend/clipboardmanager.*`, `servercommandmanager.*`, `otppairingmanager.*`,
  `app/settings/artemissettings.*` (QSettings); managers como servicios QObject expuestos a QML.
  (`Selene/docs/DEVELOPMENT.md`)
- Decode HW en Win/Mac/Linux; renderers DRM (Linux), PlVk (Vulkan/Steam Deck), D3D11VA (Windows),
  VTMetal (macOS), EGL (fallback sin HDR).
- **Plataformas:** Windows x64 **y ARM64** (Surface Pro X, Copilot+), macOS **universal** (Intel+Apple
  Silicon), Linux x64 (AppImage + Flatpak), Steam Deck. Se distribuye como app GUI; NO documenta un CLI
  headless pair/list/stream tipo `moonlight-embedded` — es la app Qt (el uso "headless en Linux" que
  hace Jordi en Selene es construcción propia, no una feature documentada de Artemis).

## 4. Compatibilidad con hosts
NVIDIA GameStream (legacy), **Sunshine** (LizardByte), **Apollo** (ClassicOldSong) y por herencia
**Helios** (fork de Apollo de Jordi). Las features avanzadas necesitan Apollo/Helios; contra Sunshine
funciona el streaming base. AV1 requiere Sunshine/Apollo + GPU de host compatible. YUV 4:4:4 y
multitouch de 10 puntos: Sunshine-only (heredado de Moonlight). No hay mención de **Wolf** en la doc
de Artemis: Wolf habla protocolo Moonlight, así que Selene debería conectarse, pero es territorio de
Jordi por validar, no algo que Artemis documente.

## 5. Limitaciones / estado del proyecto — CLAVE
- **Upstream INACTIVO/dormido.** Último commit en `wjbeckett/artemis` (rama `develop`): **2025-08-31**
  (verificado en el git local vía `upstream/develop`; el HEAD fue trabajo de renderer HDR, PR #43/#44).
  A fecha de hoy (2026-07) son ~11 meses sin actividad → confirma la nota del cerebro de que el upstream
  está muerto. (El README de GitHub sigue diciendo "actively maintained", pero eso es texto viejo.)
- **Doc interna desincronizada/optimista:** el `README` marca Phase 1–3 "COMPLETE" y muchas features
  ✅, pero `docs/DEVELOPMENT.md` (fechado 2024-12) dice "40% completo, Phase 3 en progreso" y lista la
  integración con Moonlight-Qt como pendiente. Contradicción → tratar los ✅ del README con cautela;
  varios `test_changes.md`/`AV1_DETECTION_ANALYSIS.md` son notas de depuración de features a medio
  cocer (bugs de binding QML de ClipboardManager, HDR que "se activaba" sin `setHdrMode`, AV1 mal
  detectado en Steam Deck/RTX). Es un fork joven, funcional pero con aristas.
- **En progreso / no terminado (Phase 4):** App Ordering custom y **Input-Only Mode** (stream de input
  sin video para escritorio remoto) siguen como 📋 planeados.
- **Notas operativas:** builds de dev de macOS salen "damaged" por quarantine → `xattr -cr Artemis.app`.

## 6. Relevancia para el ecosistema de Jordi
Selene = este código, rebrandeado y **revivido** (el upstream lleva ~1 año parado; Jordi es ahora
quien lo mantiene). Selene es el **cliente** que cierra el triángulo **Helios (host) + Wolf (headless)
+ Selene (cliente)**. El delta de Artemis (clipboard, OTP, server-commands, permisos, virtual-display,
refresh/res scaling, UUID launch) está pensado justo para hablar con **Apollo → Helios**, así que
host y cliente de Jordi comparten linaje y las extensiones de protocolo encajan. Puntos a vigilar por
ser fork joven + upstream muerto: seguir bajando fixes de **Moonlight-Qt** (el abuelo, sí activo) por
el submódulo/rebase, y validar Selene↔Wolf (no documentado por Artemis). El header de la doc de
DEVELOPMENT sigue apuntando a `wjbeckett/artemis` — al madurar Selene conviene reflejar el rebrand.

## Fuentes
- `/home/unjordi/code/HeliosSelene/Selene/README.md` (= README de Artemis rebrandeado)
- `/home/unjordi/code/HeliosSelene/Selene/docs/DEVELOPMENT.md` (plan de fases, arquitectura, protocolo)
- `/home/unjordi/code/HeliosSelene/Selene/AV1_DETECTION_ANALYSIS.md`, `test_changes.md` (estado real / bugs)
- git local: `upstream/develop` → último commit **2025-08-31** (remoto `git@github.com:wjbeckett/artemis.git`)
- Web: `https://github.com/wjbeckett/artemis` (README + historial de commits)
