# Measured Disney Infinity 1.0 display profile

Upstream basis: CrabeLoader dev `7f251e8eec5de534a2ed9b8efa0694f259706f0f`.

PC executable: `DisneyInfinity1.exe`, x86 PE32, preferred image base `0x400000`, timestamp `0x58535B84`, size of image `0x1511000`. SHA-256: `856e9d3c38f74fcd778109fa46591b092e431989dd8ab269eccf4a7f7d48474f`.

All PC addresses below are **RVAs**, added to the actual loaded image base. Absolute operands in signatures are wildcarded to permit PE relocation. The runtime refuses another PE identity and checks the instructions before creating detours.

| Function/data | DI1 PC RVA | Evidence |
| --- | --- | --- |
| Supported-resolution check | `0x98EC0` | `__cdecl(width,height)`; loop compares both dimensions against the table, returns AL; same code shape as DI3. Sole display enumerator call at `0x24E1A`. |
| Resolution-list Contains | `0x24C60` | `__thiscall(this,width*,height*)`, `ret 8`; loops over dimension pairs. Only calls at `0x2E0DC9` and `0x2E0DFD` are widened (return addresses `0x2E0DCE`, `0x2E0E02`). These are startup resolution validation. Other callers retain the original function. |
| `Flash::Movie::SetViewport` | `0x3C5440` | `__thiscall(this,x,y,width,height)`, `ret 0x10`. X/Y are pixel offsets, W/H fractions. Reads signed 16-bit render-target fields at `+0x68`/`+0x6A`. |
| `Renderer::r_defaultTarget` | `0x9D890` | Resolved from the two calls in SetViewport at offsets `0x13` and `0x1C`; getter reads the context and adds `0xCA8`. |
| D3D9 factory import | named delay import | Main image imports `d3d9.dll!Direct3DCreate9`, not D3D11. The import is intercepted without loading D3D inside DllMain. |
| D3D9Ex factory lookup | named Kernel32 import | DI1 dynamically resolves `Direct3DCreate9Ex`. The host's `GetProcAddress` IAT entry redirects only that factory and `Direct3DCreate9` from the D3D9 module. Other lookups and ordinals retain their native result. |
| Mouse coordinate imports | named User32 imports | `ScreenToClient` call at `0x97453` and `ClientToScreen` at `0x936C1`. Only main-image IAT entries are changed. |
| `Renderer::CameraNode::SetFOV` | `0xA59C0` | `__thiscall(this,float)`, `ret 4`; setter writes CameraNodeObject `+8` through the embedded object at `+0x130`, then marks projection dirty. Only return addresses `0xD7FBB0` and `0xD7FBC9` from `Camera::ApplyProjection` are adjusted. |
| `Renderer::CameraNode::SetAspectRatio` | `0xA5A10` | Same ABI; writes embedded object `+0xC`. Only the `ApplyProjection` call at `0xD7FB39` is adjusted, and only to compensate the physical window/render-target ratio. |
| `Camera::ApplyProjection` | `0xD7FA70` | Reads native camera FOV at `+0x3D4`, applies near/far/aspect, then conditionally converts FOV using target `+0x78` and renderer reference fields `+0xC4C/+0xC50`. The function itself and source camera FOV are not overwritten. |
| Keyboard cooperative flags | `0x98369` | Validated `GUID_SysKeyboard` creation, data-format setup, SetCooperativeLevel and Acquire sequence. The immediate changes `0x16` to `0x06` in managed modes, removing `DISCL_NOWINKEY` while retaining foreground/nonexclusive input. Mouse/gamepad flags are unchanged. |

The static supported-resolution table is not overwritten or extended in place. Both resolution gates are installed together, because enumeration without startup validation would allow a setting that the engine clamps at the next launch. The port accepts 480..32767 per side, with no aspect-ratio whitelist. The upper bound follows the engine's signed 16-bit target dimensions; actual mode availability and limits still come from the graphics driver.

## Wii U cross-reference

The supplied DI1 Wii U **update** `game-m2.consumer.rpx` is ELF32 big-endian PowerPC. Its zlib-compressed sections contain a `.symtab` with **195,351 entries** and a linked string table. This is a symbol table rather than a PC PDB; platform-specific Windows/D3D functions are not inferred from console addresses.

Relevant update symbols:

| Symbol | Wii U virtual address | Size |
| --- | --- | --- |
| `SetViewport__Q2_5Flash5MovieFfN31` | `0x029AC8D4` | 500 bytes |
| `r_defaultTarget__8RendererFv` | `0x020BAFF4` | 20 bytes |
| `SetAspectRatio__Q2_8Renderer10CameraNodeFf` | `0x020B4CD0` | 32 bytes |
| `GetProjectionMatrix__Q2_8Renderer16CameraNodeObjectFv` | `0x020C5460` | 876 bytes |
| `ApplyProjection__6CameraFv` | `0x041D34A4` | 416 bytes |

The Wii U SetViewport implementation calls the default-target getter and reads the same `+0x68`/`+0x6A` dimensions before building a Scaleform viewport. This corroborates the class/function identity and ABI measured independently in x86. DI3's `+0x60`/`+0x62` offsets are deliberately not reused.

The projection symbols establish FOV in degrees and the embedded CameraNodeObject layout. The renderer separately scales projection X by viewport height/width; assigning the full display ratio to CameraNode would therefore apply it twice. The port adjusts the native node aspect only by `physical aspect / render aspect` when those shapes differ.

The **PC runtime** confirmed that target `+0x78` stores **width/height**, while context `+0xC4C` stores the **reciprocal reference aspect**. At 3840x2160 these are `1.77778` and `0.56250`. The original FOV conversion is applied on the narrower-target branch. The correction first removes that conversion, then derives the native 16:9 view and fits other ratios. The final game log confirms `native 65.000 -> 65.000 degrees` at 16:9.

The analysis helper `tools/inspect_di1.py` reads PE/RPX files without changing them. Optional development packages `pefile` and `capstone` provide PE inspection/disassembly; they are not ASI dependencies. Extracted symbol tables and proprietary binaries are excluded from Git and distribution archives.

## Rendering adaptation

DI3's DXGI Present/ResizeBuffers/SetFullscreenState hooks cannot run in DI1. D3D9 `CreateDevice`, `Reset`, `Present`, `TestCooperativeLevel`, and final device release are intercepted here. D3D9Ex also hooks `CreateDeviceEx` (factory slot 20), `ResetEx` (device slot 132), and `PresentEx` (slot 121). A per-thread guard prevents nested base/Ex implementations from pacing a frame twice. The engine's 1x1 capability probes remain native and never become the managed renderer.

The runtime has separate normal/lost-device method tables, which are captured after a reset, failed presentation or cooperative-level check so recovery stays hooked. The mod retains no D3D surfaces, textures, state blocks or swap-chain references that could prevent Reset. Exclusive D3D9Ex receives a matching fullscreen display mode even when the game's original request was windowed. Graphics overlay forwarding is accepted only for a game-owned window in the current process.

Borderless/windowed creation uses a physically windowed D3D device and managed Win32 placement. The plugin never issues an independent Reset while the game owns resources; exclusive fullscreen is selected at startup. Focus and pause messages are forwarded unchanged, while automatic ShowWindow minimization and recursively requested minimize/hide during focus callbacks are suppressed in managed modes. Native manual minimize still works. Frame pacing uses upstream's QPC/waitable-timer strategy, and D3D9 presentation intervals implement the VSync choice.

Keyboard cooperation follows Microsoft's documented [foreground/nonexclusive behavior](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417921(v=vs.85)); only the flag that disables Windows-key shortcuts is removed.
