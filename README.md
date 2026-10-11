# LiteralCrab

LiteralCrab is a 32-bit ASI plugin for **Disney Infinity 1.0 Gold Edition (Windows PC)**. It fixes the PC version's display and frame-rate limits, adds optional quality-of-life and gameplay options, and loads script and asset mods. It is based on [CrabeLoader](https://github.com/LucasLhomme/CrabeLoader).

Every option outside the display fixes is **off by default**. With the default settings LiteralCrab does not change gameplay, characters or simulation speed.

## Features

**Display and performance**
- Borderless, windowed and exclusive fullscreen modes, with Alt+Enter switching.
- Additional resolutions and aspect ratios, including ultrawide displays.
- Field-of-view correction and centered 16:9 menus and HUD.
- Mouse scaling when the window and rendering resolutions differ.
- Configurable FPS limit (15–1000 FPS or uncapped) and VSync.
- Better Alt+Tab behavior and support for Windows screenshot shortcuts.

**Optional settings** (all off by default)
- Skip the startup logo movies.
- Switch off motion blur, depth of field and bloom individually.
- Choose how long the camera waits before swinging back behind the character.
- Swap the X and Y buttons of Xbox-compatible controllers.
- Raise the Toy Box editor's object and memory limits.
- Play any character in any play set.

**Mods**
- Script mods: replace or patch the game's Lua scripts without editing game files.
- Asset mods: replace models, textures, sounds and other files with loose files or ordinary ZIPs. The game's archives are never rebuilt.
- Verified engine adapters and features for mods, such as awarding tools, placing power discs or restoring console Toy Box Hub menus.

See the [modding guide](di1/MODDING.md).

## Installation

1. Download the latest **LiteralCrab-DI1-&lt;version&gt;-x86.zip** from [Releases](https://github.com/GregoriousX/LiteralCrab/releases).
2. Copy `LiteralCrab.asi` and `LiteralCrab.ini` into the game folder, next to `DisneyInfinity1.exe`.
3. Install a compatible **32-bit ASI loader**. The tested option is [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), installed as `dinput8.dll`.
4. Start the game.

Keep the original `binkw32.dll` unchanged. If you have another display or resolution mod installed, read the [installation notes](di1/README.md) to avoid conflicts.

## Configuration

Edit `LiteralCrab.ini` while the game is closed; changes apply on the next start. For example:

```ini
[Display]
Mode=borderless
FrameLimit=120
VSync=auto

[Startup]
SkipIntro=1

[PostProcessing]
MotionBlur=0
```

The INI groups its settings by topic and explains each one. See the [full configuration guide](di1/README.md) for every option.

Some options work through the mod loader and need `[Mods] Enabled=1` (the default): `[ToyBox] LimitScale` and `[Gameplay] CharactersInAnyPlayset`.

## Compatibility

LiteralCrab supports the verified Disney Infinity 1.0 Gold Edition PC executable. Every hook and patch checks the game's code first; on another build the plugin refuses to start, and a feature whose code differs is left out and logged in `LiteralCrab.log`. Not all gameplay scenarios or display setups have been tested.

## Build from source

Requires Visual Studio 2022 with C++ tools, the Windows SDK and CMake 3.21+.

```powershell
cmake -S . -B build-di1 -G 'Visual Studio 17 2022' -A Win32
cmake --build build-di1 --config Release --parallel
```

Output: `build-di1/Release/LiteralCrab.asi`.

## Credits

**[Lucas Lhomme (@LucasLhomme)](https://github.com/LucasLhomme)** — creator of [CrabeLoader](https://github.com/LucasLhomme/CrabeLoader), the original project this repository is forked from. LiteralCrab is based on CrabeLoader's `dev` branch (commit [`7f251e8`](https://github.com/LucasLhomme/CrabeLoader/commit/7f251e8eec5de534a2ed9b8efa0694f259706f0f)) and adapts its display features for Disney Infinity 1.0. The original README is available [here](di1/UPSTREAM-README.md).

**[Philip Parkinson II (@philparkinson1204)](https://github.com/philparkinson1204)** — [InfinityUnlocked](https://github.com/philparkinson1204/InfinityUnlocked), which identified the native play set restrictions. `[Gameplay] CharactersInAnyPlayset` builds on that research with the author's permission, applied in memory without modifying the executable or replacing archives.

## License

Licensed under [GPL-3.0](LICENSE). See [third-party notices](di1/NOTICE.txt) for additional credits and licenses.
