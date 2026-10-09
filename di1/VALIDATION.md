# DI1 ASI validation

Validated locally on Windows on 9 October 2026, against the measured DI1 Gold PC executable documented in [profile.md](profile.md). The supplied Wii U update symbols corroborated the engine UI and projection functions; Windows addresses and runtime field values were measured independently.

## Build and unit checks

- Visual Studio 2022/MSVC 19.44, Release, Win32, C++20, static CRT: successful.
- CTest: 2/2 suites passed. **432 policy checks** cover resolution limits, UI centring and preserved sub-rectangles, mouse scaling, overflow/minimized dimensions and frame-limit bounds. FOV/projection checks cover 4 native zoom values and every pairing of 7 render/display ratios: portrait, 1:1, 4:3, 16:9, 21:9, 32:9 and 48:9.
- Real QPC pacing checks passed at 60, 120 and 240 FPS, plus uncapped output.
- PE32 ASI with one export, `InitializeASI`; no Bink forwarding/exports, D3D11 or DXGI dependency.

## Real graphics-driver integration

The smoke executable links the production display, import, and frame-pacing code. It creates actual D3D devices and checks their real swap-chain presentation parameters. These are hardware tests, independent of gameplay/save data.

| API | Mode | VSync/cap | Passed checks |
| --- | --- | --- | --- |
| D3D9 | Borderless | auto/120 | 109 |
| D3D9 | Windowed | off/240 | 108 |
| D3D9 | Exclusive fullscreen | auto/120 | 37 |
| D3D9Ex | Borderless | auto/120 | 110 |
| D3D9Ex | Windowed | off/240 | 109 |
| D3D9Ex | Exclusive fullscreen | on/120 | 38 |
| D3D9Ex | Exclusive from a windowed game request | off/240 | 38 |
| D3D9Ex | Borderless | auto/30 | 110 |
| D3D9Ex | Borderless | auto/uncapped | 110 |

Checks include dynamic Ex factory interception, native 1x1 capability probes, borderless monitor fit, title bars, non-topmost windows, forwarded focus/pause events, mouse coordinate round trips, Alt+Enter/repeat handling, user minimize/restore, Reset failure and recovery, actual resized back buffers, zero-size Reset, no double pacing, and device/window-hook release. An incoming synchronized presentation interval is explicitly tested against the VSync policy.

The managed-mode tests also confirm that recursive minimize/hide from a focus callback is blocked, another test window receives native activation, and GDI capture returns the exact rendered RGB pixels before and after focus changes. Windows foreground-stealing restrictions mean the test asserts native thread activation rather than requiring the test process to become the global foreground app.

## Actual game startup and rendering

Ultimate ASI Loader as `dinput8.dll` loads the plugin. The plugin verifies the DI1 profile, installs both resolution gates, the UI/projection hooks and the keyboard shortcut patch, captures the dynamically selected D3D9Ex factory, and converts the game's exclusive request into a borderless device. The final game log reports a **3840x2160 back buffer** and a **3840x2160 borderless window**. Native 16:9 camera FOV remains **65.000 -> 65.000 degrees**. The play-set selection menu was visually inspected through Windows capture.

With the final projection/shortcut build and `FrameLimit=120`, the rendering log repeatedly measured approximately **120 FPS** after the startup movies. The display/pacing build with `FrameLimit=240` and `VSync=off` measured approximately **231–237 FPS** in subsequent startup/menu rendering on this machine; that game run preceded the final projection/shortcut additions. Startup movie presentation remains about 30 FPS while the native movie code waits for frames. A configured cap is a ceiling; achievable speed depends on the scene and hardware.

The final 240 FPS check used the ASI loader's default settings. A test-only loader INI was unnecessary and removed from active use. The workspace copy used Steam's documented `steam_appid.txt` development method during testing to avoid redirecting launch to a different Steam installation; the test file is excluded from the mod/package. See [Steamworks initialization documentation](https://partner.steamgames.com/doc/sdk/api#initialization_and_shutdown).

No characters were selected and no gameplay/save progression was exercised by these tests. Longer play sessions, every play set, split-screen gameplay, visual inspection on an ultrawide monitor, and direct Alt+Print Screen/ShareX use in gameplay remain unverified. Computer Use was stopped by the user before further game interaction. The ASI contains no character/gameplay patches, game clock changes, asset overrides, Lua runtime or Bink proxy. Scope and binary checks do not substitute for a complete gameplay compatibility test.
