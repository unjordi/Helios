# Native Virtual Display on Linux — Design & Work Plan

> Status: **Planning** · Owner: Helios maintainers · Flagship feature of the Helios fork
> Tracking upstream: Apollo [#1161](https://github.com/ClassicOldSong/Apollo/issues/1161), [PR #1477](https://github.com/ClassicOldSong/Apollo/pull/1477), [#1414](https://github.com/ClassicOldSong/Apollo/issues/1414), [#1427](https://github.com/ClassicOldSong/Apollo/issues/1427); Sunshine [PR #4762](https://github.com/LizardByte/Sunshine/pull/4762).

## 1. Goal

When a client (Selene/Artemis/Moonlight) connects, Helios should **create a virtual display that exactly matches the client's resolution / refresh / HDR**, capture and stream it, and **tear it down on disconnect** — without touching or disturbing the host's physical monitor. This is parity with the Windows SudoVDA path, on Linux/Wayland.

Reaching this dissolves a whole cluster of problems at once (they are all *symptoms of the missing virtual display*):

- The external `gamescope + KMS-capture` workaround currently needed to render at client resolution.
- Physical-monitor disruption (mode changes on the real ultrawide).
- The 240 Hz + HDR HDMI-bandwidth flicker caused by re-driving the physical link.
- The `kscreen-doctor` resolution scripts and the session lock/unlock dance.

## 2. Current state (why this doesn't work today)

| Layer | Windows | Linux |
|---|---|---|
| VD creation | `process.cpp` `#ifdef _WIN32` → `VDISPLAY::createVirtualDisplay()` (SudoVDA driver) | `#else` branch only calls `display_device::configure_display()` |
| Display device API | `libdisplaydevice` Windows impl (`src/windows/`) | `display_device.cpp` `create_settings_manager()` returns **`nullptr`** (no Linux impl) |
| Client capability flag | advertised via `nvhttp.cpp` (inside `#ifdef _WIN32`) | **not advertised** → Selene/Artemis show *"server doesn't support virtual displays"* |
| Net effect of `virtual-display: true` | works | **no-op** |

So on Linux, `virtual-display: true` does nothing, and even if it did, the capability is never advertised to the client. Today's stopgap is an external dedicated `gamescope` session rendered at the client resolution and captured via KMS — functional but with the side effects listed in §1.

## 3. Chosen approach

**Adopt and finish the EDID-override approach from Apollo PR #1477, and add a pluggable external-hook fallback (Sunshine #4762 pattern) for the cases EDID-override structurally can't serve.**

### 3.1 Why EDID-override (and why not the alternatives)

The upstream design discussion (#1161, 42 comments) converged on this trade-off space:

- **evdi (DisplayLink-style):** rejected. Its framebuffer lives in system RAM, so GPU encoding needs VRAM→RAM→VRAM copies — wasted bandwidth/latency over PCIe.
- **gamescope / Wolf / nested compositors:** copy overhead, and they create an *isolated* session — no good for "remote-desktop / continue where I left off". (This is exactly what Jordi's current hack is, with its monitor side effects.)
- **Native DRM virtual connector (`vkms` / writeback / a kernel patch):** the technically *correct* long-term answer, but kernel/DRM patches have been rejected upstream; not a path we can land soon.
- **EDID-override via `debugfs`:** **no kernel module**, reuses the existing `kmsgrab` capture path, works on AMD/Intel + KDE/GNOME Wayland today. The Apollo maintainer called it *"a plausible solution to Linux without touching kernel source."* → **this is our base.**

### 3.2 How it works (PR #1477 mechanics)

1. Generate a 256-byte **EDID 1.4** on the fly for the client's resolution/refresh — CVT Reduced-Blanking timings, CTA extension (VIC + LPCM audio). The EDID **serial is a hash of the `client_id`** so the compositor remembers each client's layout (`hash_client_id`).
2. Find a **disconnected DRM connector** (`find_disconnected_connectors`, iterating `/dev/dri/card*`). Auto-priority HDMI-A > DP > HDMI-B > DVI, or forced via config (`vdisplay_connector`, e.g. `card0-HDMI-A-2`).
3. Inject via a **privileged helper** (`apollo-vdisplay-helper`, `cap_dac_override+ep`, `root:apollo 0750`, path-allowlisted): write `edid_override`, force `status=on`, `trigger_hotplug` under `/sys/kernel/debug/dri/<N>/<connector>/`.
4. Wait for the compositor to assign a CRTC (`wait_for_connector_active`, poll ≤5 s).
5. Compute the **KMS index** the same way `kmsgrab.cpp` does (`find_kms_index`) and set `config::video.output_name` to it, so the existing KMS capture grabs the virtual connector.
6. On teardown: clear the override (`status=detect`, hotplug), restore `output_name`; persist state to `vdisplay.state` and `recover_crash_state()` on next start.

Dependencies: **libdrm + libcap** (already used by kmsgrab) and `xrandr` at runtime for X11. **No kernel module.** Requires `debugfs` mounted and **a physically disconnected connector available** on the GPU.

### 3.3 The pluggable fallback (for what EDID-override can't do)

EDID-override needs a free disconnected connector — it can't serve **headless VMs** or machines with no spare connector. For those, adopt the Sunshine #4762 pattern:

- `output_name` accepts a **connector name** (e.g. `DP-2`), not just a numeric index (`resolve_display_name()`).
- A **`pre_probe_cmd`** hook (do/undo, same shape as `global_prep_cmd`, exposing `SUNSHINE_CLIENT_WIDTH/HEIGHT/FPS/HDR`) that runs *before* encoder probing, so a user's own virtual monitor (`krfb-virtualmonitor`, a dummy plug, `kscreen-doctor`, or a VM-manager display) can be enabled on connect and disabled on disconnect.

This effectively **formalizes Jordi's current external-script approach into a first-class, supported hook** — a clean migration path, and it's aligned with the direction the Apollo maintainer said he prefers (pluggable backends).

## 4. Architecture map (integration points)

Capture backend selection — `src/platform/linux/misc.cpp:955` `display()` — tries **NvFBC → Wayland(wlr-screencopy) → KMS → X11**. On KDE Wayland the default is **wlr-screencopy** (`wl_display()` by output *name*), with KMS as fallback.

| Concern | File:symbol | Note |
|---|---|---|
| Backend selector | `src/platform/linux/misc.cpp:955` `display()` | central switch; `init()` ~`:1008` fills the `sources` bitset |
| KMS capture / monitor pick | `src/platform/linux/kmsgrab.cpp:589` `display_t::init()` (index via `util::from_view(display_name)`); enum `kms_display_names():1572` | #1477 aligns its index with this |
| Wayland capture / outputs | `src/platform/linux/wlgrab.cpp:373` `wl_display()`, `:396` `wl_display_names()` | selects by output **name** (`HDMI-1`, `DP-2`) |
| **Win/Linux VD bifurcation** | `src/process.cpp:236` `#ifdef _WIN32 … #else …` | where the Linux VD create/destroy must hook in |
| **Client capability flag** | `src/nvhttp.cpp` (VD support inside `#ifdef _WIN32`) | must extend to Linux so Selene shows the toggle |
| Display-device API | `src/display_device.cpp:616` `create_settings_manager()` → `nullptr` on Linux | VD is separate from this; out of scope to fully implement |
| Config | `src/config.{h,cpp}` (`video_t`) | add `linux_virtual_display_experimental`, `vdisplay_connector` (per #1477) |
| New module | `src/platform/linux/virtual_display.{cpp,h}`, `vdisplay_helper.cpp` | from #1477 |

> ⚠️ **Open integration question:** #1477 captures the virtual connector via **KMS** (`kmsgrab`). But on KDE Wayland the *default* capture backend is **wlr-screencopy**, which selects by output name, not KMS index. We must verify the virtual connector is the one actually captured — either force the KMS backend for VD sessions, or confirm the wlr path enumerates the new output and capture it by name. **This is the first thing Phase 0 must settle on real hardware.**

## 5. Work plan (phased)

Each phase is a branch → PR into `develop`. Host (Helios) and client (Selene) changes that touch the Moonlight protocol/capability handshake must land **aligned** (that's why we keep the forks in lockstep).

### Phase 0 — Reproduce & evaluate #1477 on our hardware
- Rebase PR #1477 (`AdivonSlav:vdisplay-linux`) onto Helios `develop` in a branch.
- Build (gcc-14, `SUNSHINE_BUILD_DRM`) and test on the CachyOS host + KDE Wayland (the 3440×1440 ultrawide rig).
- **Settle the capture-backend question** (§4 warning): is the virtual connector captured via wlr or KMS? Force/select accordingly.
- **Acceptance:** a virtual display appears at the client's resolution, is captured and streamed to Selene, and is removed cleanly on disconnect — *without* changing the physical monitor's mode.
- Confirm the host GPU has a **free disconnected connector**; record GPU vendor (drives the NVIDIA risk).
- Output: go/no-go on the EDID-override base + a list of concrete breakages on our stack.

### Phase 1 — Client capability parity (unblock on-demand)
- Extend the VD capability flag in `nvhttp.cpp` to Linux (remove/condition the `#ifdef _WIN32`) so the host advertises VD support.
- Verify **Selene** honors the advertised capability and shows the toggle (host↔client aligned change; validate the handshake end-to-end).
- **Acceptance:** Selene offers the virtual-display toggle against a Helios Linux host; on-demand (not just `always_use_virtual_display`) works.

### Phase 2 — Integrate & harden the lifecycle
- Wire VD create/destroy into the `process.cpp` Linux branch, mirroring Windows (`headless_mode || launch_session->virtual_display || _app.virtual_display`).
- **HDR:** encode the client's HDR capability into the generated EDID (CTA HDR static-metadata block) so the virtual display is HDR-capable per client — directly targets the dark/handshake HDR pain on KDE.
- **Permissions UX:** fix the helper perms friction (0750 + group `apollo` needing relogin), wire `sysusers`/`postinst` for Arch/CachyOS, document it.
- **Acceptance:** clean connect/disconnect across repeated sessions; HDR client gets an HDR virtual display; no manual `chmod` needed after install.

### Phase 3 — Pluggable fallback (headless / no free connector)
- Implement connector-name `output_name` resolution + a `pre_probe_cmd` do/undo hook (Sunshine #4762 pattern), exposing `SUNSHINE_CLIENT_*`.
- Migrate Jordi's existing external gamescope/kscreen scripts onto this supported hook as the reference example.
- **Acceptance:** a headless VM (or a box with no free connector) can stream at client resolution via a user-provided virtual monitor through the hook.

### Phase 4 — Physical-monitor orchestration & polish
- Optionally auto-disable physical outputs during a VD session **without DPMS** (DPMS also blanks the VD); KDE via `kscreen-doctor`, generic via DRM.
- Guard against the **OpenGL-with-only-VD** failure (#1414): never leave zero valid displays for GL/Vulkan contexts.
- Multi-client identity (EDID serial = `hash(client_id)`) and multi-instance support.

## 6. Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| NVIDIA: `nvidia-drm` EDID-override shaky; `trigger_hotplug` fails | Med–High | polling fallback (KWin picks up via DRM poll ~400 ms); validate on the host GPU; document NVIDIA caveats |
| Capture-backend mismatch on KDE Wayland (wlr vs KMS) | High | resolve in Phase 0 before anything else |
| No free disconnected connector (headless VM, some laptops) | Structural | Phase 3 pluggable fallback |
| OpenGL/Vulkan context fails with only a virtual display (#1414) | Med | keep a valid display present; test GL titles |
| X11 path (`xrandr`) untested in #1477 | Med | validate or scope out; we are Wayland-first |
| Upstream goes "pluggable backends" instead of merging #1477 | — | we do **both** (adopt #1477 + pluggable hook), so we're hedged and can contribute back |
| 240 Hz + HDR HDMI link flicker | Med | should disappear once we stop re-driving the *physical* link; if it persists it's genuinely host-side bandwidth — track separately |

## 7. Testing matrix

GPU {AMD, Intel, NVIDIA} × session {KDE Wayland, GNOME Wayland, X11} × case {free connector, headless} → record result. Primary target first: **AMD/Intel · KDE Wayland · free connector** (Jordi's rig).

## 8. Open questions / decisions

- Which GPU does the Helios host use? (sets NVIDIA-risk priority)
- Does that GPU expose a free **disconnected** connector?
- KDE Wayland capture path on the host: KMS or wlr? (Phase 0)
- **Collaborate vs diverge:** recommend collaborating with `AdivonSlav` on #1477 (push our fixes upstream / preserve authorship) rather than hard-forking the work — keeps Helios a good citizen and reduces our maintenance load.

## 9. References

- Apollo PR #1477 — *Add experimental virtual display support for Linux* (author `AdivonSlav`). Key files: `src/platform/linux/virtual_display.cpp`, `vdisplay_helper.cpp`, `virtual_display.h`, integration in `src/process.cpp`.
- Apollo issues #1161 (design thread), #1414 (OpenGL-on-VD), #1427 (headless VM).
- Sunshine PR #4762 — connector-name `output_name` + `pre_probe_cmd` (the pluggable pattern).
