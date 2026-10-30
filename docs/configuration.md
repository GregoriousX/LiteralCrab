# LiteralCrab configuration

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
LanguageSwitcher=off

[PostProcessing]
MotionBlur=1
DepthOfField=1
Bloom=1

[Camera]
ReturnDelay=default

[Controller]
SwapXY=0

[SplitScreen]
Enabled=0

[ToyBox]
LimitScale=1

[Gameplay]
CharactersInAnyPlayset=0
ToolsInAnyPlayset=0

[ToyStore]
Infinity=0
InfinityAll=0

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
| `[Startup]` `LanguageSwitcher` | `off` | Selects a startup language, e.g. `polish`, `italian` or `EnglishPAL`, and preserves it through Steam language detection. Recognizes all 18 PS3 locale IDs and regional aliases. `off` keeps native behavior. Additional languages require their installed PC-compatible assets and locale configuration; missing/unsupported choices retain the native language. Replaces the language pack's separate `PS3Languages.asi`. See language list and setup. |
| `[PostProcessing]` `MotionBlur` | `1` | `0` removes the motion blur seen when the camera turns quickly, in every world. `1` keeps each world's own setting. |
| `[PostProcessing]` `DepthOfField` | `1` | `0` removes the distance blur that some worlds and Toy Box skies use. |
| `[PostProcessing]` `Bloom` | `1` | `0` removes the glow around bright lights, the sky and shiny surfaces. |
| `[Camera]` `ReturnDelay` | `default` | Seconds the camera waits after you turn it before it swings back behind the character, for every camera: `0` returns at once, `5` waits five seconds (up to `3600`). `default` keeps each camera's own timing. |
| `[Controller]` `SwapXY` | `0` | `1` exchanges the X and Y buttons of Xbox-compatible (XInput) controllers everywhere in the game. On-screen prompts keep the original letters. |
| `[SplitScreen]` `Enabled` | `0` | Experimental. `1` restores the consoles' local two-player drop-in: START on a second Xbox-compatible controller asks the game to add player 2, and the game's own drop-in rules, popups and portal figure apply. The keyboard never joins. |
| `[ToyBox]` `LimitScale` | `1` | Multiplies the Toy Box editor's limits (its "memory" meter): object costs, measured memory and frame time. `4` is generous, `10` very high, up to `100`. Very large Toy Boxes can still slow down or run out of memory. Needs `[Mods] Enabled=1`. |
| `[Gameplay]` `CharactersInAnyPlayset` | `0` | `1` lets every character play in every play set. Visitors use their base actor, all avatar dependency maps are loaded, and tool/motion effects are enabled. Player tool selection and unlocked, unsuppressed personal equipment (such as Buzz's innate Pixar Ball) can cross playset zone restrictions. Shared Toy Box/shop equipment retains its native world filters unless `ToolsInAnyPlayset=1`. Native character rosters, character swaps, unlocks and Cars race rules are preserved; six biped progression filters accept visitors. Changes are in memory only. Needs `[Mods] Enabled=1`. See the repair notes and testing status. |
| `[Gameplay]` `ToolsInAnyPlayset` | `0` | `1` lets existing tools and packs cross playset world filters after the game allows their use through native unlock/upgrade checks. Requires `CharactersInAnyPlayset=1` and `[Mods] Enabled=1`; otherwise inactive. Does not grant equipment, purchases or progression. Character conditions, suppression and action restrictions remain native. |
| `[ToyStore]` `Infinity` | `0` | `1` adds **Toy Store Infinity** to the play set pause menu and to the Infinity kiosk, next to the Toy Store: the unlocked vehicles and mounts of every world, placed beside you for free. See [Toy Store Infinity](playsets.md#toy-store-infinity). Needs `[Mods] Enabled=1`. |
| `[ToyStore]` `InfinityAll` | `0` | `1` also lists the locked vehicles and mounts in Toy Store Infinity. Requires `Infinity=1`. |
| `[ToyBoxes]` `Enabled` | `0` | `1` restores **Toy Box Share** (Toy Box menu > Toy Box Share), the PC release's menu that Gold hides, with the Toy Boxes from the `toyboxes` folder beside the ASI. See [Toy Box Share](toybox-share.md#toy-box-share). The menu entry needs `[Mods] Enabled=1`. |
| `[ToyBoxes]` `Servers` | empty | **Experimental, not yet tested at a large scale; use at your own risk.** Web servers to mirror Toy Box packages from, separated by spaces: `http://` or `https://` folder URLs; their Toy Boxes are listed under "Disney's Toy Boxes". See [Toy Box Share](toybox-share.md#toy-box-share). |
| `[ToyBoxes]` `MaxToyBoxes` | `5000` | Most Toy Boxes listed, from `1` to `20000`; packages beyond the limit are not read. |
| `[ToyBoxes]` `ServerMaxPackageMB` | `32` | Largest package a server may send, in MB (`1` to `1024`). A hundred Toy Boxes take about 5 MB. Larger packages are refused before they are downloaded, or cut off as soon as they exceed it. |
| `[ToyBoxes]` `ServerMirrorMB` | `1024` | Most disk space for the packages of all servers together, in MB (`16` to `65536`). |
| `[Mods]` `Enabled` | `1` | Loads script and asset mods from the `mods` folder beside the ASI. With no mods installed nothing is hooked. `0` leaves every game script and file untouched. See the [modding guide](modding.md). |
| `[Mods]` `Trace` | `0` | For mod authors. `1` logs the name of every Lua chunk the game compiles and every file its loader requests, which are the names a mod targets. Slows loading; leave `0` for play. |

## Optional diagnostics

Release packages and source configuration default diagnostics to off. Missing diagnostic keys in older INI files also default to off. `[Diagnostics] CrashReports=1` enables the in-process crash reporter, `ExceptionLog=1` enables the first-chance exception observer, and `TraceAudioBanks=1` enables dialogue-file and native bank/phrase tracing. QA packages may explicitly enable these options. With all diagnostic options off, startup installs no diagnostic exception observer or crash-report worker; functional mod file/audio access remains available.

PL: W paczkach release i konfiguracji źródłowej diagnostyka jest domyślnie wyłączona; brak kluczy w starszym INI również oznacza OFF. `[Diagnostics] CrashReports=1` włącza raportowanie crashy wewnątrz gry, `ExceptionLog=1` obserwację wyjątków, a `TraceAudioBanks=1` śledzenie odczytów dialogów oraz banków/wypowiedzi. Paczki QA mogą jawnie włączać te opcje. Przy wyłączonych opcjach diagnostycznych nie uruchamia się obserwator wyjątków ani wątek raportowania; dostęp modów do plików i audio nadal działa.

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
