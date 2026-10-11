# LiteralCrab configuration guide

Edit **LiteralCrab.ini** beside **LiteralCrab.asi** while the game is closed; changes apply on the next start. Display options belong in `[Display]`, the optional settings in their own sections and the mod loader in `[Mods]`. Use `1` to enable a switch and `0` to disable it. Every option outside `[Display]` defaults to the game's own behavior and installs nothing until it is changed.

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

[Startup]
SkipIntro=0

[PostProcessing]
MotionBlur=1
DepthOfField=1
Bloom=1

[Camera]
ReturnDelay=default

[Controller]
SwapXY=0

[ToyBox]
LimitScale=1

[Gameplay]
CharactersInAnyPlayset=0

[ToyBoxes]
Enabled=0
Servers=
MaxToyBoxes=5000
ServerMaxPackageMB=32
ServerMirrorMB=1024

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
| `[Startup]` `SkipIntro` | `0` | `1` starts without the logo movies. The game is sent down the path it already takes for logo movies that are not installed; the movie files stay untouched and the menu's attract movie and in-game cinematics still play. |
| `[PostProcessing]` `MotionBlur` | `1` | `0` removes the motion blur seen when the camera turns quickly, in every world. `1` keeps each world's own setting. |
| `[PostProcessing]` `DepthOfField` | `1` | `0` removes the distance blur that some worlds and Toy Box skies use. |
| `[PostProcessing]` `Bloom` | `1` | `0` removes the glow around bright lights, the sky and shiny surfaces. |
| `[Camera]` `ReturnDelay` | `default` | Seconds the camera waits after you turn it before it swings back behind the character, for every camera: `0` returns at once, `5` waits five seconds (up to `3600`). `default` keeps each camera's own timing. |
| `[Controller]` `SwapXY` | `0` | `1` exchanges the X and Y buttons of Xbox-compatible (XInput) controllers everywhere in the game. On-screen prompts keep the original letters. |
| `[ToyBox]` `LimitScale` | `1` | Multiplies the Toy Box editor's limits (its "memory" meter): object costs, measured memory and frame time. `4` is generous, `10` very high, up to `100`. Very large Toy Boxes can still slow down or run out of memory. Needs `[Mods] Enabled=1`. |
| `[Gameplay]` `CharactersInAnyPlayset` | `0` | `1` lets every character play in every play set. Only the play set checks are relaxed, in memory: other rules (for example which characters may enter Cars races), unlocks and game files stay unchanged. Needs `[Mods] Enabled=1`. |
| `[ToyBoxes]` `Enabled` | `0` | `1` restores **Toy Box Share** (Toy Box menu > Toy Box Share), the PC release's menu that Gold hides, with the Toy Boxes from the `toyboxes` folder beside the ASI. See [Toy Box Share](#toy-box-share). The menu entry needs `[Mods] Enabled=1`. |
| `[ToyBoxes]` `Servers` | empty | **Experimental, not yet tested at a large scale; use at your own risk.** Web servers to mirror Toy Box packages from, separated by spaces: `http://` or `https://` folder URLs; their Toy Boxes are listed under "Disney's Toy Boxes". See [Toy Box Share](#toy-box-share). |
| `[ToyBoxes]` `MaxToyBoxes` | `5000` | Most Toy Boxes listed, from `1` to `20000`; packages beyond the limit are not read. |
| `[ToyBoxes]` `ServerMaxPackageMB` | `32` | Largest package a server may send, in MB (`1` to `1024`). A hundred Toy Boxes take about 5 MB. Larger packages are refused before they are downloaded, or cut off as soon as they exceed it. |
| `[ToyBoxes]` `ServerMirrorMB` | `1024` | Most disk space for the packages of all servers together, in MB (`16` to `65536`). |
| `[Mods]` `Enabled` | `1` | Loads script and asset mods from the `mods` folder beside the ASI. With no mods installed nothing is hooked. `0` leaves every game script and file untouched. See [MODDING.md](MODDING.md). |
| `[Mods]` `Trace` | `0` | For mod authors. `1` logs the name of every Lua chunk the game compiles and every file its loader requests, which are the names a mod targets. Slows loading; leave `0` for play. |

## Toy Box Share

With `[ToyBoxes] Enabled=1` the Toy Box menu gets its **Toy Box Share** entry back. "My Toy Boxes" lists every Toy Box from the `toyboxes` folder and "Disney's Toy Boxes" those from the configured servers, each with its picture and description; choose one and *Play Now*. Without a Disney account the game first says that only local content is available; that is expected.

- **Packages:** ordinary ZIP files with any names, in any subfolders of `toyboxes`; ZIPs are recommended because they keep different Toy Boxes' files apart. Loose save files work too: the loose files of each folder count as one package, and a folder holding a single Toy Box needs no particular names. A package may hold one Toy Box or hundreds, as Toy Box Share files (`U`, `UH`, `UC`) or Toy Box saves (`SRR`, `SHRR`, `SCRR`), from the PC, PS3 or Wii U; the names inside do not matter either, files are recognised by their content. The same Toy Box in several packages is listed once. Saves of hubs with progress of their own (Toy Box Launch, Hall of Heroes, the introduction) are left out; Toy Boxes built in the empty, themed or prebuilt Toy Box worlds are listed, autosaves included.
- **Your saves stay untouched.** The Toy Boxes are shown to the game as extra, read-only Toy Box Share slots after its own 101; nothing is copied into the save folder, so they never use up slots or reach Steam Cloud. Deleting one in the menu only hides it until the next start; remove its package to delete it.
- **Untrusted packages:** servers are only read below their URL (no other hosts or folders), with limits on listing size, pages, packages per server (2000), servers (8), redirects and time (10 minutes per start); `ServerMaxPackageMB` and `ServerMirrorMB` bound the downloads and `MaxToyBoxes` the list. Inside packages, file and archive sizes are limited, and each Toy Box is checked before the game parses it: the object count must be sane and its object list at least that long, the world must be installed, its header is written anew from cleaned, length-limited fields, and a screenshot whose size does not match its pixel data is left out.
- **Damaged files** are skipped: every package and Toy Box is checked (ZIP checksums, the save checksums, the layout and the world it needs) before the game sees it, and `LiteralCrab.log` names what was skipped and why.
- **Speed:** packages are converted once into `toyboxes\.cache` on a background thread; later starts reuse the cache and only read changed packages. While playing, nothing runs.
- **Servers (experimental):** reading Toy Boxes from servers has not been tested at a large scale yet, so enable it at your own risk. `Servers=https://example.org/toyboxes/` or `Servers=localhost:8000` (several separated by spaces; `http://` when no scheme is given). A server lists its packages in an `index.txt` beside that URL, one path relative to it per line (any subfolders and names, `#` starts a comment), or simply shows a directory listing, which is followed into subfolders. Packages are mirrored into `toyboxes\.cache\servers` in the background: new and changed ones are downloaded, ones the server no longer lists are removed (an unreachable server keeps its copies). At startup the game waits up to 5 seconds for the servers; Toy Boxes that arrive later appear after the next save or start.

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
- Check LiteralCrab.log beside the ASI for the detected game profile, effective display mode, cap, the optional settings in effect and any feature left out because the game's code differs. The log is replaced on each launch. For FPS or mouse problems, set `Diagnostics=1`, reproduce the issue, press Alt+Tab once and attach the log.
- Unsupported executables are rejected by the version/signature checks. Missing settings use the defaults above. Unrecognized Mode/VSync values fall back to borderless/auto.

Installation requires no changes to characters, gameplay scripts or game assets, and LiteralCrab never writes to the game's files. Script mods are optional, separate downloads; LiteralCrab itself ships none.
