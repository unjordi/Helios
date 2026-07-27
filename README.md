# Helios

> **Helios** is a community-maintained fork of [**Apollo**](https://github.com/ClassicOldSong/Apollo) by [ClassicOldSong](https://github.com/ClassicOldSong) — which is itself a fork of [**Sunshine**](https://github.com/LizardByte/Sunshine) by [LizardByte](https://github.com/LizardByte). Full credit for the foundation goes to both upstream projects. Helios exists to keep the host actively maintained and extended, with a particular focus on **native Linux/Wayland virtual-display support**. Its sibling client is [**Selene**](https://github.com/unjordi/Selene).

Helios is a self-hosted desktop stream host for [Selene](https://github.com/unjordi/Selene), [Artemis (Moonlight Noir)](https://github.com/ClassicOldSong/moonlight-android), and other Moonlight-protocol clients. It offers low latency, native client resolution, and hardware encoding on AMD, Intel, and Nvidia GPUs (software encoding is also available), with a web UI for configuration and client pairing from any browser.

Major features:

- [x] Per-client permission management
- [x] Clipboard sync
- [x] Commands on client connect/disconnect (e.g. [auto pause/resume games](https://github.com/ClassicOldSong/Apollo/wiki/Auto-pause-resume-games))
- [x] Input-only mode
- [x] Built-in Virtual Display with HDR that auto-matches the client's resolution/framerate — **Windows today; native Linux/Wayland support is this fork's flagship effort** (see [Focus](#focus-of-this-fork))

## Focus of this fork

Helios prioritizes the **Linux/Wayland** host experience that upstream never fully landed, plus first-party docs. In order:

1. **Native virtual display on Linux** — render at the client's exact resolution without disturbing your physical monitor. Today this needs external workarounds (e.g. a dedicated `gamescope` session captured via KMS); the goal is to make it native, the way SudoVDA already works on Windows. Tracking: virtual display on Linux ([Apollo #1161](https://github.com/ClassicOldSong/Apollo/issues/1161), [PR #1477](https://github.com/ClassicOldSong/Apollo/pull/1477)), OpenGL-on-VD ([#1414](https://github.com/ClassicOldSong/Apollo/issues/1414)), headless ([#1427](https://github.com/ClassicOldSong/Apollo/issues/1427)).
2. **First-party, Linux-first documentation** (below) instead of only pointing at upstream.

Windows remains fully supported as inherited from Apollo/Sunshine — see [Downloads](#downloads).

## Quick start (Linux)

1. Install Helios (build [from source](#building-from-source-linux) for now; packaged releases are on the way).
2. Start the service:
   ```bash
   systemctl --user enable --now apollo.service   # the binary is still named sunshine/apollo pending a binary rebrand
   ```
3. Open the web UI at **https://localhost:47990** and set your username/password on first run.
4. Pair your client (Selene/Artemis/Moonlight): use the PIN flow in the UI, or enter the host address on the client.

> [!NOTE]
> The **first** client paired gets **full** permissions; later clients only get `View Streams` + `List Apps`. If you hit `Permission Denied` launching an app, or can't use mouse/keyboard from a new client, grant that device the matching permission in the web UI. More: [Permission System wiki](https://github.com/ClassicOldSong/Apollo/wiki/Permission-System).

## Building from source (Linux)

First-party steps, verified on **Arch / CachyOS**. (For Debian/Fedora/Ubuntu the dependency names differ — see `packaging/linux/` and the upstream build reference linked under [Documentation](#documentation).)

### Dependencies (Arch / CachyOS)

```bash
# Build tooling — upstream pins gcc-14 (a newer gcc, e.g. 16, currently breaks the build)
sudo pacman -S --needed gcc14 cmake make nodejs npm git appstream appstream-glib \
  desktop-file-utils python-jinja shaderc

# Runtime / library deps
sudo pacman -S --needed avahi curl libcap libdrm libevdev \
  libmfx libnotify libpulse libva libx11 libxcb libxfixes libxrandr libxtst \
  miniupnpc numactl openssl opus pipewire qt6-base qt6-svg vulkan-icd-loader wayland
```

> [!NOTE]
> A few of these are new since the Sunshine sync: `qt6-base`/`qt6-svg` (the tray icon moved to a Qt
> backend and no longer uses `libayatana-appindicator`), `pipewire` (required by the KWin ScreenCast
> and XDG Desktop Portal capture backends, both on by default), `shaderc` + `vulkan-icd-loader`
> (Vulkan video encoding, on by default — pass `-DSUNSHINE_ENABLE_VULKAN=OFF` to skip) and
> `python-jinja` (generates the glad GL/EGL loader).

### Build

```bash
git clone --recurse-submodules https://github.com/unjordi/Helios.git
cd Helios

cmake -S . -B build -Wno-dev \
  -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DBUILD_WERROR=OFF \
  -DSUNSHINE_ENABLE_CUDA=OFF -DCUDA_FAIL_ON_MISSING=OFF

make -C build -j"$(nproc)"
```

The binary lands at `build/sunshine`. Run it directly to test, or install with `make -C build install`.

> [!TIP]
> `SUNSHINE_ENABLE_CUDA` does **not** gate NVENC — NVENC goes through FFmpeg (`h264_nvenc`,
> `hevc_nvenc`, `av1_nvenc`) and works with the flag off, just without zero-copy (you'll see a benign
> `Attempting to use NVENC without CUDA support` warning). What the flag does gate is the CUDA/OpenGL
> interop path, and with it **NvFBC capture and 4:4:4 chroma on NVENC**. If you want either, install
> `cuda` and drop `-DSUNSHINE_ENABLE_CUDA=OFF`. On AMD, `libva-mesa-driver` enables VAAPI encoding
> (which has its own HEVC 4:4:4 profiles).

### Capture backends on Linux

Beyond KMS/DRM, wlroots Wayland and X11, the host now also supports **KWin ScreenCast** (a direct
Wayland protocol that bypasses the portal — the KDE/Plasma path) and the **XDG Desktop Portal**. Both
are built by default and both need PipeWire; turn them off with `-DSUNSHINE_ENABLE_KWIN=OFF` /
`-DSUNSHINE_ENABLE_PORTAL=OFF`. Encoding can go through VAAPI, NVENC or **Vulkan Video**.

## Documentation

First-party documentation lives in this repo (the [Quick start](#quick-start-linux) and [build](#building-from-source-linux) sections above; more is being migrated). For deeper, not-yet-re-documented topics from the Sunshine era, the upstream [Sunshine docs](https://docs.lizardbyte.dev/projects/sunshine) remain a useful reference — but they are no longer the primary doc for Helios.

## About the Virtual Display

> [!NOTE]
> **TL;DR** Treat your Selene/Artemis/Moonlight client like a dedicated PnP monitor.

On **Windows**, Helios uses **SudoVDA** for the virtual display: auto resolution/framerate matching, created when the stream starts and removed when it ends. It assigns a **fixed identity per client**, so Windows remembers each device's display configuration natively. If no virtual display appears/disappears with the stream, suspect a driver misconfiguration or a leftover persistent virtual display.

> [!WARNING]
> Remove any other virtual-display solutions from your system and config to avoid conflicts.

On **Linux/Wayland**, a native virtual display is **this fork's flagship work in progress** (see [Focus](#focus-of-this-fork)). Until it lands, render-at-client-resolution is achieved with external workarounds (e.g. a dedicated `gamescope` session captured via KMS).

## About HDR

HDR quality depends almost entirely on the **client**. Color correction (ICC) is essentially a client-side job while streaming HDR, not the host's.

On Windows, HDR requires Windows 11 23H2+ (24H2 recommended). On Linux/KDE, HDR capture is sensitive to the compositor version (e.g. dark-HDR regressions after some Plasma updates).

<details>
<summary>HDR caveats (worth reading)</summary>

If HDR looks dark/washed-out, that's usually the client tone-mapping SDR content to its panel's peak brightness — not a Helios bug. HDR remains a messy space across standards and devices; SDR often gives more stable, predictable color. Enable HDR only if you understand the trade-offs. Background: [Apollo issue #164](https://github.com/ClassicOldSong/Apollo/issues/164).

</details>

## Advanced

- **Dual-GPU laptops:** set `Adapter Name` to your dGPU and enable `Headless mode` in the `Audio/Video` tab, then restart — no dummy plug needed.
- **Multiple instances / multiple virtual displays:** [wiki guide](https://github.com/ClassicOldSong/Apollo/wiki/How-to-start-multiple-instances-of-Apollo).
- **Stuttering:** common causes and fixes in the [Stuttering Clinic](https://github.com/ClassicOldSong/Apollo/wiki/Stuttering-Clinic).
- **FAQ:** [wiki](https://github.com/ClassicOldSong/Apollo/wiki/FAQ).

## System requirements

> **Note:** work in progress — don't buy hardware based on this table.

| Component | Minimum |
|-----------|---------|
| GPU | AMD: VCE 1.0+ · Intel: VAAPI-compatible (Linux) / Skylake+ with QuickSync (Windows) · Nvidia: NVENC-capable |
| CPU | AMD Ryzen 3 / Intel Core i3 or higher |
| RAM | 4 GB+ |
| OS | Linux: Debian 13+ (trixie), Fedora 43+, Ubuntu 22.04+ · Windows 11+ · macOS 14.2+ · FreeBSD 14.4+ |
| Network | 5 GHz 802.11ac (host & client) |

For 4K, prefer wired CAT5e+ and a newer GPU (AMD VCE 3.1+, Intel HD 510+, Nvidia RTX 2000-series+ on
Linux / GTX 1080+ on Windows). For HDR: AMD VCE 3.4+, Intel UHD 730+, Nvidia Pascal/GTX 10-series+.

## Integrations

- **[Selene](https://github.com/unjordi/Selene)** — Helios' sibling desktop client (Windows/macOS/Linux/Steam Deck).
- **SudoVDA** — the Virtual Display Adapter driver used on Windows.
- **[Artemis (Android)](https://github.com/ClassicOldSong/moonlight-android)** — Android client with integrated virtual-display controls.

## Downloads

- **Build from source (Linux):** see [above](#building-from-source-linux) (recommended for now).
- **Releases:** [github.com/unjordi/Helios/releases](https://github.com/unjordi/Helios/releases) (packaged builds are on the way).

<details>
<summary>Windows (inherited from Apollo)</summary>

The upstream Apollo packages still work on Windows. In an elevated PowerShell:

```pwsh
winget install ClassicOldSong.Apollo
# or
choco upgrade apollo -y
```

Both are community-maintained. Native Helios Windows packages will follow.

</details>

## Support

Support is via GitHub **Issues** and **Discussions** on this repo. No real-time chat channels are provided.

## Acknowledgments

- **[ClassicOldSong](https://github.com/ClassicOldSong)** — creator of Apollo (this fork's direct upstream) and Artemis.
- **[LizardByte](https://github.com/LizardByte)** — for Sunshine, the foundation Apollo and Helios build on.

## License

GPLv3 — inherited from Sunshine. See [LICENSE](LICENSE).
