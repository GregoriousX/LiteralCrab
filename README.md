# LiteralCrab — Disney Infinity 1.0 display ASI

A Windows x86 **ASI plugin for Disney Infinity 1.0 Gold Edition**, forked from [LucasLhomme/CrabeLoader dev](https://github.com/LucasLhomme/CrabeLoader/tree/dev) at `7f251e8eec5de534a2ed9b8efa0694f259706f0f` (8 October 2026).

The DI1 target ports windowed, borderless and exclusive fullscreen modes, resolution checks, UI aspect centring, mouse coordinate scaling, and frame pacing to **D3D9 and D3D9Ex**. `FrameLimit` defaults to **120**, accepts **15–1000 FPS**, and accepts **0** for uncapped output. `VSync=off` permits presentation above monitor refresh.

Aspect support has no aspect-ratio whitelist: ultrawide and multi-monitor displays gain horizontal view, and narrower/portrait displays gain vertical view relative to the native 16:9 camera. Projection compensation also handles a render buffer stretched into a differently shaped window. Full-screen UI stays in a centred 16:9 area. Borderless/windowed use a real windowed device, stay visible on focus loss, and allow Windows screenshot shortcuts. Native camera zoom, character handling, gameplay scripts, assets, pause behavior and simulation clocks are preserved.

The output is **LiteralCrab.asi**. Put it and `LiteralCrab.ini` beside `DisneyInfinity1.exe` with a compatible 32-bit ASI loader. The tested loader is Ultimate ASI Loader as `dinput8.dll`. Keep the original Bink DLL in place.

Download the ASI package from [LiteralCrab releases](https://github.com/GregoriousX/LiteralCrab/releases), then read **[installation and configuration](di1/README.md)**. The installer preserves an existing INI, backs up a prior ASI, and disables the recognized older 4K plugin without deleting it. See **[validation results and limits](di1/VALIDATION.md)** and **[the measured DI1 profile](di1/profile.md)** for compatibility details.

```powershell
cmake -S . -B build-di1 -G 'Visual Studio 17 2022' -A Win32
cmake --build build-di1 --config Release --parallel
ctest --test-dir build-di1 -C Release --output-on-failure
```

Output: `build-di1/Release/LiteralCrab.asi`. This fork builds DI1 by default; `-DCRABELOADER_BUILD_DI1_ASI=OFF` selects the retained upstream DI3 target. Its original documentation is preserved in [di1/UPSTREAM-README.md](di1/UPSTREAM-README.md).

GPL-3.0; see [LICENSE](LICENSE) and [attribution and third-party notices](di1/NOTICE.txt). No game binaries, extracted proprietary symbol tables, assets or saves are distributed.
