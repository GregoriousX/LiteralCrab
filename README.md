# LiteralCrab

LiteralCrab is a 32-bit ASI plugin for **Disney Infinity 1.0 Gold Edition (Windows PC)**. It adds display and frame-rate options based on [CrabeLoader](https://github.com/LucasLhomme/CrabeLoader).

This is a display-focused port, not a port of CrabeLoader's Disney Infinity 3.0 modding features.

## Features

- Borderless, windowed and exclusive fullscreen modes.
- Additional resolutions and aspect ratios, including ultrawide displays.
- Field-of-view correction and centered 16:9 menus and HUD.
- Mouse scaling when the window and rendering resolutions differ.
- Configurable FPS limit (15–1000 FPS or uncapped) and VSync.
- Better Alt+Tab behavior and support for Windows screenshot shortcuts in borderless/windowed modes.

The plugin does not change gameplay, characters or game simulation speed.

## Installation

1. Download **LiteralCrab-DI1-0.1.0-x86.zip** from [Releases](https://github.com/GregoriousX/LiteralCrab/releases).
2. Copy `LiteralCrab.asi` and `LiteralCrab.ini` into the game folder, next to `DisneyInfinity1.exe`.
3. Install a compatible **32-bit ASI loader**. The tested option is [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), installed as `dinput8.dll`.
4. Start the game.

Keep the original `binkw32.dll` unchanged. If you have another display or resolution mod installed, read the [installation notes](di1/README.md) to avoid conflicts.

## Configuration

Edit `LiteralCrab.ini` to change the settings:

```ini
[Display]
Mode=borderless
FrameLimit=120
VSync=auto
```

- `Mode`: `borderless`, `windowed` or `fullscreen`.
- `FrameLimit`: `15`–`1000`, or `0` for uncapped FPS.
- `VSync`: `auto`, `on` or `off`.

Restart the game after changing settings. See the [full configuration guide](di1/README.md) for other options.

## Compatibility

LiteralCrab currently supports the verified Disney Infinity 1.0 Gold Edition PC executable. Unsupported game builds are rejected. Not all gameplay scenarios or display setups have been tested.

See [validation results](di1/VALIDATION.md) for tested features and known limitations. The plugin writes `LiteralCrab.log` next to the ASI file for troubleshooting.

## Build from source

Requires Visual Studio 2022 with C++ tools, the Windows SDK and CMake 3.21+.

```powershell
cmake -S . -B build-di1 -G 'Visual Studio 17 2022' -A Win32
cmake --build build-di1 --config Release --parallel
```

Output: `build-di1/Release/LiteralCrab.asi`.

## Credits

**[Lucas Lhomme (@LucasLhomme)](https://github.com/LucasLhomme)** — creator of [CrabeLoader](https://github.com/LucasLhomme/CrabeLoader), the original project this repository is forked from.

LiteralCrab is based on CrabeLoader's `dev` branch (commit [`7f251e8`](https://github.com/LucasLhomme/CrabeLoader/commit/7f251e8eec5de534a2ed9b8efa0694f259706f0f)) and adapts its display features for Disney Infinity 1.0. The original README is available [here](di1/UPSTREAM-README.md).

## License

Licensed under [GPL-3.0](LICENSE). See [third-party notices](di1/NOTICE.txt) for additional credits and licenses.
