# LiteralCrab mods

LiteralCrab can load mods for Disney Infinity 1.0 Gold Edition. The loader is deliberately small. A mod can replace game files (see [Asset mods](#asset-mods)), replace a game Lua chunk, or patch a chunk after the game has compiled it. LiteralCrab adds only logging, option values and the game's own basic Lua library functions. It exposes no memory access or native hooks, and it does not sandbox scripts: they run with whatever the game's Lua environment provides, so install mods only from authors you trust. LiteralCrab itself ships no mods. Mods are separate downloads, so the display plugin stays stable whether or not mods are installed.

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
    ├── patches/...
    ├── assets/...     <- loose replacement files (asset mods)
    └── *.zip          <- or the same assets/ tree packed in ordinary ZIPs
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

Only `[Mod]` is needed for a mod that consists of assets alone. If any script in a mod is missing, outside the mod folder, not a `.lua` file, larger than 4 MiB or precompiled bytecode, the whole mod is skipped and the reason is logged. Other mods still load.

Players change options and `Enabled` in `user.ini`, using the same sections. A mod update then does not overwrite their choices.

## Asset mods

An asset mod mirrors the game's `assets` folder. Put the files loose in `mods/<mod>/assets/`, or pack that `assets` folder into one or more ordinary `.zip` files placed directly in `mods/<mod>/`. Any common ZIP tool works (Deflate or Store compression; no encryption or ZIP64). There is no need to rebuild the game's own archive format.

Each file is placed by its path below `assets`:

- **A file the game keeps on disk** (videos, sound banks, an archive as a whole): the mod's file replaces it. A whole replacement archive may be an ordinary ZIP; it is converted.
- **A file inside one of the game's archives**: for example, `assets/ui/controllerbuttons.mtb` and `assets/ui/textures/0389b6b276ccc3af.tbody` are members of `assets/ui/ui.zip`. LiteralCrab looks for the member in the archives of the file's folder, then of each parent folder. The first archive containing it gets the mod's version. A file that no archive contains is added to the folder's archive when that folder has exactly one archive; otherwise it is skipped and logged.

Ship only the files you changed. Unchanged members come from the player's own game files, so a mod never needs to redistribute game content. Mods are combined per file: if two mods change the same file, the first in load order wins and the conflict is logged.

At startup, every archive a mod touches is rebuilt once in the game's layout under `mods/.cache/`. It is rebuilt again only when the game archive or a contributing mod file changes; the log reports how long preparation took. The cache is safe to delete. While the game runs, opening an affected file is redirected to the mod or cache copy. Writes always reach the original path, and all other files are untouched.

The log lists each mod's number of asset files and every file that was skipped, with the reason. With `Trace=1`, every redirected open is logged too.

## Chunk names

The game compiles each Lua script under a name such as `@Presentation/Adventures.lua`. Matching ignores case, a leading `@`, and the `\`/`/` difference. A configured name matches when it is the full name or a suffix starting at a `/`: `Adventures.lua` and `Presentation/Adventures.lua` both match `@Presentation/Adventures.lua`. Prefer the longest name, because a bare file name can match several chunks.

To find names, set `Trace=1` in the `[Mods]` section of LiteralCrab.ini. Then play through the relevant screens and read LiteralCrab.log. Turn tracing off again for normal play.

### Chunks without a name

The game compiles many data tables, and some precompiled scripts, without any name. The trace lists them as `<unnamed>`, followed by their first line, or as `<unnamed bytecode>`. Target these chunks by a piece of text they contain:

```ini
[Patch]
contains:ToyBoxLimits=patches/toybox_limits.lua
```

The text after `contains:` is matched exactly and is case-sensitive. It must be at least 8 bytes long. It is searched for only in chunks without a name; named chunks are always matched by name. In precompiled chunks, string constants and global names appear as plain text, so a distinctive table or string name works there too. Choose a marker that occurs in only one chunk. Leading and trailing spaces are trimmed by the INI format, and the marker cannot contain `=`.

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

### Globals available to patches

The game loads many data tables into Lua states that have no standard library at all. The game's own scripts there are pure table definitions. So that patches work the same everywhere, a global a patch reads is looked up first in the chunk's environment and then in a fixed set of library functions taken from the game itself:

- `assert`, `error`, `getfenv`, `setfenv`, `getmetatable`, `setmetatable`, `next`, `pairs`, `ipairs`, `pcall`, `xpcall`, `rawequal`, `rawget`, `rawset`, `select`, `tonumber`, `tostring`, `type`, `unpack`
- `table.concat`, `insert`, `maxn`, `remove`, `sort`
- `string.byte`, `char`, `find`, `format`, `gmatch`, `gsub`, `len`, `lower`, `match`, `rep`, `reverse`, `sub`, `upper`
- `math.*` except `random` and `randomseed`, plus `math.pi` and `math.huge`

These fallbacks are not added to the game's state. Globals a patch assigns are written to the chunk's environment, exactly as if the chunk had defined them. File, process and module functions are not provided. String methods (`("x"):upper()`) work only where the game opened the string library; call `string.upper("x")` instead.

## Guarantees and limits

- With no mods installed, or `[Mods] Enabled=0`, the game's Lua loader is not hooked at all.
- The loader checks the signatures of the game's Lua functions before installing. On any other executable it does nothing.
- Per compiled chunk, the cost is a name lookup without memory allocation. Unnamed chunks are searched only when an installed mod uses `contains:`. Scripts are read and validated only at startup.
- A native fault while composing a patch disables the mod loader for the rest of the session and is logged. Display features are unaffected.
- Mods must not contain game assets or extracted game scripts. Distribute your own patch code only.
