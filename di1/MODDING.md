# LiteralCrab mods

LiteralCrab can load mods for Disney Infinity 1.0 Gold Edition. A mod can replace game files (see [Asset mods](#asset-mods)), replace or patch a game Lua chunk, or declare a separate custom figure with its own resources and native equipment. LiteralCrab provides logging, option values, the game's basic Lua library functions and verified engine adapters. It exposes no arbitrary memory access or native hooks to scripts, and it does not sandbox scripts: they run with whatever the game's Lua environment provides, so install mods only from authors you trust. LiteralCrab itself ships no mods. Mods are separate downloads.

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
    ├── assets/...     <- loose global replacement files (asset mods)
    ├── character-assets/... <- optional private character resources
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

[Engine]
; Optional engine features this mod needs (see Engine features). 1 = requested.
TriggerGuiScreen=1
```

Only `[Mod]` is needed for a mod that consists of assets alone. If any script in a mod is missing, outside the mod folder, not a `.lua` file, larger than 4 MiB or precompiled bytecode, the whole mod is skipped and the reason is logged. Other mods still load.

Players change options and `Enabled` in `user.ini`, using the same sections. A mod update then does not overwrite their choices.

## Asset mods

An asset mod mirrors the game's `assets` folder. Put the files loose in `mods/<mod>/assets/`, or pack that `assets` folder into one or more ordinary `.zip` files placed directly in `mods/<mod>/`. Any common ZIP tool works (Deflate or Store compression; no encryption or ZIP64). There is no need to use the game's own archive format.

Each file is placed by its path below `assets`:

- **Files inside the game's archives** (models, materials, textures, data scripts, shaders): the game's file loader asks for these by name, and LiteralCrab hands it the mod's file instead. The name is the archive's folder plus the file's path inside the archive. For example, `controllerbuttons.mtb` inside `assets/ui/ui.zip` is `assets/ui/controllerbuttons.mtb` in a mod. Textures (`.tbody`) are named after their content hash and are global: `assets/<any folder>/textures/0389b6b276ccc3af.tbody` replaces that texture wherever it is used.
- **Files the game opens from disk** (videos, sound banks): the mod's file is opened instead. A packaged one is extracted once to `mods/.cache/`.
- **A whole archive** (for example a complete `assets/ui/ui.zip`, ordinary or from the game): its files are served by name as above, so it is never converted or repacked.

The game's archives are never rebuilt or copied. Startup only lists the mods' files, and a file is read when the game requests it, so even large mods start quickly and use no extra disk space. Ship only the files you changed; everything else still comes from the player's own game files, so a mod never needs to redistribute game content.

Mods are combined per file. If two mods provide the same file, the first in load order wins and the conflict is logged. The log lists each mod's number of asset files and every file that was skipped, with the reason. With `Trace=1`, every file the game's loader requests is logged; files served from a mod are marked `<- mod`. Use this to find the names to replace.

## Custom characters / Własne postacie

An enabled mod can add one separate figure to the virtual portal through `[Character]` in `mod.ini`. `[Character.Gameplay]` creates its private player actor/DNA and configures native equipment. The loader handles figure registration, portal entry, display name, fitted portraits and the private gameplay records. No Lua patch or rebuilt original archive is required for this declaration. Enable `[Mods] Enabled=1` in LiteralCrab.ini, put the mod beside the ASI under `mods`, and restart after changes.

PL: Jeden mod może dodać jedną osobną figurkę przez `[Character]`. Sekcja `[Character.Gameplay]` tworzy prywatnego aktora/DNA gracza i ustala wyposażenie. LiteralCrab obsługuje rejestrację, wpis w portalu, nazwę, dopasowany portret i prywatne rekordy rozgrywki. Sama deklaracja nie wymaga skryptu Lua ani przebudowy oryginalnych archiwów. Włącz `[Mods] Enabled=1`, umieść folder w `mods` i po zmianach uruchom grę ponownie.

### Zurg manifest example / Przykładowy manifest Zurga

This example uses the installed game's Zurg portrait. `AssetDirectory` must exist and contain the native resources you supply locally, for example the source DNA and blaster files shown below. Omit `AssetDirectory` if using only installed resources. The completed local Zurg test also includes a compatible model bundle with the missing Toy Box animation aliases and corrected hoverboard socket; the manifest alone does not author those model changes.

PL: Przykład korzysta z ikony Zurga dostępnej w instalacji. Folder `AssetDirectory` musi istnieć i zawierać własne lokalne zasoby, np. pokazane niżej DNA i pliki blastera. Pomijaj to pole, jeśli używasz wyłącznie zasobów instalacji. Gotowy lokalny test Zurga zawiera też model z brakującymi aliasami animacji Toy Boksa i poprawionym mocowaniem deski. Sam manifest nie wprowadza tych zmian w modelu.

```text
mods/my_zurg/
├── mod.ini
└── character-assets/
    └── characters/
        ├── ts_zurg.dnax
        ├── zurgbossblaster.dnax
        ├── zurgbossblaster.sx
        └── ts_zurg/...       <- optional coherent model bundle and dependencies
```

```ini
[Mod]
Name=Custom Zurg
Enabled=1

[Character]
AssetDirectory=character-assets
SkuId=1000900
Name=LiteralCrab_Zurg
Gateway=LCR_Zurg
Actor=lcr_zurg
BaseCharacter=AV_Buzz
DisplayName=Zurg
Icon=ui/icons/npc_mg_zurg

[Character.Gameplay]
SourceActor=av_zurg
SourceDNA=characters/TS_Zurg.dnax
InventorySource=AV_Buzz
InventoryExclude=ITEM_BUZZ,ABILITY_BUZZ
Prop=PROP_BUZZ
Item=ITEM_ZURG_BOSS_BLASTER
ItemControlSource=ITEM_BLASTER
ItemDisplayName=Zurg Blaster
ItemWeaponSource=ZurgBossBlasterShot
ItemToolDNA=characters/ZurgBossBlaster.dnax
ItemToolScript=characters/ZurgBossBlaster.sx
ItemProjectileRadius=0.15
ItemProjectileScale=0.1
ItemProjectileSpeed=45
ItemProjectileLifetime=2
ItemProjectileDamage=10
AimOffset=-0.95,2.1,-0.8
PreloadActors=tool_zurgbossblaster
PreloadIncludes=biped_avatar
PreloadFiles=Entity|weapons/laser_blast,Cho|choreographies/ts_zurgloaded/ts_zurgloaded,Cho|choreographies/ts_zurgball/ts_zurgball,Cho|choreographies/ts_zurgbalimpact/ts_zurgbalimpact,Audio|ToyStoryInSpace
```

Choose a unique `SkuId` in `1000100..1999999`, `Name` and `Gateway`; do not enable two examples with the same identity. At most 64 extra figures are supported. `Actor` and `Gateway` must match ignoring case and start with `LCR_` for private gameplay. `SourceActor` must be a different existing avatar; `SourceDNA` must match that source actor's native record and have compatible `ToolSetManager` and identification data. `BaseCharacter` supplies presentation defaults, not that character's complete behavior or powers. Technical names are ASCII identifiers; display text currently accepts printable ASCII. Rejected definitions and private-name conflicts are logged and keep the affected figure inactive.

PL: Wybierz unikalne `SkuId` z zakresu `1000100..1999999`, `Name` i `Gateway`; nie włączaj dwóch przykładów o tej samej tożsamości. Limit to 64 dodatkowe figurki. W prywatnej rozgrywce `Actor` i `Gateway` są zgodne bez rozróżniania wielkości liter i zaczynają się od `LCR_`. `SourceActor` wskazuje innego istniejącego awatara, a `SourceDNA` odpowiada jego natywnemu rekordowi i zawiera zgodne dane `ToolSetManager` oraz identyfikacji. `BaseCharacter` dostarcza domyślne pola prezentacji. Nazwy techniczne są identyfikatorami ASCII; tekst wyświetlany obsługuje obecnie drukowalne ASCII. Błędne definicje i konflikty są logowane, a dana figurka pozostaje nieaktywna.

### Attacks, tools and projectiles / Ataki, narzędzia i pociski

| Setting | Authoring purpose / Zastosowanie |
| --- | --- |
| `InventorySource`, `InventoryExclude` | Merge a native avatar's Items/Props/Abilities, deduplicate and exclude named tools. / Dodaj natywny profil i pomiń wskazane narzędzia. |
| `Item`, `Prop`, `Ability` | Choose native starting equipment in the corresponding group; at least one is required. / Wybierz wyposażenie początkowe w odpowiedniej grupie; wymagane jest co najmniej jedno pole. |
| `ItemControlSource`, `PropControlSource` | Borrow player control, targeting and HUD hints from another tool in the same group. / Pobierz sterowanie, celowanie i podpowiedzi innego narzędzia z tej samej grupy. |
| `ItemDisplayName`, `PropDisplayName`, `AbilityDisplayName` | Set printable ASCII labels for the declared tool IDs. These localization keys are shared; first enabled mod wins a naming conflict. / Ustaw nazwy; przy wspólnym ID wygrywa pierwszy mod. |
| `ItemWeaponSource`, `ItemToolDNA`, `ItemToolScript` | Clone one native gun, its projectile and tool actor/script privately. / Utwórz prywatną kopię natywnej broni, pocisku i aktora/skryptu narzędzia. |
| `PreloadActors`, `PreloadIncludes`, `PreloadFiles` | Add dependencies to the private character record. / Dodaj zależności prywatnej postaci. |

`PROP_BUZZ` supplies Buzz's body-only shoulder charge and ground pound. `ABILITY_BUZZ` is his jetpack; it is not the standard attack. The example excludes Buzz's innate laser and jetpack, keeps Zurg's blaster and adds normal equipment groups. Native unlock, suppression and world restrictions still apply. Compatible motions, effects and rig sockets are required. Playing an attack animation through Lua does not create a projectile, hit detection or damage.

PL: `PROP_BUZZ` daje szarżę barkiem i ground pound, a `ABILITY_BUZZ` oznacza jetpack. Przykład pomija laser i jetpack Buzza, zachowuje blaster Zurga i dodaje zwykły ekwipunek. Natywne odblokowania, blokady oraz reguły świata nadal obowiązują. Potrzebne są zgodne animacje, efekty i mocowania rigu. Samo odtworzenie animacji przez Lua nie tworzy pocisku ani obrażeń.

The weapon clone currently supports the item slot and a source item with exactly one actor descriptor. It keeps the source weapon/projectile definitions unchanged. The optional numerical overrides are separate:

| Setting | Range | Effect / Działanie |
| --- | --- | --- |
| `ItemProjectileRadius` | `0.001..10` | Collision radius / promień kolizji |
| `ItemProjectileScale` | `0.001..10` | Visual scale / skala wyglądu |
| `ItemProjectileSpeed` | `0.1..1000` | Launch speed / prędkość |
| `ItemProjectileLifetime` | `0.01..120` | Lifetime / czas istnienia |
| `ItemProjectileDamage` | `0..10000` | Damage / obrażenia |
| `AimOffset=x,y,z` | Each component `-10..10` | Targeting-camera offset / przesunięcie kamery celowania |

Omitted values inherit the source. `0.075` is a smaller visual projectile than the accepted Zurg value `0.1`; `0.25` is larger. Changing visual scale does not change collision radius. `PreloadFiles` entries are comma-separated `type|path`, with types `DNA`, `Script`, `Entity`, `Cho`, `Audio`, `AnimTreeProject` or `BehaviorMap`. `PreloadIncludes` names native Include entries without `|Include`. Each dependency list accepts up to 64 distinct entries.

PL: Klonowanie broni obsługuje obecnie slot `Item` i źródłowe narzędzie z jednym deskryptorem aktora. Pominięte parametry są dziedziczone. Skala `0.075` jest mniejsza od przyjętego `0.1`, a `0.25` większa. Skala wyglądu i kolizja są niezależne. `PreloadFiles` ma format `typ|ścieżka`; dostępne typy podano wyżej. `PreloadIncludes` wskazuje nazwy Include bez sufiksu `|Include`. Każda lista zależności dopuszcza do 64 różnych wpisów.

### Private resources and portraits / Prywatne zasoby i portrety

Use `AssetDirectory` for resources belonging to one character. It is a relative folder inside the mod, outside its global `assets` folder. Files keep their logical paths, such as `characters/ts_zurg.dnax`; keep using those logical paths in the manifest. The loader mounts them under private SKU paths and rebinds texture hashes. Two characters can therefore supply different resources at the same relative path without replacing each other's or the stock boss's files. Unprovided paths retain the existing game/mod fallback. By contrast, ordinary `assets/...` replacements apply globally.

The directory must be nonempty, with at most 4096 loose files and 512 MiB. Traversal paths, links/junctions and ambiguous resource names are rejected. ZIPs in `AssetDirectory` are not unpacked. Models, rigs, animations and materials must already use compatible native PC formats; the loader does not convert Wii U/console assets. Preserve relative binary dependencies inside BENT/OCT/animation bundles, including shared `../biped/motions/...` paths where used. The loader redirects DNA strings, dependency records and SLNG resource literals; it does not rewrite every internal reference in opaque files.

`Icon` names a native `ui/...` single-texture MTB bundle, with its DDS texture in a `.tbody` resource. A PNG alone is not a game icon: author the native bundle/texture first. A private icon belongs under the same `AssetDirectory`, for example `ui/icons/my_zurg.mtb` and `textures/<hash>.tbody`, with `Icon=ui/icons/my_zurg`. The loader fits alpha bounds and proportions to the portal's normal/selected 512×512 and small 256×256 portraits. See the [character reference](README.md) for supported DDS formats and explicit portrait overrides.

PL: `AssetDirectory` służy zasobom jednej postaci. To względny folder moda poza globalnym `assets`. Zachowaj logiczne ścieżki plików i używaj ich w manifeście. Loader tworzy prywatne ścieżki SKU i hashe tekstur; brakujące zasoby nadal korzystają z gry lub pozostałych modów. Zwykłe `assets/...` zastępuje zasoby globalnie. Folder musi być niepusty; limit to 4096 luźnych plików i 512 MiB. ZIP-y nie są rozpakowywane. Modele, rig i animacje wymagają zgodnych formatów PC oraz zachowanych zależności względnych. Loader nie konwertuje zasobów konsolowych i nie przepisuje wszystkich odwołań wewnętrznych binarnych plików.

PL: `Icon` wskazuje natywny pakiet MTB i jego teksturę DDS w `.tbody`; sam PNG nie wystarczy. Własną ikonę umieść np. jako `ui/icons/my_zurg.mtb` oraz `textures/<hash>.tbody` wewnątrz `AssetDirectory`, a w manifeście ustaw `Icon=ui/icons/my_zurg`. Loader zachowuje proporcje i marginesy portalu; pełny opis formatów i nadpisywania wariantów zawiera [dokumentacja postaci](README.md).

### Respawn and attachment fit / Odradzanie i dopasowanie mocowań

Private gameplay DNA defaults a missing `DynamicExploder.dynexp_reassemble` to numeric `1` when that gadget exists. Explicit author values are preserved. For zero-piece private avatars using the recognized native Avatar tree, the loader also supplies a private tree with an earlier `PrepareForDeath` transition so the reassembly event is received. No extra manifest respawn key is needed. Combat respawn, weapon return and shorter recovery are confirmed for the Zurg example; fall/water rules are separate.

Matching socket names do not guarantee matching directions. Zurg's right-foot-center socket inherits its left foot, unlike Buzz's right-foot parent. The accepted local Zurg model adds a `+40` degree yaw to its existing group through OCT `DataNode333.Data.Orientation`; this is a model correction, not a general manifest option. Do not apply that node number or angle to another rig without checking its topology. Read local rotations with `GetAvatarSocketOrientation` and compare the socket's parent frames in the same pose. The query neither edits the rig nor composes a world-space rotation.

PL: Gdy istnieje `DynamicExploder`, prywatne DNA domyślnie otrzymuje brakujące `dynexp_reassemble=1`; jawne wartości autora pozostają zachowane. Dla postaci bez rozbitych części, używającej rozpoznanego natywnego drzewa awatara, loader przygotowuje prywatne drzewo z wcześniejszym przejściem `PrepareForDeath`. Dodatkowy klucz respawnu w manifeście nie jest potrzebny. Na Zurgu potwierdzono powrót postaci, broni i krótsze odradzanie po walce; woda i upadek to osobne reguły.

PL: Nazwy mocowań nie gwarantują zgodnego kierunku. Przyjęty model Zurga koryguje grupę prawej stopy o `+40` stopni przez OCT `DataNode333.Data.Orientation`; to poprawka konkretnego modelu, nie opcja manifestu dla wszystkich postaci. Nie przenoś numeru węzła ani kąta na inny rig bez sprawdzenia struktury. `GetAvatarSocketOrientation` odczytuje obrót lokalny; porównuj również rodziców w tej samej pozie. Funkcja nie zmienia rigu i nie oblicza obrotu w świecie.

### Testing and current coverage / Testowanie i obecny zakres

After a restart, check the new portal row and actual spawn, then the normal attack and damage, aiming and a distant projectile hit, tool/backpack switching, combat death and recovery, and attachments while moving. Test each intended playset and a source boss encounter separately. Portal membership alone does not supply missing playset dependencies or authorize every tool. This API does not yet provide a complete arbitrary ability/skill-tree editor or guarantee independent XP/save progression.

Check `LiteralCrab.log` for `Custom figure`, `Custom assets`, rejected definitions and applied private gameplay patches. Enable tracing only while diagnosing and turn it off afterward. A missing portal row can result from an ID/name conflict, unavailable icon or invalid resource directory. A visible gun without aiming/fire needs player control/targeting data; an oversized or self-colliding projectile needs separate visual/collision tuning. See the [character reference](README.md) for the full contracts and tested limits.

PL: Po restarcie sprawdź osobny wpis i spawn, atak oraz obrażenia, celowanie i trafienie oddalonego celu, zmianę broni/plecaka, śmierć i powrót oraz mocowania podczas ruchu. Każdy docelowy playset i walkę ze źródłowym bossem testuj oddzielnie. Sam portal nie zapewnia zależności playsetu ani wszystkich uprawnień wyposażenia. API nie daje jeszcze pełnego edytora dowolnych zdolności/drzewka ani gwarancji osobnego XP i zapisu. W `LiteralCrab.log` szukaj wpisów `Custom figure`, `Custom assets`, odrzuceń i zastosowanych poprawek prywatnych. Trace włączaj tylko na czas diagnozy.

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

A patch runs immediately after the chunk it targets, every time that chunk runs. It shares the chunk's environment, so the globals and tables the chunk just defined are visible. It receives four arguments:

```lua
local result, options, log, environment = ...
-- result:      the first value the chunk returned (often nil)
-- options:     this mod's [Options], after user.ini
-- log:         log(message) writes "Mod <mod>/<file>: message" to LiteralCrab.log
-- environment: the chunk's global table, e.g. to walk everything it defined with pairs()

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

### Engine adapters (`LiteralCrab.*`)

Some game capabilities have no Lua binding on PC. LiteralCrab provides a few as adapters in the `LiteralCrab` table. Each adapter checks the game's code before it becomes available, and is left out (and logged) on any other build. Test for one with `LiteralCrab.has("AwardTool")` before using it. Functions created inside a patch, such as a wrapped menu method, keep access to `LiteralCrab` when the game calls them later.

| Adapter | Result |
| --- | --- |
| `AwardTool(player, tool[, force])` | `true` once the tool is awarded. Runs the game's own award (what its `AwardTool`/`ForceAwardTool` messages do), so playset and avatar rules apply. A tool that is already awarded is left alone, so repeated calls are harmless. |
| `UnAwardTool(player, tool)` | `true` when the tool is no longer awarded. |
| `IsToolAwarded(player, tool)` / `IsToolUnlocked(player, tool)` | The tool's current state on that player's avatar. |
| `HasToolSet(player)` | `true` while that player has an avatar with tools. Before a world loads there is none, and every tool call returns `false`. |
| `PlaceDisc(player, disc)` | `true` once the round power disc is on the virtual base, as if set under that player's figure (`player` 0 or 1). The game then applies and reports it like a disc on a real base. Placing it again changes nothing. |
| `RemoveDisc(disc)` | `true` when the disc is no longer on the base. |
| `IsDiscPlaced(disc)` | Whether the disc is on the base. |
| `GetDiscOwner(disc)` | Read-only owner of an actual placed round disc: `0`/`1` player, `-1` valid round disc absent, `-2` invalid argument/category, unavailable gateway, unreadable/inconsistent record, unsupported antenna or native read failure. Requires a numeric integer ID; strings are rejected. |
| `TagCategory(id)` | The game's own classification of a figure or disc ID: `0` figure, `1` round power disc, `2`/`3` hex power disc, `4` play set or Toy Box piece, `-1` unknown. |
| `PlayAvatarMotion(player, motion)` | Starts a mapped motion on the native shared `Misc_OverRide` channel; returns native success. |
| `StopAvatarMotion(player, motion)` | Requests stopping that named override; returns whether the native call completed. |
| `AvatarMotionEnded(player)` | Returns the native completion state of the override channel. |
| `GetAvatarSocketOrientation(player, socket)` | Read-only local node quaternion `x,y,z,w`; returns no values when unavailable. Parent rotations are not composed. |

The avatar motion/socket adapters resolve the current local avatar on each call. `player` must be a numeric integer for a currently present player (`0..7` supported by the adapter); strings are rejected. Motion and socket names accept 1–127 ASCII letters, digits or underscores. Missing/invalid motion calls return `false`; a missing/invalid socket query returns no values. Check each capability with `LiteralCrab.has`. Playback uses shared override channel 14, so coordinate with other native motions. It does not implement hitboxes, damage or animation-event actions.

```lua
-- Call from a callback while the local player's avatar is present.
local function logRightFoot(log, player)
    if not LiteralCrab.has("GetAvatarSocketOrientation") then return end
    local x, y, z, w = LiteralCrab.GetAvatarSocketOrientation(player, "Cntrl_FootCenter_Right")
    if x ~= nil then
        log(string.format("Local socket quaternion: %.6f %.6f %.6f %.6f", x, y, z, w))
    end
end
```

PL: Adaptery animacji i mocowań pobierają bieżącego awatara. `player` jest liczbowym indeksem obecnego gracza od zera; adapter dopuszcza `0..7`. Nazwa animacji/kontrolki ma 1–127 liter ASCII, cyfr lub `_`. Błędne wywołanie animacji zwraca `false`, a niedostępna kontrolka nie zwraca wartości. Sprawdzaj każdą funkcję przez `LiteralCrab.has`. Odtwarzanie współdzieli natywny kanał 14 i nie tworzy obrażeń ani akcji zdarzeń animacji. Kwaternion kontrolki jest lokalny, bez obrotów rodziców.

Round power discs are DI1 IDs `3000003`–`3000017` and `3000062`–`3000063`. `PlaceDisc` only accepts IDs the game itself classes as round discs, so figures, play set pieces and hex discs stay with the game's virtual reader. Verified examples: `3000003` Bolt's Super Strength (all damage), `3000004` Ralph's Power of Destruction (melee damage), `3000006` C.H.R.O.M.E. Damage Increaser (ranged damage), `3000008` Electro-Charge (full health). The discs a player has active can be read in UI scripts with the game's own `UI_GetItemDetails("PowerDiscs", player, "Name,Value,Type")`.

`GetDiscOwner` queries the native tag on every call, so its result survives replacement of the Lua/menu environment and does not depend on cached slots or delayed avatar effects. Its record-layout validation is independent: a mismatch withholds this function while retaining the existing disc adapters. Check `LiteralCrab.has("GetDiscOwner")` before use. Treat `-2` as unknown, never as absent or permission to remove a tag. The query does not place/remove discs, update an avatar, refresh effects, or write a profile/save. A mod must explicitly use it to reconstruct its menu state; exporting the function alone does not change existing mod UI.

```lua
if LiteralCrab.has("GetDiscOwner") then
    local owner = LiteralCrab.GetDiscOwner(3000063)
    if owner == self.InstanceData.playerNum then
        -- Reconstruct this player's card from the actual native tag.
    elseif owner == -2 then
        -- Ownership is unknown: defer changes to this disc.
    end
end
```

PL: `GetDiscOwner(disc)` odczytuje właściciela rzeczywiście założonego okrągłego dysku: `0`/`1` oznacza gracza, `-1` brak prawidłowego dysku na bazie, a `-2` błędny argument, nieznany stan lub błąd odczytu. ID musi być liczbą całkowitą, sklasyfikowaną przez grę jako round disc; tekstowe ID nie są przyjmowane. Odczyt jest niezależny od pamięci menu Lua. Niezgodny układ rekordu wyłącza tylko nową funkcję, zachowując stare adaptery. Mod sprawdza dostępność przez `LiteralCrab.has("GetDiscOwner")` i nie może traktować `-2` jak braku dysku. Funkcja nie zmienia tagów, efektów ani zapisu; mod musi osobno wykorzystać wynik do odbudowania kart.

`player` is the game's local player index (`0` for the first player, as in `self.InstanceData.playerNum`), and `tool` is a tool identifier such as `ITEM_BASEBALLGUN` or `PROP_SHOVELPACK`. Invalid tool arguments return `false`; numeric queries use the documented negative results above. Awards belong to the current avatar. A mod that should keep a tool available re-applies it, for example when the pause menu opens, rather than assuming it survives character swaps or reloads.

```lua
local menu = ...  -- patch on Presentation/PauseMenu.lua, which returns the PauseMenu class
if type(menu) == "table" and LiteralCrab.has("AwardTool") and not menu.myToolsWrapped then
    menu.myToolsWrapped = true
    local onEnter = menu.onEnter
    menu.onEnter = function(self, ...)
        LiteralCrab.AwardTool(self.InstanceData.playerNum, "ITEM_BASEBALLGUN")
        return onEnter(self, ...)
    end
end
```

## Engine features

Some content needs engine behavior that the PC build lacks. A mod requests such a feature in its `[Engine]` section. LiteralCrab installs it once at startup, and only when at least one enabled mod asks for it. Like adapters, each feature first checks the game's code and is left out (and logged) on any other build. Unknown names are logged and ignored; a value other than an integer skips the mod.

| Feature | Effect |
| --- | --- |
| `TriggerGuiScreen` | World graph `TriggerGuiScreen` nodes run their `ScreenCommand`, as in the console builds. The PC build parses the command but its node only passes the signal on. With the feature, a local player triggering the node gets: `0` the Toy Box editor ribbon, `1` Adventures, `2` the Toy Vault (for the host), `3` Catalog, `4` Collection, `5` the Toy Box travel menu (`PauseTravel`). |

`TriggerGuiScreen` uses only the PC game's own routes. The editor ribbon is opened through the same function and checks as the PC editor key, so it is refused where that key would be (outside an editable Toy Box or play set, or while the player is busy). Adventures and the travel menu return to the game on cancel rather than to the pause menu, as on console. Other than the Toy Vault, a command is ignored while the player already has a menu open. A refused command does not pass the signal on, matching the console node. Every command is logged with its result. The feature applies to every such node, including any in the game's own PC content, which were inert before. The menus normally sit on the pause menu, whose pause gives the mouse a pointer; a menu opened from the world does the same and returns the mouse to camera control once the player's menus are closed.

## Patches from LiteralCrab options

Two settings in LiteralCrab.ini work through the same script loader: `[ToyBox] LimitScale` patches `GameDB/Core/ToyBoxStats_Limits.lua`, and `[Gameplay] CharactersInAnyPlayset` reads original playset membership, registers avatar variants from actor tables and enables `WantToyBoxFX`/`WantBloatFX` in the six play sets. It does not append to or replace `PlayableAvatars`. The built-in patches appear as `LiteralCrab/toybox_limits`, `LiteralCrab/character_playsets` and `LiteralCrab/character_actor_registry`, and run after every mod's patch for the same chunk. They are only registered while their option is on.

The character option exposes `LiteralCrab.RegisterPlaysetAvatars(playset, commaSeparatedAvatars)` to its built-in patches. Actor-table registrations use `RumpusRoom` as a provisional home; a specific native playset takes precedence regardless of table loading order. Crystal/base records share one home. An unregistered actor keeps native suffix/fallback behavior. The native option also loads all dependency maps using the paired Toy Box load/unload route, making avatar statistics and choreography records available. If the script loader cannot be installed, native patches and the detour are removed. See the [repair notes](CHARACTER_FREEDOM_FIX.en.md) for validation limits.

`LiteralCrab.IsRegisteredAvatar(actorName)` identifies names registered by the original actor/zone data. `LiteralCrab/character_preloads` preserves the common playset record's files and adds original avatar-stat and effect Includes. The effect Include is generated from existing avatar/tool dependency records and contains only choreography files; character models/AI retain their original loading route. `[Gameplay] TraceCharacterFreedom=1` reports bounded native EXP, FX-flag and file-request evidence plus actual stat-package loads; it does not grant progress. Keep it off outside diagnosis.

## Guarantees and limits

- With no mods installed, or `[Mods] Enabled=0`, the game's Lua loader is not hooked at all. Engine features are hooked only while a mod requests them.
- The loader checks the signatures of the game's Lua functions before installing. On any other executable it does nothing.
- Per compiled chunk, the cost is a name lookup without memory allocation. Unnamed chunks are searched only when an installed mod uses `contains:`. Scripts are read and validated only at startup.
- A native fault while composing a patch disables the mod loader for the rest of the session and is logged. Display features are unaffected.
- Distribute your authored patch code and original assets. Do not redistribute extracted game assets or scripts; local tests may read resources from the player's own installation. Private resource mounting does not change this distribution policy.
