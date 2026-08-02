---
name: build-selene-macos
description: Cómo compilar Selene (cliente Qt) nativo en la Mac de Jordi — Qt vía aqt + strip de AGL
metadata:
  type: reference
---

# Build de Selene (cliente) nativo en macOS (Apple Silicon)

Verificado 2026-07-18 y RE-VERIFICADO 2026-07-25 en la Mac de Jordi (`unjordi@192.168.1.84`, macOS 26.x arm64,
M4 Pro), build OK end-to-end. Tras el rebrand (en develop) la salida es **`Selene.app/Contents/MacOS/Selene`**
(arm64, ~4.1 MB; ya NO `Artemis`), `make_rc=0`. El repo en la Mac vive en `~/code/HeliosSelene/artemis` (la
carpeta conserva el nombre viejo, pero `origin`=unjordi/Selene y builda develop). Qt vía aqt 6.8.3.
(Existe un clon viejo `~/selene-test` en `chore/ci-triage` — basura inofensiva, candidato a limpieza.)

> **VALIDADO POR USO REAL (no "sin validar").** El fix de macOS (PR #4 de Selene: scope `macx` de
> `globaldefs.pri` + strip de AGL) está **confirmado en el mundo real**: el `Selene.app` de la Mac es el
> **cliente que decodifica HEVC (VideoToolbox, M4 Pro) en CADA prueba multi-sesión de 2 IPs**. No es un
> build teórico. Lo frágil es la RECETA (Qt vía aqt, strip de AGL de los `.prl`), pero el binario que
> produce está **verde en uso real**.

## El Qt de Homebrew estaba ROTO — usar aqt, NO brew
El `qt` de Homebrew es keg-only meta-paquete: `qmake6` queda como symlink roto y los submódulos
fallan con `Could not find feature thread` / `Could not find qmake spec 'macx-clang'`. **No pelear
con brew.** En su lugar, Qt limpio con aqt (misma versión que el CI, 6.8.3):

```bash
python3 -m pip install --user aqtinstall
python3 -m aqt install-qt mac desktop 6.8.3 clang_64 -m qtmultimedia --outputdir ~/Qt
# -> ~/Qt/6.8.3/macos/bin/qmake  (reporta 6.8.3, funciona sin QMAKEPATH ni hacks)
```

## AGL: hay que quitarlo del Qt instalado (Apple lo eliminó del SDK)
Qt 6.8.3 referencia `-framework AGL` en los `.prl` **internos de cada framework**
(`lib/Qt*.framework/Versions/A/Resources/*.prl`, no solo `lib/*.prl`) → `ld: framework 'AGL' not
found`. Un `QMAKE_LIBS_OPENGL -= -framework AGL` en globaldefs.pri **NO sirve** (AGL entra por los
.prl). Fix: correr el script del repo contra el Qt:

```bash
bash scripts/macos-strip-agl.sh ~/Qt/6.8.3/macos   # quita AGL de TODOS los .prl + mkspecs
```
(En CI lo hace `.github/workflows/dev-build.yml` tras "Setup Qt", en ambos jobs macOS.)

## Compilar
```bash
export PATH=~/Qt/6.8.3/macos/bin:$PATH
mkdir -p ~/selene-build && cd ~/selene-build
qmake ~/code/HeliosSelene/artemis/artemis.pro -spec macx-clang CONFIG+=release
make -j$(sysctl -n hw.ncpu)     # ~15-20 min full; relink solo ~1-2 min
```
También necesita el fix de `qyieldcpu` (ya en globaldefs.pri: `-Wno-error=implicit-function-declaration`).

Relacionado: [[forks-helios-selene]]. El rebrand a "Selene" vive en develop (PR #10 merged) — para
un `.app` que diga Selene, compilar `develop` (no `chore/ci-triage`).
