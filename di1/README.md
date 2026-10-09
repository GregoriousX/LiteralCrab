# LiteralCrab DI1 display ASI

This is a display-only port of [CrabeLoader's dev branch](https://github.com/LucasLhomme/CrabeLoader/tree/dev), based on commit `7f251e8eec5de534a2ed9b8efa0694f259706f0f` (8 October 2026). The output is **LiteralCrab.asi**, a 32-bit ASI plugin for **Disney Infinity 1.0 Gold Edition on PC**. DI1 uses Direct3D 9/9Ex; the DI3 Direct3D 11 swap-chain hooks were replaced with creation/reset/presentation hooks for both D3D9 paths.

## Install

Place `LiteralCrab.asi` and `LiteralCrab.ini` beside `DisneyInfinity1.exe`. A compatible **32-bit ASI loader** is required; Ultimate ASI Loader installed as `dinput8.dll` was used for testing. Keep the game's original `binkw32.dll` in place. This plugin exports `InitializeASI`, and has no Bink exports or Bink forwarding code.

The included PowerShell installer validates the executable and preserves the known older `DisneyInfinity4K.asi` as `DisneyInfinity4K.asi.disabled` to avoid running two resolution mods together:

```powershell
.\install.ps1 -GameDirectory 'C:\Games\Disney Infinity Gold Edition'
```

For a source checkout, pass `-ArtifactDirectory .\build-di1\Release` from the repository root. An existing `LiteralCrab.ini` is preserved. The installer also migrates the recognized pre-release `CrabeLoaderDI1.asi` name and copies its settings if no LiteralCrab INI exists. Disabled ASIs end in `.disabled`, so the loader cannot load two copies. To uninstall, move `LiteralCrab.asi` out of the game directory and, if desired, restore the saved previous mod. The install receipt records backup names.

## Settings

Edit the `[Display]` section of `LiteralCrab.ini` and restart the game:

| Key | Values | Effect |
| --- | --- | --- |
| `Mode` | `borderless`, `windowed`, `fullscreen` | Default: borderless. Fullscreen uses a real exclusive D3D9 device. |
| `FrameLimit` | `0`, or `15`–`1000` | Default: 120 FPS. `0` is uncapped; 144, 165, 240 and higher caps are supported. |
| `VSync` | `auto`, `off`, `on` | `auto` follows upstream: bypass sync when uncapped or the cap is at least the monitor refresh. `off` enables presentation above the display refresh; `on` synchronizes. |
| `UnlockResolutions` | `0`, `1` | Adds plausible monitor modes to the game's existing resolution choices and accepts them at startup. |
| `FixFOV` | `0`, `1` | Preserves the native 16:9 view/zoom. Wider screens gain horizontal view; narrower screens gain vertical view. Compensates projection when window and buffer shapes differ. |
| `KeepVisibleOnFocusLoss` | `0`, `1` | Prevents automatic minimize/hide in borderless/windowed while forwarding native focus and pause events. Manual minimize remains available. |
| `ScreenshotHotkeys` | `0`, `1` | Allows Windows-key capture shortcuts in borderless/windowed by removing the native `DISCL_NOWINKEY` flag. Keyboard polling and bindings stay native. |
| `FixUIAspect` | `0`, `1` | Centres full-screen Scaleform UI in a 16:9 area on other aspect ratios. Sub-rectangles, split-screen and explicitly placed movies are unchanged. |
| `ScaleMouse` | `0`, `1` | Maps client pixels to render pixels when the window and back buffer differ. |
| `AltEnter` | `0`, `1` | Toggles borderless/windowed. Exclusive fullscreen keeps native hotkey handling. |
| `RememberMode` | `0`, `1` | Persists Alt+Enter changes. |

Example for 240 FPS:

```ini
[Display]
Mode=borderless
FrameLimit=240
VSync=off
```

Choose resolution in the game's own graphics menu. Available modes come from the display driver; sizes from 480 to 32767 per side are accepted without an aspect-ratio restriction. Actual supported sizes remain subject to the graphics driver and hardware. The engine's render-target sizes remain consistent with its own buffers.

`FixFOV=1` retains the game's 16:9 vertical FOV and zoom on ultrawide screens (Hor+). At narrower ratios, it expands vertical FOV to retain the baseline horizontal view (Vert+). It applies only to the measured render-camera projection calls, and never overrides the game's camera controls, movement, near/far planes, character behavior, clocks, simulation steps, save data, gameplay Lua or assets. Orthographic projections stay native.

Use `Mode=borderless` for Alt+Print Screen, ShareX or a snipping overlay. The D3D9 device is physically windowed, the window is non-topmost and stays visible when capture tools take focus. The native game pause on focus loss is preserved. The GPU probe verified rendered pixels through GDI capture before and after native focus changes; ShareX and the system clipboard hotkey have not been tested directly in gameplay.

A frame limit is a ceiling, not a promise that a particular scene or GPU will reach it. Native startup movies keep their original frame timing.

## Compatibility and diagnostics

The measured executable has PE timestamp `0x58535B84`, image size `0x1511000`, and SHA-256 `856e9d3c38f74fcd778109fa46591b092e431989dd8ab269eccf4a7f7d48474f`. Unknown builds are refused. The engine detours check full instruction patterns and their display call sites before installation. The install script requires this exact executable.

`LiteralCrab.log` is written beside the ASI. It records profile acceptance, installed engine checks, D3D9 creation/reset results, the actual back-buffer size, the presentation interval and measured rendering FPS. `READY` proves initialization, and subsequent `CreateDevice`/`Present` messages prove the rendering path ran. Failed Reset/Present results remain visible to the game's recovery code.

See [profile.md](profile.md) for the measured addresses and Wii U symbol evidence, and [VALIDATION.md](VALIDATION.md) for verification and its limits.

## Build

Visual Studio 2022 with C++ build tools, a Windows SDK, and CMake 3.21+ are required. Runtime dependencies are statically linked.

```powershell
cmake -S . -B build-di1 -G 'Visual Studio 17 2022' -A Win32
cmake --build build-di1 --config Release --parallel
ctest --test-dir build-di1 -C Release --output-on-failure
.\build-di1\Release\di1_d3d9_smoke.exe borderless
.\build-di1\Release\di1_d3d9_smoke.exe windowed
.\build-di1\Release\di1_d3d9_smoke.exe fullscreen
.\build-di1\Release\di1_d3d9_smoke.exe borderless auto 120 ex
.\build-di1\Release\di1_d3d9_smoke.exe windowed off 240 ex
.\build-di1\Release\di1_d3d9_smoke.exe fullscreen off 240 ex nativewindow
```

Output: `build-di1/Release/LiteralCrab.asi`. The DI1 build is the fork's default. `-DCRABELOADER_BUILD_DI1_ASI=OFF` selects the original upstream DI3 build; do not install its Bink proxy into DI1.

Licensed under GPL-3.0, with CrabeLoader attribution retained. See `LICENSE` and `NOTICE.txt`. The source checkout/archive accompanies the binary; no game binaries, extracted Wii U symbol tables, game assets or saves are included.
