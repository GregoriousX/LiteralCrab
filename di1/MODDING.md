# LiteralCrab script mods

LiteralCrab can load Lua script mods for Disney Infinity 1.0 Gold Edition. The loader is deliberately small. A mod can replace a game Lua chunk, or patch a chunk after the game has compiled it. LiteralCrab adds only logging and option values. It exposes no memory access or native hooks, and it does not sandbox scripts: they run with whatever the game's Lua environment provides, so install mods only from authors you trust. LiteralCrab itself ships no mods. Mods are separate downloads, so the display plugin stays stable whether or not mods are installed.

Mods are read once at startup. Restart the game after installing, removing or configuring a mod.

## Layout

```text
DisneyInfinity1.exe
LiteralCrab.asi
LiteralCrab.ini
mods/
└── my_mod/
    ├── mod.ini        <- manifest, shipped by the mod
    ├── user.ini       <- optional player settings, never shipped by the mod
    ├── replace/...
    └── patches/...
```

The `mods` folder sits beside LiteralCrab.asi. Each subfolder containing a `mod.ini` is one mod. Mods load in ordinal, case-insensitive folder-name order.

## mod.ini

```ini
[Mod]
Name=My Mod
Enabled=1

[Options]
; Defaults passed to patches. Integers become Lua numbers, anything else a string.
ShowExtras=1
Label=hello

[Override]
; <game chunk>=<script in this mod>
Presentation/Adventures.lua=replace/adventures.lua

[Patch]
; A chunk may be listed more than once; patches run in file order.
Presentation/PauseMenu.lua=patches/pause_menu.lua
```

A mod needs at least one `[Override]` or `[Patch]` entry. If any script in a mod is missing, outside the mod folder, not a `.lua` file, larger than 4 MiB or precompiled bytecode, the whole mod is skipped and the reason is logged. Other mods still load.

Players change options and `Enabled` in `user.ini`, using the same sections. A mod update then does not overwrite their choices.

## Chunk names

The game compiles each Lua script under a name such as `@Presentation/Adventures.lua`. Matching ignores case, a leading `@`, and the `\`/`/` difference. A configured name matches when it is the full name or a suffix starting at a `/`: `Adventures.lua` and `Presentation/Adventures.lua` both match `@Presentation/Adventures.lua`. Prefer the longest name, because a bare file name can match several chunks.

To find names, set `Trace=1` in the `[Mods]` section of LiteralCrab.ini. Then play through the relevant screens and read LiteralCrab.log. Turn tracing off again for normal play.

## Overrides

An override replaces the chunk's source before the game compiles it. If several mods replace the same chunk, the first mod in load order wins and the others are logged as ignored. If an override fails to compile, the game's own chunk is used and the error is logged.

Prefer patches when possible. They compose with other mods and survive small differences between game versions.

## Patches

A patch runs immediately after the chunk it targets, every time that chunk runs. It shares the chunk's environment, so the globals and tables the chunk just defined are visible. It receives three arguments:

```lua
local result, options, log = ...
-- result:  the first value the chunk returned (often nil)
-- options: this mod's [Options], after user.ini
-- log:     log(message) writes "Mod <mod>/<file>: message" to LiteralCrab.log

if options.ShowExtras == 1 and type(PauseMenu) == "table" then
    -- adjust the tables the game script just built
end
```

The chunk's own results and errors are passed through unchanged. A patch error is caught and logged, and the game continues with the unpatched data. A patch that fails to compile is not applied. Patches from all matching names apply; within one name they run in load order.

## Guarantees and limits

- With no mods installed, or `[Mods] Enabled=0`, the game's Lua loader is not hooked at all.
- The loader checks the signatures of the game's Lua functions before installing. On any other executable it does nothing.
- Per compiled chunk, the cost is a name lookup without memory allocation. Scripts are read and validated only at startup.
- A native fault while composing a patch disables the mod loader for the rest of the session and is logged. Display features are unaffected.
- Mods must not contain game assets or extracted game scripts. Distribute your own patch code only.
