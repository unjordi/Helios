---
name: build-selene-linux-headless
description: Receta para compilar Selene (cliente, fork moonlight-qt) HEADLESS en la caja CachyOS — qmake6 + prefixes PRIVADOS de SDL2_ttf y vulkan-headers que NO existen como dev-packages del sistema. Reconstruida el 2026-07-24 (antes solo existía el binario, sin receta).
metadata:
  type: reference
---

Cómo compilar el cliente **Selene** en Linux (CachyOS) para el binario headless de pruebas
(`~/.cache/selene-e2e/bin/selene`, el que usa `wolf-deploy/demo-wolf-multisesion.sh`).
**Reconstruida el 2026-07-24** debugueando el build roto: antes solo existía el binario (jul-19) sin receta,
y el drift de toolchain (Qt 6.11 + gcc-16) + prefixes a medias lo tumbaban.

## El comando
```bash
BUILD=<dir-de-build-FUERA-del-repo>            # p.ej. scratchpad
export PKG_CONFIG_PATH=/home/unjordi/.cache/selene-e2e/sdlttf-prefix/lib/pkgconfig:$PKG_CONFIG_PATH
export CPATH=/home/unjordi/.cache/selene-e2e/vkh/include:$CPATH
( cd "$BUILD" && qmake6 /home/unjordi/code/HeliosSelene/Selene/artemis.pro \
    CONFIG+=release CONFIG+=disable-wayland CONFIG+=disable-libdrm CONFIG+=disable-cuda \
  && make -j$(nproc) )
# binario resultante: $BUILD/app/artemis   (el TARGET quedó "artemis" en la rama chore/ci-triage;
# el rename a "selene" vive en develop — cosmético, se copia al destino como 'selene')
```

## Las 2 deps que NO vienen del sistema (y POR QUÉ) — la trampa
Esta caja usa **sdl2-compat (API SDL2 sobre SDL3)** y **NO tiene `SDL2_ttf`-dev ni `vulkan-headers`** del sistema
(hay SDL3_ttf, no SDL2_ttf). Por eso el build e2e usa **prefixes privados** en `~/.cache/selene-e2e/`:
1. **SDL2_ttf** → `sdlttf-prefix/` (lib `libSDL2_ttf-2.0.so.0.2400.0` = v2.24.0 + `lib/pkgconfig/SDL2_ttf.pc`).
   El `.pro` lo pide con `PKGCONFIG += SDL2_ttf`. **Gotchas reparados 2026-07-24:**
   - el `.pc` traía un `prefix=/tmp/.../scratchpad/...` **STALE** de una sesión vieja → corregido al path real del cache.
   - **faltaba el header** (solo estaban `.so` + `.pc`) → bajado de la release que casa con la lib:
     `curl -sL https://raw.githubusercontent.com/libsdl-org/SDL_ttf/release-2.24.0/SDL_ttf.h -o <prefix>/include/SDL2/SDL_ttf.h`
2. **Vulkan headers** → `vkh/include/` (`vulkan/vulkan.h`, `vk_video/`). libplacebo hace `#include <vulkan/vulkan.h>`
   y el sistema no los trae → van por **`CPATH`**.

## Runtime (correr el binario headless)
```bash
export LD_LIBRARY_PATH=/home/unjordi/.cache/selene-e2e/sdlttf-prefix/lib   # resuelve libSDL2_ttf
export QT_QPA_PLATFORM=offscreen           # o Xvfb (el demo levanta Xvfb :121/:122)
export VK_ICD_FILENAMES=/nonexistent/none.json LIBGL_ALWAYS_SOFTWARE=1     # neutraliza Vulkan NVIDIA en el cliente
selene list <host:puerto>      # o pair / stream
```

## Toolchain
`qmake6` = **Qt 6.11.1** (sistema); **gcc 16.1.1** (default). Selene SÍ compila con gcc-16 (a diferencia de Helios,
que exige gcc-14). Los warnings `[[nodiscard]]` de `QFile::open` (Qt 6.11) son benignos — no hay `-Werror` general.

Relacionado: [[build-selene-macos]] (build nativo en la Mac), [[deploy-streaming-y-resolucion]], [[forks-helios-selene]].
