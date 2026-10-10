# LiteralCrab configuration guide

Edit **LiteralCrab.ini** beside **LiteralCrab.asi**, then restart the game. Display options belong in the `[Display]` section; the script mod loader uses `[Mods]`. Use `1` to enable a switch and `0` to disable it.

## Complete default configuration

```ini
[Display]
Mode=borderless
UnlockResolutions=1
FixFOV=1
KeepVisibleOnFocusLoss=1
ScreenshotHotkeys=1
FixUIAspect=1
ScaleMouse=1
AltEnter=1
RememberMode=1
FrameLimit=120
VSync=auto
Diagnostics=0

[Mods]
Enabled=1
Trace=0
```

## Every setting

| Setting | Default | Effect |
| --- | --- | --- |
| `Mode` | `borderless` | `borderless` fills the current monitor without a title bar; `windowed` has window borders and a title bar; `fullscreen` requests exclusive fullscreen. |
| `UnlockResolutions` | `1` | Allows driver-supported game resolutions from 480 to 32767 pixels per side, without restricting the aspect ratio. |
| `FixFOV` | `1` | Keeps the native 16:9 camera view. Wider displays gain horizontal view; narrower displays gain vertical view. |
| `KeepVisibleOnFocusLoss` | `1` | Prevents automatic minimize/hide when another app takes focus in borderless/windowed mode. The game's native focus and pause handling is preserved. |
| `ScreenshotHotkeys` | `1` | Allows Windows-key capture shortcuts in borderless/windowed mode by removing the keyboard's Windows-key suppression. |
| `FixUIAspect` | `1` | Fits full-screen menus and HUD into a centred 16:9 area. Placed UI elements and sub-viewports keep their native layout. |
| `ScaleMouse` | `1` | Corrects mouse coordinates when the render resolution and window size differ. |
| `AltEnter` | `1` | Enables Alt+Enter to toggle between borderless and windowed. Exclusive fullscreen keeps the game's own hotkey handling. |
| `RememberMode` | `1` | Saves a mode selected with Alt+Enter into the INI for the next launch. |
| `FrameLimit` | `120` | Rendering cap: `15`–`1000` FPS, or `0` for uncapped output. |
| `VSync` | `auto` | `auto` bypasses VSync when uncapped or when the cap is at least the monitor refresh rate; `on` always synchronizes; `off` always bypasses it. |
| `Diagnostics` | `0` | Troubleshooting only. `1` writes frame-time statistics (average and worst frame, counts above 20/33/50 ms, time blocked in Present) and the game's mouse/focus state to LiteralCrab.log on each focus change and on exit. Nothing is written per frame. |
| `[Mods]` `Enabled` | `1` | Loads Lua script mods from the `mods` folder beside the ASI. With no mods installed nothing is hooked. `0` leaves every game script untouched. See [MODDING.md](MODDING.md). |
| `[Mods]` `Trace` | `0` | For mod authors. `1` logs the name of every Lua chunk the game compiles, which is the name a mod targets. |

## Frame-rate examples

Change these lines in the existing `[Display]` section; keep the other settings.

| Desired limit | `FrameLimit` | `VSync` |
| --- | --- | --- |
| 60 FPS with display synchronization | `60` | `on` |
| 120 FPS | `120` | `auto` |
| 144, 165 or 240 FPS | `144`, `165` or `240` | `off` |
| Uncapped | `0` | `off` |

A cap is a ceiling. Achievable FPS depends on the hardware and scene. VSync and driver-level limits can also restrict the rate. Startup movies use the game's native presentation timing. FrameLimit does not change the simulation clock.

## Resolutions and ultrawide

Choose the resolution in the game's graphics options. The plugin does not define separate width/height INI entries. `UnlockResolutions=1` exposes supported driver modes; keep `FixFOV=1`, `FixUIAspect=1` and `ScaleMouse=1` for corrected view, UI and input.

Borderless fills the monitor. A render buffer with a different aspect ratio is compensated by the projection/input patches, but selecting a resolution matching the monitor gives the clearest image.

The plugin marks the game as DPI aware before its window is created. With Windows display scaling (for example 150% on a 4K monitor) the window therefore uses physical pixels: a 4K resolution is shown at 4K instead of being scaled down and stretched again by Windows, and mouse input maps 1:1. Windowed mode retains its decorated window size and position when toggled back from borderless.

## Alt+Tab and screenshots

Use `Mode=borderless` or `Mode=windowed`, with `KeepVisibleOnFocusLoss=1` and `ScreenshotHotkeys=1`. The first option keeps the window visible when ShareX or another capture tool takes focus; the second allows Windows-key shortcuts. Automatic focus-loss minimization is blocked, while a deliberate minimize action remains available.

Direct ShareX/Alt+Print Screen capture and every ultrawide setup still require in-game confirmation. Exclusive fullscreen is outside the plugin's managed-window focus policy.

## Installation and troubleshooting

- Copy LiteralCrab.asi and LiteralCrab.ini beside DisneyInfinity1.exe and use a compatible 32-bit ASI loader, such as Ultimate ASI Loader as dinput8.dll. Keep the game's original binkw32.dll.
- Disable another display/resolution ASI before testing LiteralCrab; two plugins patching the same functions can conflict. Preserve the disabled file as a backup.
- Change the INI while the game is closed. Alt+Enter is the supported live borderless/windowed toggle. Restart to select exclusive fullscreen.
- Check LiteralCrab.log beside the ASI for the detected game profile, effective display mode, cap and hook failures. The log is replaced on each launch. For FPS or mouse problems, set `Diagnostics=1`, reproduce the issue, press Alt+Tab once and attach the log.
- Unsupported executables are rejected by the version/signature checks. Missing settings use the defaults above. Unrecognized Mode/VSync values fall back to borderless/auto.

Installation requires no changes to characters, gameplay scripts or game assets. Script mods are optional, separate downloads; LiteralCrab itself ships none.
