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

An enabled mod can add one separate figure to the virtual portal through `[Character]` in `mod.ini`. With `Source=`, the figure gets its own private player actor, DNA, inventory and model. LiteralCrab reads the installed game's data at startup and fills in everything else, so a basic character needs its identity and the native actor it is made from. No Lua patch, DNAX file or rebuilt archive is required. Enable `[Mods] Enabled=1` in LiteralCrab.ini, put the mod beside the ASI under `mods`, and restart after changes.

PL: Mod może dodać jedną osobną figurkę przez `[Character]`. `Source=` tworzy prywatnego aktora gracza, DNA, ekwipunek i model. LiteralCrab odczytuje dane zainstalowanej gry i uzupełnia resztę, więc podstawowa postać wymaga tylko tożsamości i natywnego aktora źródłowego. Nie są potrzebne skrypty Lua, pliki DNAX ani przebudowane archiwa. Włącz `[Mods] Enabled=1`, umieść folder w `mods` i po zmianach uruchom grę ponownie.

### Quick start / Szybki start

```ini
[Mod]
Name=Butch
Enabled=1

[Character]
Name=LiteralCrab_Butch
Source=butch
DisplayName=Butch
```

That is a complete character. `Source` names a native actor row (here the Lone Ranger enemy `butch`) or, for an actor without a row, its DNA file (`Source=characters/PNF_Ferb.dnax`). `LiteralCrab.log` lists every decision the loader made, for example:

```text
Custom gameplay LiteralCrab_Butch: Template LR_LoneRanger chosen automatically (83 shared animation files); set Template to override
Custom gameplay LiteralCrab_Butch: source tool profile is incomplete; using LR_LoneRanger's basic inventory
Custom gameplay LiteralCrab_Butch: starting equipment PROP_LONERANGER chosen automatically
Custom gameplay LiteralCrab_Butch: non-player source: LR_LoneRanger's player body with characters/Butch.dnax's model and identity
Custom gameplay LiteralCrab_Butch: player entity built from LR_LoneRanger's animation sets for characters/LR_Butch/LR_Butch (662 files)
```

PL: To pełna definicja postaci. `Source` wskazuje natywny wiersz aktora (tu przeciwnika `butch` z Lone Rangera) albo, gdy aktor nie ma wiersza, jego plik DNA. Każdą automatyczną decyzję loader zapisuje w `LiteralCrab.log`.

### What is prepared automatically / Co loader przygotowuje automatycznie

Each step uses verified native data and can be overridden with the setting in brackets. Ambiguous cases are errors that name the setting to add; nothing is guessed silently and a rejected character is never partly registered.

| Step | Automatic behavior |
| --- | --- |
| Source DNA | The actor row's `DNAFile` (`SourceDNA`). A `.dnax` path works for actors without a row. |
| Player body | A source DNA with the avatar gadget, controller mapping, tool manager and `characters/Avatar.btx` tree is used as is. Otherwise (NPCs, bosses) the template's player DNA is used and only the source's model, explosion pieces, name, dialog ID and scale are copied onto it (`Body=source|template`). |
| Template | A stock playable biped supplying the player body, animations and basic inventory: `Template`, else a biped `BaseCharacter`, else the stock biped whose model shares the most animation files with the source model, else `AV_Buzz`. |
| Actor type | The avatar controller drives only `PlayerControllable` actor types. A source type outside that filter (for example the undefined `AV_Ferb`) becomes the template's type on the private copy. |
| Inventory | Native tool profiles are keyed by the DNA's `ActorTypeMask`. A source with a complete player profile (`ITEM_NONE`, `PROP_NONE`, `ABILITY_NONE`) keeps it. A source without one, or with an NPC profile, gets the template's profile without the template's own starting Item and Ability (`InventorySource`, `InventoryExclude`). Declared tools missing from both profiles are added. |
| Starting equipment | Each empty slot uses the source DNA's own usable start tool; the Prop slot falls back to the template's body-only standard attack, such as `PROP_BUZZ` (`Item`, `Prop`, `Ability`). Start tools that do not exist in this build (for example `ITEM_FERB`) are removed. |
| NPC tools | An Items tool without player control contexts (a boss weapon) borrows `ITEM_BLASTER`'s controls, targeting and HUD hints (`ItemControlSource`). |
| Weapon clone | Any `ItemProjectile...` value clones the item's gun privately. Its tool DNA, script and gun are read from the item's tool actor (`ItemToolDNA`, `ItemToolScript`, `ItemWeaponSource`). Shipped projectile model and effect choreographies are preloaded. |
| Rows and dependencies | The source's Avatar actor and dependency rows are cloned. A source without one (cut avatars, NPCs) clones the template's rows with its model and pieces entries replaced, and drops the template's `modelDef`. The shared `biped_avatar` include and the actors of declared tools are added (`PreloadActors`, `PreloadIncludes`, `PreloadFiles`). |
| Player model | The avatar tree plays named animation sets that player entities define. A player entity missing some of the template's sets is completed with them; an entity without player sets, or a mesh without any entity, is bound to the template's whole animation entity (`Animations=source|complete|template`). |
| Identity | `Gateway`/`Actor` default to `LCR_<Name>`, `SkuId` to a stable value derived from `Name`, `DisplayName` to `Name`, `Icon` to the generic `ui/charactericons/blankavatar`, and `BaseCharacter` to the template. |

The generated player model is a private BENT entity that keeps the source mesh, materials and textures and borrows the template's channels, layout and animation sets. Every file it references is served lazily from the player's own game archives under `characters/literalcrab/assets/<SkuId>/...`, with private texture keys. Stock entities, actor rows, profiles, tools and the source NPC are never changed. A model you supply in `AssetDirectory` at the same logical path replaces the generated one. Shared animation sets are not retargeting: a rig with different proportions plays the template's motions as they are.

PL: Każdy krok korzysta ze zweryfikowanych danych gry i można go nadpisać ustawieniem w nawiasie. Niejednoznaczne przypadki kończą się błędem z nazwą ustawienia do dodania; odrzucona postać nigdy nie jest rejestrowana częściowo. Źródło nie będące awatarem gracza otrzymuje ciało gracza z szablonu i tylko własny model, części, nazwę, dialog i skalę. Szablon to `Template`, dwunożny `BaseCharacter`, awatar o najbardziej zgodnych animacjach albo `AV_Buzz`. Typ aktora spoza `PlayerControllable` (np. nieistniejący `AV_Ferb`) zostaje zastąpiony typem szablonu. Profile narzędzi są przypisane do `ActorTypeMask`; brakujący lub NPC-owy profil zastępuje podstawowy ekwipunek szablonu bez jego osobistego Item i Ability. Puste sloty startowe wypełnia własne narzędzie źródła albo standardowy atak szablonu; nieistniejące narzędzia (np. `ITEM_FERB`) są usuwane. Broń NPC otrzymuje sterowanie `ITEM_BLASTER`. Wartości `ItemProjectile...` klonują broń razem z wykrytym DNA, skryptem i efektami. Brak wiersza aktora oznacza klon wiersza szablonu z podmienionym modelem. Model gracza to prywatny BENT z siatką źródła i zestawami animacji szablonu, serwowany z własnych archiwów gracza; oryginalne zasoby, wiersze i profile pozostają niezmienione. Wspólne animacje nie są retargetowaniem.

### Examples / Przykłady

**A. A cut playable without a tool profile: Ferb.** The shipped `characters/PNF_Ferb.dnax` is a complete player DNA, but the game has no `AV_Ferb` tool profile, no `ITEM_FERB`/`PROP_FERB` tools, no actor or dependency row, no `AV_Ferb` actor type and no animated entity for `characters/PNF_Ferb/PNF_Ferb` (only the mesh). The loader handles all of these; Phineas is the natural template for his rig:

```ini
[Character]
Name=LiteralCrab_Ferb
Source=characters/PNF_Ferb.dnax
Template=PNF_Phineas
DisplayName=Ferb
Icon=ui/icons/hud_playericons_ferb
```

**B. An NPC with an NPC profile: Butch.** `butch` is a Cowboy-AI enemy: BipedNPC animation tree, no avatar gadgets, an `LR_Butch` profile holding enemy weapons, an entity with only enemy and riding animation sets, and dependency rows only in the Lone Ranger play set. The quick-start manifest above is enough; the Lone Ranger template is found automatically.

**C. An existing native tool.** Any tool ID can be a starting slot. It joins the private inventory, and an NPC weapon gets player controls:

```ini
[Character.Gameplay]
Item=ITEM_PISTOL
```

```ini
[Character.Gameplay]
Item=ITEM_ZURG_BOSS_BLASTER
ItemProjectileRadius=0.15
ItemProjectileScale=0.1
```

**D. A standalone weapon from a native template.** See [Standalone custom tools](#standalone-custom-tools--osobne-customowe-narzędzia):

```ini
[Tool]
Id=LCR_BUTCH_BLASTER
Source=ITEM_ZURG_BOSS_BLASTER
Characters=LiteralCrab_Butch
Equip=1
ProjectileRadius=0.15
ProjectileScale=0.1
ProjectileSpeed=45
```

**E. Properties in plain Lua.** `DNA=` names a data-only Lua table merged into the private DNA after the loader's own changes. `Remove` lists dotted paths to delete. Ferb's DNA borrows Perry's explosion pieces; removing them selects the zero-piece death and respawn path:

```ini
[Character]
Name=LiteralCrab_Ferb
Source=characters/PNF_Ferb.dnax
Template=PNF_Phineas
DNA=ferb.lua
```

```lua
-- ferb.lua: plain readable Lua, validated at startup.
DNA = {
    AgentGadgets = {
        Health = { health = 600 },
        TargetMode = { TargetingNormalOffset = { -0.9, 2.0, 0 } },
    },
}
Remove = { "AgentGadgets.DynamicExploder.dynexp_pieceFile" }
```

Tables merge by key; a list (such as an offset) and any plain value replace the native value. The file may only assign `DNA` and `Remove`, using literals and table constructors. Calls, expressions and other globals are rejected at startup with the line's reason. For logic, `DNAScript=` names an ordinary Lua script that runs on the private DNA after the overlay, with the same arguments and library functions as a [patch](#patches).

PL: A — Ferb ma pełne DNA gracza, ale w grze brakuje profilu `AV_Ferb`, narzędzi `ITEM_FERB`/`PROP_FERB`, wiersza aktora i zależności, typu `AV_Ferb` oraz animowanej encji (jest sama siatka). Loader uzupełnia wszystkie braki; szablonem jest Phineas. B — Butch to przeciwnik z AI Cowboy, drzewem BipedNPC, profilem z bronią wrogów i encją bez zestawów animacji gracza; wystarczy manifest z szybkiego startu. C — każde natywne narzędzie może być wyposażeniem startowym; trafia do prywatnego ekwipunku, a broń NPC dostaje sterowanie gracza. D — osobna broń z natywnego szablonu. E — `DNA=` wskazuje czytelną tabelę Lua (tylko dane), scalaną z prywatnym DNA; `Remove` usuwa ścieżki. Tabele scalają się po kluczach, listy i wartości zastępują oryginał. `DNAScript=` uruchamia zwykły skrypt Lua na prywatnym DNA, jak patch.

### Before and after / Przed i po

The accepted Zurg character previously needed this manifest; every native record had to be named by hand:

```ini
[Character]
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

The same result now comes from:

```ini
[Character]
SkuId=1000900
Name=LiteralCrab_Zurg
Source=av_zurg
Template=AV_Buzz
DisplayName=Zurg
Icon=ui/icons/npc_mg_zurg

[Character.Gameplay]
Item=ITEM_ZURG_BOSS_BLASTER
ItemProjectileRadius=0.15
ItemProjectileScale=0.1
ItemProjectileSpeed=45
ItemProjectileLifetime=2
ItemProjectileDamage=10
AimOffset=-0.95,2.1,-0.8
PreloadFiles=Cho|choreographies/ts_zurgloaded/ts_zurgloaded,Audio|ToyStoryInSpace
```

The loader derives the same inventory, standard attack, blaster controls, gun clone, tool actor, `biped_avatar` include, projectile model and choreographies, and also completes Zurg's entity with Buzz's Toy Box animation set. The remaining preloads are the boss script's charge effect and the Toy Story sound bank; their names are not derivable from the gun's data. When this replaces an existing character, also keep its `SkuId` and `Gateway` (here `Gateway=LCR_Zurg`) so the portal identity stays the same. The old manifest still works unchanged, and an `AssetDirectory` model still takes precedence. `ItemDisplayName` is optional.

PL: Wcześniej Zurg wymagał ręcznego wskazania każdego rekordu natywnego. Teraz ten sam wynik daje krótki manifest: loader sam wyznacza ekwipunek, standardowy atak, sterowanie, klon broni, aktora narzędzia, include `biped_avatar`, model pocisku i choreografie, a encję Zurga uzupełnia zestawem animacji Toy Boksa od Buzza. Pozostałe dwa preloady (efekt ładowania ze skryptu bossa i bank dźwięków Toy Story) nie wynikają z danych broni. Zachowaj `SkuId` przy zastępowaniu istniejącej postaci. Stary manifest nadal działa bez zmian.

### [Character] settings / Ustawienia

| Setting | Meaning / Znaczenie |
| --- | --- |
| `Name` | Required unique identifier (1–127 ASCII letters, digits, `_`); also names the portal portrait. / Wymagany unikalny identyfikator. |
| `Source` | Native actor row name, or a relative `.dnax` path. Enables gameplay. / Natywny aktor albo ścieżka `.dnax`; włącza rozgrywkę. |
| `Template` | Stock playable biped for the body, animations and basic inventory: `AV_Buzz`, `AV_Woody`, `AV_Jessie`, `LR_LoneRanger`, `LR_Tonto`, `PIR_JackSparrow`, `PIR_Barbossa`, `PIR_DavyJones`, `AV_MrIncredible`, `AV_ElastiGirl`, `AV_Dash`, `AV_Violet`, `AV_Syndrome`, `MU_Sully`, `MU_Mike`, `MU_Randall`, `TB_MickeyMouse`, `NBC_JackSkellington`, `TAN_Rapunzel`, `FRO_Anna`, `FRO_Elsa`, `WR_Ralph`, `WR_Vanellope`, `PNF_Perry`, `PNF_Phineas`. / Szablon ciała, animacji i ekwipunku. |
| `Body` | `auto` (default), `source` or `template`. / Źródło ciała gracza. |
| `Animations` | `auto` (default), `source`, `complete` or `template`. / Źródło zestawów animacji. |
| `DNA`, `DNAScript` | Mod-relative Lua overlay (data only, up to 1 MiB) and script (up to 4 MiB) for the private DNA. / Nakładka i skrypt Lua dla prywatnego DNA. |
| `DisplayName`, `Description` | Printable ASCII text. / Tekst w drukowalnym ASCII. |
| `Icon` | Native `ui/...` single-texture bundle; default `ui/charactericons/blankavatar`. / Natywna ikona. |
| `SkuId` | `1000100..1999999`; default derived from `Name`. Set it to keep the identity when renaming, or when the derived value collides. / Domyślnie wyliczane z `Name`. |
| `Gateway`, `Actor` | Private names, equal ignoring case and starting with `LCR_`; default `LCR_<Name>`. / Prywatne nazwy `LCR_`. |
| `BaseCharacter` | Stock figure for portal presentation defaults; default is the template (`AV_Buzz` without gameplay). / Domyślne pola prezentacji. |
| `Playset`, `AssetDirectory` | As before; see private resources below. / Jak wcześniej. |

Unknown `[Character]` keys are logged and ignored. At most 64 extra figures are supported; do not enable two mods with the same `SkuId`, `Name` or `Gateway`.

PL: Nieznane klucze `[Character]` są logowane i pomijane. Limit to 64 dodatkowe figurki; nie włączaj dwóch modów o tym samym `SkuId`, `Name` lub `Gateway`.

### [Character.Gameplay] (optional) / Opcjonalna sekcja rozgrywki

Every key is optional and only overrides an automatic choice. `SourceActor`/`SourceDNA` are the older spelling of `Source`; use one or the other.

| Setting | Authoring purpose / Zastosowanie |
| --- | --- |
| `InventorySource`, `InventoryExclude` | Merge a native profile (by `ActorTypeMask`, for example `AV_Buzz`) and exclude named tools. / Dodaj profil i pomiń narzędzia. |
| `Item`, `Prop`, `Ability` | Starting equipment in the corresponding group. / Wyposażenie początkowe. |
| `ItemControlSource`, `PropControlSource` | Player control, targeting and HUD hints from another tool of the same group. / Sterowanie innego narzędzia. |
| `ItemDisplayName`, `PropDisplayName`, `AbilityDisplayName` | Printable ASCII labels; these localization keys are shared, and the first enabled mod wins. / Etykiety narzędzi. |
| `ItemWeaponSource`, `ItemToolDNA`, `ItemToolScript` | Explicit gun, tool DNA and native script for the private weapon clone. / Jawne źródła klonu broni. |
| `PreloadActors`, `PreloadIncludes`, `PreloadFiles` | Extra dependencies of the private character. / Dodatkowe zależności. |

| Setting | Range | Effect / Działanie |
| --- | --- | --- |
| `ItemProjectileRadius` | `0.001..10` | Collision radius / promień kolizji |
| `ItemProjectileScale` | `0.001..10` | Visual scale / skala wyglądu |
| `ItemProjectileSpeed` | `0.1..1000` | Launch speed / prędkość |
| `ItemProjectileLifetime` | `0.01..120` | Lifetime / czas istnienia |
| `ItemProjectileDamage` | `0..10000` | Damage / obrażenia |
| `AimOffset=x,y,z` | Each component `-10..10` | Targeting-camera offset / przesunięcie kamery celowania |

Omitted values inherit the source. A boss gun's own values are tuned for NPCs (Zurg's has radius 5, which explodes under the avatar), so set radius and scale when giving one to a player. Changing visual scale does not change collision radius. `PreloadFiles` entries are comma-separated `type|path`, with types `DNA`, `Script`, `Entity`, `Cho`, `Audio`, `AnimTreeProject` or `BehaviorMap`. `PreloadIncludes` names native Include entries without `|Include`. Each list accepts up to 64 distinct entries. `PROP_BUZZ` and the other avatars' `PROP_...` rows without a tool actor are body-only standard attacks (shoulder charge, ground pound); `ABILITY_BUZZ` is Buzz's jetpack. Native unlock, suppression and world rules still apply. Playing an attack animation through Lua does not create a projectile, hit detection or damage.

PL: Wszystkie klucze są opcjonalne i jedynie nadpisują wybór automatyczny. Pominięte wartości dziedziczy źródło. Broń bossa ma wartości dla NPC (promień 5 Zurga wybucha pod awatarem), więc ustaw promień i skalę. `PreloadFiles` ma format `typ|ścieżka`, `PreloadIncludes` nazwy Include bez `|Include`, a każda lista do 64 wpisów. `PROP_...` bez aktora narzędzia to standardowy atak ciała; `ABILITY_BUZZ` to jetpack. Natywne odblokowania i reguły świata nadal obowiązują.

### Lua and DNAX / Lua a DNAX

`.dnax` files are not a separate format. Each is a Lua 5.1 chunk that only builds a table and assigns it to the global `DNA`; the game normally ships them precompiled. The engine runs the chunk in a data state without the standard library and converts the resulting table into native components. Behavior trees (`.btx`) and behavior maps (`.bmx`) work the same way. Every shipped DNA, behavior and data-map chunk uses only table-building instructions, which is why LiteralCrab can read them exactly at startup and refuses anything else instead of approximating it.

So a text `.dnax` with a plain Lua table already loads, and LiteralCrab's `DNA=` overlay is ordinary readable Lua that the game converts into the same native structures. What cannot be ordinary Lua:

- `.sx` files are SLNG, the engine's own compiled behavior bytecode for tools, AI and gameplay actions. They are not Lua, and no SLNG compiler is available. Custom behavior therefore reuses native scripts (the private clones keep their instructions and only rename resource strings) or game Lua reached through [patches](#patches) and the [engine adapters](#engine-adapters-literalcrab).
- Models, rigs, animation clips, materials and textures are native binary resources.
- A DNA table must still describe components the engine knows. A data-only overlay cannot add new component types, and the loader does not validate component field names against the engine; misspelled fields are ignored by the game.

PL: Pliki `.dnax` to fragmenty Lua 5.1, które tylko budują tabelę `DNA`; zwykle są prekompilowane. Silnik wykonuje je bez biblioteki standardowej i zamienia tabelę na komponenty natywne. Tak samo działają `.btx` i `.bmx`. Tekstowe `.dnax` z tabelą Lua już działa, a nakładka `DNA=` to zwykły Lua przekształcany przez grę w te same struktury. Zwykłym Lua nie mogą być: skrypty `.sx` (SLNG, natywny bytecode zachowań, nie Lua), modele, rigi, animacje, materiały i tekstury. Nakładka nie dodaje nowych typów komponentów, a błędne nazwy pól gra pomija.

### Private resources and portraits / Prywatne zasoby i portrety

Use `AssetDirectory` for resources belonging to one character. It is a relative folder inside the mod, outside its global `assets` folder. Files keep their logical paths, such as `characters/ts_zurg.dnax`; keep using those logical paths in the manifest. The loader mounts them under private SKU paths and rebinds texture hashes. Two characters can therefore supply different resources at the same relative path without replacing each other's or the stock boss's files. Unprovided paths retain the existing game/mod fallback. By contrast, ordinary `assets/...` replacements apply globally.

The directory must be nonempty, with at most 4096 loose files and 512 MiB. Traversal paths, links/junctions and ambiguous resource names are rejected. ZIPs in `AssetDirectory` are not unpacked. Models, rigs, animations and materials must already use compatible native PC formats; the loader does not convert Wii U/console assets. Preserve relative binary dependencies inside BENT/OCT/animation bundles, including shared `../biped/motions/...` paths where used. The loader redirects DNA strings, dependency records and SLNG resource literals; it does not rewrite every internal reference in opaque files.

`Icon` names a native `ui/...` single-texture MTB bundle, with its DDS texture in a `.tbody` resource. A PNG alone is not a game icon: author the native bundle/texture first. A private icon belongs under the same `AssetDirectory`, for example `ui/icons/my_zurg.mtb` and `textures/<hash>.tbody`, with `Icon=ui/icons/my_zurg`. The loader fits alpha bounds and proportions to the portal's normal/selected 512×512 and small 256×256 portraits. See the [character reference](README.md) for supported DDS formats and explicit portrait overrides.

PL: `AssetDirectory` służy zasobom jednej postaci. To względny folder moda poza globalnym `assets`. Zachowaj logiczne ścieżki plików i używaj ich w manifeście. Loader tworzy prywatne ścieżki SKU i hashe tekstur; brakujące zasoby nadal korzystają z gry lub pozostałych modów. Zwykłe `assets/...` zastępuje zasoby globalnie. Folder musi być niepusty; limit to 4096 luźnych plików i 512 MiB. ZIP-y nie są rozpakowywane. Modele, rig i animacje wymagają zgodnych formatów PC oraz zachowanych zależności względnych. `Icon` wskazuje natywny pakiet MTB i jego teksturę DDS w `.tbody`; sam PNG nie wystarczy.

### Respawn and attachment fit / Odradzanie i dopasowanie mocowań

Private gameplay DNA defaults a missing `DynamicExploder.dynexp_reassemble` to numeric `1` when that gadget exists. Explicit author values are preserved. For zero-piece private avatars using the recognized native Avatar tree, the loader also supplies a private tree with an earlier `PrepareForDeath` transition so the reassembly event is received. No extra manifest respawn key is needed. Combat respawn, weapon return and shorter recovery are confirmed for the Zurg example; fall/water rules are separate.

Matching socket names do not guarantee matching directions. Zurg's right-foot-center socket inherits its left foot, unlike Buzz's right-foot parent. The accepted local Zurg model adds a `+40` degree yaw to its existing group through OCT `DataNode333.Data.Orientation`; this is a model correction, not a general manifest option. Do not apply that node number or angle to another rig without checking its topology. Read local rotations with `GetAvatarSocketOrientation` and compare the socket's parent frames in the same pose.

PL: Gdy istnieje `DynamicExploder`, prywatne DNA domyślnie otrzymuje brakujące `dynexp_reassemble=1`; jawne wartości autora pozostają zachowane. Dla postaci bez rozbitych części loader przygotowuje prywatne drzewo z wcześniejszym `PrepareForDeath`. Nazwy mocowań nie gwarantują zgodnego kierunku; korekta deski Zurga dotyczy tylko jego modelu.

### Testing and current coverage / Testowanie i obecny zakres

After a restart, check the new portal row and actual spawn, then movement and animation, the normal attack and damage, aiming and a distant projectile hit, tool/backpack switching, combat death and recovery, and attachments while moving. Test each intended playset separately. Portal membership alone does not prove a character is playable. Check `LiteralCrab.log` for `Custom figure`, `Custom gameplay`, `Custom assets` and `Custom tool` lines and for applied private patches. Enable tracing only while diagnosing.

Status: the Zurg character and TS3 blaster are confirmed in game with explicit manifests. With the minimal manifests above, Butch and Ferb have been spawned in a Toy Box from the virtual base: both render their own models, walk and jump with the template's animation sets, and Butch aims the `Source`-only blaster. Damage, the standard attack, death and recovery, tool switching and other play sets are not yet confirmed for them. The private actor row is cloned into the actor lists that contain the source or template row (for stock avatars, the Toy Box list). This API does not provide an ability/skill-tree editor or guarantee independent XP/save progression.

PL: Po restarcie sprawdź wpis w portalu i spawn, ruch i animacje, atak i obrażenia, celowanie, zmianę narzędzi, śmierć i powrót oraz mocowania. Każdy playset testuj osobno; sam portal nie dowodzi grywalności. W logu szukaj wpisów `Custom figure`, `Custom gameplay`, `Custom assets`, `Custom tool`. Stan: Zurg i blaster TS3 są potwierdzone w grze z jawnymi manifestami. Butch i Ferb z minimalnych manifestów pojawili się w Toy Boksie: mają własne modele, chodzą i skaczą z animacjami szablonu, a Butch celuje blasterem z `Source`. Obrażeń, ataku, śmierci, zmiany narzędzi i innych playsetów jeszcze nie potwierdzono.

## Standalone custom tools / Osobne customowe narzędzia

`[Tool]` registers a new native tool ID, independently of `[Character]`, and grants it to custom characters. The simplest tool names its ID, a native tool to copy and the characters:

```ini
[Mod]
Name=Butch blaster
Enabled=1

[Tool]
Id=LCR_BUTCH_BLASTER
Source=ITEM_ZURG_BOSS_BLASTER
Characters=LiteralCrab_Butch
Equip=1
ProjectileRadius=0.15
ProjectileScale=0.1
```

From `Source` the loader takes the tool actor (the row's single actor descriptor), its DNA from the actor list, and, when projectile values are set, its native script and the unique gun that script names. A player tool is its own `BaseTool`. An NPC-only item has no player control contexts, so `ITEM_BLASTER` supplies the controls, attachment, selection and upgrade rules (`BaseTool`). Shipped projectile models and effect choreographies are preloaded; other dependencies (a boss script's own effects, sound banks) go in `PreloadFiles`. One mod declares one tool; up to 64 custom tools can be enabled. A tool can serve many characters through `Characters=A,B` or `Characters=*`. Original actor/tool rows and every native inventory profile keep their bytes.

PL: `[Tool]` rejestruje nowe ID narzędzia i nadaje je customowym postaciom. Najprostsze narzędzie podaje ID, natywne narzędzie do skopiowania i postacie. Z `Source` loader bierze aktora narzędzia, jego DNA, a przy ustawieniach pocisku także skrypt i jedyną broń, którą skrypt wskazuje. Narzędzie gracza jest własną bazą; broń NPC dostaje bazę `ITEM_BLASTER`. Modele pocisków i choreografie z gry są wczytywane automatycznie; inne zależności dodaj w `PreloadFiles`. Jeden mod to jedno narzędzie, limit 64; `Characters` może wskazać wiele postaci lub `*`.

A tool with its own model and icon, as in the local TS3 Zurg gun conversion:

```ini
[Tool]
Id=LCR_TS3_ZURG_BLASTER
Source=ITEM_ZURG_BOSS_BLASTER
Characters=LiteralCrab_Zurg
Equip=1
DisplayName=TS3 Zurg Blaster
AssetDirectory=tool-assets
Model=characters/ts3_zurggun/ts3_zurggun
ModelHandle=ControlHandle
Icon=ui/icons/lcr_ts3_zurg_blaster
ProjectileRadius=0.15
ProjectileScale=0.1
ProjectileSpeed=45
ProjectileLifetime=2
ProjectileDamage=10
PreloadFiles=Cho|choreographies/ts_zurgloaded/ts_zurgloaded,Audio|ToyStoryInSpace
```

The model/icon paths require a compatible native bundle in the mod; the manifest does not convert a source game's resources. The local TS3 gun conversion preserves its source shape, topology, UVs and diffuse textures, with a uniform transform into the previous DI1 player gun's grip frame. Loading, aiming, firing and grip have been checked locally with the explicit form of this manifest (`BaseTool=ITEM_BLASTER`, `SourceActor`, `SourceDNA`, `SourceScript`, `WeaponSource`), which still works. The barrel animation is not connected, and the hose/backpack are omitted.

| Field | Contract / Znaczenie |
| --- | --- |
| `Id` | Required unique `LCR_` identifier, 5–64 ASCII bytes. / Unikalny identyfikator `LCR_`. |
| `Source` | Native tool to copy; needs exactly one actor descriptor. / Natywne narzędzie do skopiowania. |
| `Characters` | Comma-separated custom figure `Name` values, or only `*`. / Nazwy customowych postaci lub `*`. |
| `BaseTool` | Optional player tool whose controls, attachment, selection, world and upgrade rules the new row inherits; default `Source`, or `ITEM_BLASTER` for an NPC-only item. Without `Source`, the base tool is also copied. / Baza reguł narzędzia. |
| `ControlSource` | Optional same-group targeting, control-context and HUD donor. / Sterowanie innej pozycji z tej samej grupy. |
| `Equip` | `0` by default; `1` makes this the starting tool in its group, in memory. A second tool cannot claim the same slot. / Wyposażenie początkowe. |
| `SourceActor`, `SourceDNA`, `SourceScript`, `WeaponSource` | Explicit tool actor, its DNA, native PC SLNG script and gun; inferred from `Source` when omitted. / Jawne źródła; domyślnie z `Source`. |
| `Model`, `ModelHandle` | Optional extensionless Entity path replacing the DNA's model, and the attachment control it exports. / Model i uchwyt. |
| `Icon` | Optional native `ui/...` MTB/TEXB icon; default the base tool's icon. / Ikona. |
| `AssetDirectory` | Optional private resource folder, with the same limits as character assets. / Prywatne zasoby. |
| `DisplayName`, `Description` | Printable ASCII localization text. / Nazwa i opis. |
| `DNA`, `DNAScript` | Lua overlay and script for the private tool DNA, as for characters. / Nakładka i skrypt Lua. |
| `PreloadActors`, `PreloadIncludes`, `PreloadFiles` | Additional private tool dependencies. / Dodatkowe zależności. |

`ProjectileRadius`, `ProjectileScale`, `ProjectileSpeed`, `ProjectileLifetime` and `ProjectileDamage` use the ranges of their `ItemProjectile...` equivalents and apply to Items. Omitted values inherit the source gun. The base tool's native upgrade prerequisite still applies; this API does not award unrelated progression.

Private resources mount under `characters/literalcrab/assets/tools/<lowercase Id>/...`, with separate texture hashes. Actor, script and weapon names also belong to the new ID. Only the targeted private inventories and their preload lists receive the new row. `Equip=1` temporarily replaces that figure's declared starting slot and any per-character weapon clone for that slot; the character manifest is unchanged. Disabling the tool mod through `user.ini` restores the prior character configuration on restart. A bad declaration or failed resource preparation publishes no partial tool; runtime private actor/dependency/weapon conflicts disable affected custom portal figures instead of replacing the conflicting records.

Private MTB files use native BNDL v1 / TEXB v3. MATP v2 metadata is checked before mounting: string tables, repeated material count and shader indices must be in bounds. The loader preserves the shader identities and opaque parameter blocks; those shaders must already exist in the game. Material authors must use the native grouped arrays and distinct material cache keys. These checks do not validate shader-specific parameter lengths or convert foreign shaders.

PL: Pola `Projectile...` mają zakresy odpowiednich `ItemProjectile...`. Natywny warunek odblokowania bazy nadal obowiązuje. Zasoby, tekstury, aktor, skrypt i pocisk mają prywatne nazwy. Nowy wpis trafia tylko do wskazanych prywatnych ekwipunków. `Equip=1` zmienia slot początkowy w pamięci; plik postaci pozostaje zachowany. Błędna deklaracja nie publikuje części narzędzia, a konflikty rekordów blokują powiązane figurki. Prywatne MTB wymagają BNDL v1 / TEXB v3; metadane MATP v2 są sprawdzane przed montowaniem, a shadery muszą istnieć w grze.

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
