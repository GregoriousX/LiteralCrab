# Characters and added content

## Added content and native file access

With `[Mods] Enabled=1`, files below an active mod's `assets` directory are available both to the engine's member loader and to native Windows file reads, including files whose original path does not exist. This covers new dialogue banks and sidecars, character resources and other extensions. Earlier active mods win each conflicting path; unique files from later mods remain available. Disabled mods contribute nothing. Replacement game archives keep their existing member-loading behavior.

Loose files are read from their mod folder. Files supplied inside a mod package are extracted to `mods/.cache` during startup, before game/audio initialization, and reused while the package is unchanged. Directory attribute checks recognize directories containing indexed files. No junctions or copies into the original `assets` directory are needed. This provides file access; a new character or content type still needs its appropriate game definitions and compatible formats. Windows directory enumeration is not merged with mod contents.

With `[Diagnostics] TraceAudioBanks=1`, native dialogue opens log the requested path, resolved mod/cache path and requested language directory in `LiteralCrab.log` (successful dialogue logging is limited to 2048 opens per launch). `[Mods] Trace=1` also enables these messages and logs other indexed asset opens and attribute queries. Successful opens are quiet by default. Failed redirected opens are always logged, and native Win32 error codes are preserved. The selected audio directory follows the game's actual requests, including changes made through a language pack's menu.

Automated tests cover PLP/RUR/ARS paths in separate processes and across locale requests within one process, without native language directories or junctions; file precedence, disabled mods, packaged resources, cache reuse, damaged packages, custom character files and unchanged factory-language reads are also covered. They exercise quiet defaults, audio-only tracing and full file tracing while checking the same file delivery and Win32 results. A separate local-pack probe delivered all 184 banks and 184 name tables for each of PLP, RUR and ARS through the hooked Windows APIs, byte-for-byte identical to their source files. These file-access tests do not confirm audible playback in the game. An initial Russian gameplay trial still had no voices despite successful file opens. With `TraceAudioBanks=1`, mods supplying sound banks also enable native bank-loading results and the first 256 phrase lookups, preserving native behavior; their x86 ABI and rollback are tested against the supported executable.

Przy `[Mods] Enabled=1` pliki z katalogu `assets` aktywnego moda są dostępne zarówno dla loadera gry, jak i natywnych odczytów Windows, nawet gdy oryginalna ścieżka nie istnieje. Dotyczy to banków dialogowych, plików pomocniczych, zasobów postaci i innych rozszerzeń. Przy konflikcie ścieżki wygrywa wcześniejszy aktywny mod; unikalne pliki późniejszych modów pozostają dostępne. Wyłączone mody nie dostarczają plików. Zastępowane archiwa gry zachowują dotychczasową obsługę przez loader zawartości.

Pliki luźne są czytane z folderu moda. Pliki z paczek są przygotowywane w `mods/.cache` przed inicjalizacją gry/audio i używane ponownie, dopóki paczka się nie zmieni. Sprawdzanie atrybutów rozpoznaje katalogi zawierające zaindeksowane pliki. Junctiony i kopiowanie do oryginalnego `assets` nie są wymagane. Nowa postać nadal wymaga właściwych definicji gry i zgodnych formatów. Enumeracja katalogów Windows nie jest scalana z zawartością modów.

Przy `[Diagnostics] TraceAudioBanks=1` log zawiera żądaną ścieżkę dialogu, rozstrzygniętą ścieżkę moda/cache i katalog języka; limit udanych odczytów dialogów to 2048 na uruchomienie. `[Mods] Trace=1` również włącza te wpisy oraz pozostałe odczyty i sprawdzanie atrybutów. Domyślnie udane otwarcia nie są śledzone. Błędy przekierowanych otwarć są logowane zawsze, z zachowaniem kodu błędu Windows. Język wynika z faktycznego żądania gry, także po zmianie w menu paczki. Testy obejmują wyłączoną diagnostykę, samą diagnostykę audio i pełny trace plików przy identycznym dostarczaniu danych i wynikach Win32. Dodatkowy próbnik odczytał przez hooki Windows po 184 banki i 184 pliki nazw dla PLP, RUR i ARS, zgodnie ze źródłem co do bajtu, bez junctionów. Potwierdza to dostęp do plików; nie potwierdza słyszalnych dialogów w rozgrywce. Pierwsza próba rosyjskiego w grze nadal nie miała głosów mimo udanych otwarć plików. Przy `TraceAudioBanks=1` i modach dostarczających banki logowany jest także natywny wynik ładowania banku i pierwszych 256 wyszukiwań wypowiedzi, bez zmiany zachowania audio; ABI i rollback tej diagnostyki sprawdzono na obsługiwanym EXE.

## Character mods and the virtual portal / Mody postaci i virtual portal

For a folder layout, examples and authoring/test steps, start with the [modding guide](modding.md). PL: Układ folderów, przykłady oraz sposób przygotowania i testowania moda opisuje [poradnik modowania](modding.md).

### Automatic preparation / Automatyczne przygotowanie

`[Character] Source=<native actor or .dnax>` is enough for a playable custom character. At startup LiteralCrab evaluates the installed game's data chunks (actor lists, data maps, DNA, behavior trees) with a strict reader that accepts only the table-building Lua instructions every shipped data chunk uses, and reads the native tool table and BENT entities. It then resolves the source DNA, chooses a stock player template, keeps a player-form source DNA or grafts an NPC's model and identity onto the template's player DNA, replaces a non-player-controllable actor type, builds a private inventory when the source has no complete player tool profile, fills empty starting slots, borrows player controls for NPC-only weapons, infers gun clones and their projectile effects, clones actor/dependency rows, and generates a private animated entity from the template's animation sets when the source model lacks them. Everything stays private to the figure; explicit `[Character.Gameplay]` values still override each step. Standalone `[Tool]` mods likewise need only `Id`, `Source` and `Characters`. Lua overlays (`DNA=`) and scripts (`DNAScript=`) adjust the private DNA without authoring DNAX. Startup work only runs for enabled custom characters and tools.

The tool-profile problem reported for NPCs had several causes: native profiles are keyed by `ActorTypeMask`, not by actor row name; cut avatars such as Ferb also lack an actor row, tools, an actor type and an animated entity; and NPCs such as Butch use NPC DNA (Cowboy AI, BipedNPC tree) and models without the player animation sets. Each is handled generically. In a Toy Box trial, minimal Ferb and Butch manifests spawned from the virtual base with their own models, walking and jumping on the template's animation sets; Butch aimed a `Source`-only blaster. Combat, death/recovery and other play sets are not yet confirmed for them.

PL: `[Character] Source=<aktor lub .dnax>` wystarcza do grywalnej postaci. Przy starcie LiteralCrab odczytuje dane gry ścisłym czytnikiem fragmentów Lua zawierających tylko budowę tabel, a także tabelę narzędzi i encje BENT. Następnie ustala DNA źródła i szablon, zachowuje DNA gracza albo przenosi model i tożsamość NPC na DNA gracza z szablonu, zastępuje typ aktora spoza `PlayerControllable`, tworzy prywatny ekwipunek, uzupełnia sloty startowe, dodaje sterowanie broni NPC, wykrywa klon broni i efekty, klonuje wiersze aktora i zależności oraz generuje prywatną encję z zestawami animacji szablonu. Wszystko pozostaje prywatne; jawne wartości nadal mają pierwszeństwo. `[Tool]` wymaga tylko `Id`, `Source` i `Characters`. Nakładki Lua (`DNA=`) i skrypty (`DNAScript=`) zmieniają prywatne DNA bez edycji DNAX. Problem braku profilu narzędzi miał kilka przyczyn: profile są przypisane do `ActorTypeMask`, Ferbowi brakuje też wiersza aktora, narzędzi, typu i animowanej encji, a Butch ma DNA i model NPC bez zestawów animacji gracza. W próbie w Toy Boksie Ferb i Butch z minimalnych manifestów pojawili się z własnymi modelami, chodzili i skakali z animacjami szablonu; Butch celował blasterem z `Source`. Walki, śmierci i innych playsetów jeszcze nie potwierdzono.

**Rigs, authored characters and animations.** A source counts as a biped when its DNA uses the player `Biped`/`BipedNPC` tree or when its mesh has the stock biped skeleton (twelve named limb/head joints). Humanoid mission givers such as Tia Dalma, and authored models, convert automatically; cars, robots and animals do not. A character can come entirely from the mod's `AssetDirectory`: a text DNA (a UTF-8 byte-order mark is accepted), a native PC mesh and its materials and textures. A mesh without an entity is bound to the template's animation sets. An authored or shipped player entity with the template's playback channels is completed with the sets it lacks and with the shared interaction aliases of the template, as served with any enabled animation provider: bicycle, ship, rowboat, mounts, vehicles, boards and LiteralCrab's `LCRM_` poses. Its own mappings never change. `AnimationTemplate=` takes all animations from another stock figure than `Template`. Interactions the animation figure lacks come from the stock figure whose bind pose is nearest the character's rig, because these clips are not retargeted; `AnimationDonors=` overrides a group. Private character and tool rows are added to all eight play set actor lists and data maps. With `CharactersInAnyPlayset=1`, custom figures get the same hitching-post mounts and vehicles as stock visitors. The Zurg example has been confirmed in game for these points: spawn and blaster in play sets, vehicle summoning in Incredibles, megaphone scare, bicycle, ship and rowboat steering, and respawn. See the [modding guide](modding.md#characters-not-in-the-game--postacie-spoza-gry) for exact asset requirements.

PL: Źródło jest traktowane jako biped, gdy jego DNA używa drzewa `Biped`/`BipedNPC` albo gdy siatka ma standardowy szkielet bipeda (dwanaście nazwanych stawów kończyn i głowy). Dzięki temu automatycznie przechodzą humanoidalni zleceniodawcy (np. Tia Dalma) i modele autorskie, a samochody, roboty i zwierzęta nie. Postać może w całości pochodzić z `AssetDirectory`: tekstowe DNA (BOM UTF-8 jest akceptowany), natywna siatka PC, materiały i tekstury. Sama siatka dostaje zestawy animacji szablonu. Encja gracza z kanałami szablonu jest uzupełniana o brakujące zestawy i wspólne aliasy interakcji szablonu (rower, statek, łódka, wierzchowce, pojazdy, deski, pozy `LCRM_`), bez zmiany własnych mapowań. `AnimationTemplate=` bierze wszystkie animacje od innej postaci niż `Template`. Brakujące interakcje pochodzą od postaci z gry o najbliższej pozie spoczynkowej (klipy nie są retargetowane), a `AnimationDonors=` pozwala wskazać dawcę ręcznie. Prywatne wiersze postaci i narzędzi trafiają do list aktorów i map zależności wszystkich ośmiu playsetów. Z `CharactersInAnyPlayset=1` postacie własne mają te same słupki wierzchowców i pojazdów co postacie z gry. Na przykładzie Zurga potwierdzono w grze: spawn i blaster w playsetach, przywoływanie pojazdów w Iniemamocnych, okrzyk megafonem, rower, ster statku i łódki oraz respawn.

A mod can expose an existing DI1 avatar as a separate figure in the virtual portal. Put one `[Character]` section in its `mod.ini`; no Lua patch or rebuilt game archive is required. The loader registers the encrypted DI1 figure record, adds its portal row, supplies its label and portrait, and unlocks only that registered custom entry. The original native world-validity check remains active. The basic section uses an already defined avatar. The optional gameplay section below creates a private actor/DNA copy with a declared native loadout.

```ini
[Mod]
Name=Zurg avatar
Enabled=1

[Character]
SkuId=1000900
Name=LiteralCrab_Zurg
Gateway=AV_Zurg
Actor=av_zurg
BaseCharacter=AV_Buzz
DisplayName=Zurg
Description=The Zurg avatar already included in DI1.
Icon=ui/icons/npc_mg_zurg
Playset=RumpusRoom
```

Copy the mod folder into `mods` and restart. `[Mods] Enabled=1` is required. `user.ini` with `[Mod] Enabled=0` disables the whole character mod at the next start. Each mod supplies one character; enabled mods are combined in their normal folder order. Use a unique explicit `SkuId` in `1000100..1999999` and a unique `Name` and `Gateway`. `Name`, `Gateway`, `Actor`, `BaseCharacter` and `Playset` are ASCII identifiers; display fields currently accept printable ASCII. `Actor` must match `Gateway`, ignoring case. The basic form uses an existing avatar; `[Character.Gameplay]` can create a private alias from a source avatar. `BaseCharacter` is a stock gateway name used only for presentation defaults. The loader clears the base figure's purchase, trailer and bonus-item fields. Unknown or conflicting definitions are logged and left inactive.

`Icon` names an existing single-texture `ui/...` bundle. The loader creates private portal portrait bundles in memory and reads the original texture from the player's installation; the mod does not need to redistribute it. Explicit mod portrait files take precedence. The roster is bounded to 64 added figures. New figure saves, XP, weapons, every animation and cross-playset resource coverage need separate gameplay verification. Code tests cover original registry preservation, encrypted row positions, repeated registration, collisions, portal ownership, native validity gating and portrait delivery. They do not establish a successful in-game spawn.

Fallback portraits are fitted by their visible alpha bounds, preserving aspect ratio and adding the transparent margins expected by the portal. The normal and selected variants use separate 512×512 canvases; the small variant uses 256×256. Supported source textures are legacy DDS A8R8G8B8, DXT1, DXT3 and DXT5. Generated textures have private content addresses and leave the source icon unchanged. Fitting fixes framing; a small source image still limits sharpness. A mod can supply its own ready-made `ui/virtualreadercharacters/<Name>{,_s,_sm}.mtb` assets for full control of its portraits.

Stock gateway names and portal name keys are reserved. A custom entry becomes selectable only after its exact encrypted registry row has been accepted and the native registry recognizes its SKU as a figure. A rejected conflicting row cannot unlock a different character with the same SKU. If portrait preparation or the archive loader hooks fail, character definitions stay inactive.

The Zurg example has passed manual checks for a separate visible portal entry, an in-game Zurg spawn and the corrected portrait framing. The test uses only its mod folder and the loader features above. This confirms the initial loader path; it does not extend the coverage to weapons, progression or every playset.

Mod może udostępnić istniejącego awatara DI1 jako osobną figurkę w virtual portalu. Powyższa sekcja `[Character]` w `mod.ini` uruchamia funkcje LiteralCraba: wpis figurki, pozycję portalu, nazwę, portret i odblokowanie wyłącznie nowej zarejestrowanej postaci. Nie potrzeba skryptu Lua ani przebudowy archiwów. Skopiuj folder moda do `mods`, pozostaw `[Mods] Enabled=1` i uruchom grę ponownie. `user.ini` z `[Mod] Enabled=0` wyłącza mod przy następnym starcie.

Każdy mod deklaruje jedną postać. ID, `Name` i `Gateway` muszą być unikalne; zakres ID to `1000100..1999999`. `Actor` odpowiada `Gateway` bez rozróżniania wielkości liter i musi już istnieć jako awatar w danych gry. `BaseCharacter` dostarcza jedynie domyślne pola prezentacji, a pola zakupów, bonusów i zwiastuna są czyszczone. Nazwy techniczne są identyfikatorami ASCII, teksty wyświetlane obsługują obecnie drukowalne ASCII. `Icon` wskazuje istniejący zasób `ui/...`; loader przygotowuje portret w pamięci z zasobów własnej instalacji. Limit wynosi 64 dodatkowe figurki. Ten etap nie importuje modeli ani nie gwarantuje osobnego zapisu postępów, broni, wszystkich animacji czy działania w każdym playsecie. Wpis w portalu, wybór i rzeczywisty spawn wymagają testu w grze.

Nazwy gateway i klucze nazw portalu oryginalnych postaci są zarezerwowane. Nowy wpis można wybrać dopiero po przyjęciu dokładnego zaszyfrowanego wpisu i rozpoznaniu jego SKU jako figurki przez silnik. Konflikt ID nie odblokowuje innej postaci. Błąd przygotowania portretu lub instalacji hooków archiwów pozostawia definicje postaci nieaktywne.

Portret zastępczy jest dopasowywany do widocznych granic obrazu z zachowaniem proporcji i przezroczystych marginesów portalu. Warianty zwykły i zaznaczony mają osobne płótna 512×512, a mały 256×256. Obsługiwane tekstury źródłowe to DDS A8R8G8B8, DXT1, DXT3 i DXT5. Nowe tekstury nie zmieniają ikony źródłowej. Mały obraz nadal ogranicza ostrość. Autor moda może dostarczyć gotowe zasoby `ui/virtualreadercharacters/<Name>{,_s,_sm}.mtb`, aby samodzielnie ustalić wygląd portretów.

Przykład Zurga przeszedł ręczne testy osobnego widocznego wpisu w portalu, pojawienia się właściwej postaci w grze i poprawionego kadrowania portretu. Wykorzystuje tylko folder moda i opisane funkcje loadera. Potwierdza to działanie pierwszej ścieżki ładowania; bronie, postępy i wszystkie playsety pozostają poza zakresem tego testu.

### Native character loadouts / Natywne wyposażenie postaci (experimental)

To give the separate character its own starting equipment, use a private `Gateway`/`Actor` alias in `[Character]`, then add `[Character.Gameplay]` in the same manifest:

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

`SourceActor` must differ from the new actor. `SourceDNA` is optional when the actor row names it. `Item`, `Prop` and `Ability` set the native `AutoStartTool1`, `AutoStartTool2` and `AutoStartTool3` respectively; empty slots receive automatic defaults (see [Automatic preparation](#automatic-preparation--automatyczne-przygotowanie)). Each tool must be in the correct `Items`, `Props` or `Abilities` group; a declared tool missing from the profiles joins the private inventory. This reads the effective mod-provided tool table before falling back to the installed original, preserving companion mods. Invalid gameplay definitions are rejected before portal registration.

Optional `InventorySource` adds another native avatar's inventory to the source profile, grouped as Items, Props and Abilities with duplicates removed. `InventoryExclude` removes comma-separated tool IDs from the merged inventory; native `ITEM_NONE`, `PROP_NONE` and `ABILITY_NONE` entries cannot be excluded. The loader supplies a private native profile only for the DNA marked as this custom figure. Row positions and ID hashes are checked against the live table; original avatars, shared tool rows and shared profiles remain unchanged. The native per-actor unlock, suppression and world rules still decide which of these tools can be selected and used. This does not grant every locked item or enable cross-playset use by itself.

For Zurg, `InventorySource=AV_Buzz` adds the ordinary player equipment groups. Excluding `ITEM_BUZZ,ABILITY_BUZZ` avoids borrowing Buzz's innate laser and jetpack. `Prop=PROP_BUZZ` supplies his body-only shoulder-charge/ground-pound controls; `ABILITY_BUZZ` is the jetpack, not the melee attack. Zurg already has the shared shoulder-charge aliases and Avatar behavior tree. `PreloadIncludes=biped_avatar` adds common hit/charge effects to the private dependency row. The local test also supplies a private native Zurg model bundle with seven added `Playset:ToyBox` aliases for the wand, toolbox and creation animation, preserving all original model entries. A compatible model/animation bundle is still required; an inventory template cannot create missing animations. Standard attack, tool switching and the replacement portal artwork passed a manual trial. A separate manual trial confirms hoverboard alignment after the private model correction described below; other tool orientations are not all validated.

PL: `InventorySource` dodaje natywny profil ekwipunku innego awatara, zachowując grupy i usuwając duplikaty. `InventoryExclude` pomija wskazane ID narzędzi; wpisy NONE muszą pozostać. Prywatny profil działa wyłącznie na DNA oznaczonym jako ta customowa postać. Natywne odblokowania, blokady i reguły świata nadal obowiązują; nie jest to automatyczne odblokowanie wszystkich narzędzi ani używania ich w każdym playsecie. Przykład Zurga korzysta z profilu Buzza bez jego lasera i jetpacka. `PROP_BUZZ` włącza szarżę barkiem i ground pound, a `ABILITY_BUZZ` oznacza jetpack. `PreloadIncludes=biped_avatar` dodaje wspólne efekty ataków. Lokalny pakiet modelu dopisuje siedem brakujących aliasów Toy Boksa, zachowując oryginalne dane Zurga. Sam profil nie dostarcza brakujących animacji. Standardowy atak, zmiana sprzętu i nowa grafika portalu przeszły test ręczny. Osobna próba ręczna potwierdziła ustawienie hoverboardu po opisanej niżej korekcie prywatnego modelu; nie sprawdzono orientacji wszystkich narzędzi.

Socket names alone do not guarantee matching attachment frames across rigs. Zurg's `Cntrl_FootCenter_Right` inherits its left foot, whereas Buzz's inherits the right foot; native forward-hover samples show about 40 degrees of difference. The local test model adds a `+40` degree yaw through `DataNode333.Data.Orientation` on its existing right-foot-center group. This is one rest-frame quaternion in the mod's private OCT file; animation inputs, skin, graph tables and shared prop scripts stay intact. A manual gameplay trial confirms the corrected hoverboard alignment. It does not establish correct alignment of other sockets or every tool.

PL: Ta sama nazwa mocowania nie gwarantuje tej samej orientacji w różnych rigach. `Cntrl_FootCenter_Right` Zurga dziedziczy obrót lewej stopy, a Buzza prawej; pomiary jazdy wskazują około 40 stopni różnicy. Lokalny model testowy dodaje yaw `+40` przez `DataNode333.Data.Orientation` istniejącej grupy mocowania. Zmiana jednego kwaternionu w prywatnym OCT zachowuje wejścia animacji, skórę, tabele grafu i wspólne skrypty deski. Próba w grze potwierdziła poprawne ustawienie hoverboardu. Nie potwierdza poprawności innych mocowań ani wszystkich narzędzi.

Private gameplay DNA also defaults an existing `AgentGadgets.DynamicExploder.dynexp_reassemble` to numeric `1` when that field is absent. The shared avatar death behavior waits for the native reassembly event before restoring health, visibility and tool controls. An NPC-derived source such as Zurg omits this setting and can otherwise disappear permanently after combat death. The native engine supports reassembly with zero break-apart pieces; the loader does not borrow another avatar's pieces. Explicit authored values, original NPC/boss DNA and fall/water respawn rules remain unchanged. Combat respawn and weapon recovery passed a manual trial, with an excessive recovery delay reported afterward.

For a zero-piece private avatar using the native `characters/avatar.btx` tree, the loader prepares a private tagged tree and changes only the recognized `Death/PrepareForDeath` sleep from `0.1` to `0.001` seconds. A zero-piece exploder can otherwise emit its reassembly event before the stock preparation finishes and wait for the ten-second fallback. Original/shared trees and explicit custom timing definitions are preserved. A manual combat-death trial confirms the shorter recovery; other death/recovery animations and health/weapon cleanup remain native.

PL: Prywatne DNA otrzymuje domyślne `dynexp_reassemble=1`, jeśli istnieje komponent `DynamicExploder`, ale brakuje tego pola. Wspólna procedura śmierci czeka na zdarzenie złożenia, aby przywrócić zdrowie, widoczność i sterowanie. Źródłowe DNA Zurga pomija tę flagę. Silnik obsługuje również składanie bez modelu rozbitych części. Loader zachowuje jawne wartości autora i oryginalne DNA NPC/bossa; nie zmienia reguł upadku ani wody. Respawn i powrót broni potwierdzono ręcznie, ale zgłoszono długie oczekiwanie.

PL: Dla prywatnej postaci bez rozbitych części, używającej natywnego drzewa `characters/avatar.btx`, loader tworzy prywatną kopię i skraca rozpoznany etap `Death/PrepareForDeath` z `0.1` do `0.001` sekundy. Sygnał powrotu może inaczej nadejść przed rozpoczęciem nasłuchiwania, powodując oczekiwanie na dziesięciosekundowy limit. Pozostałe animacje, przywracanie zdrowia i broni oraz oryginalne drzewa pozostają natywne. Próba śmierci w walce potwierdziła krótsze odradzanie.

Optional `ItemControlSource` and `PropControlSource` borrow the native targeting/behavior, control-context and HUD-hint lists from another tool in the corresponding group. They change only the private avatar's declared slot. For example, the shipped Zurg boss blaster has no player control/targeting lists; `ItemControlSource=ITEM_BLASTER` supplies those while retaining Zurg's own tool actor, projectile script, equipment animations and inventory. Source row positions are resolved from the effective table at startup and checked against native tool IDs before use. This does not copy a different character's complete DNA or special powers.

Optional `ItemDisplayName`, `PropDisplayName` and `AbilityDisplayName` supply printable ASCII labels for the corresponding native tool IDs. These are shared localization keys: if multiple enabled figures name the same tool, the first mod's label wins. Tool icons still use the native `ui/icons/<ToolID>.mtb` asset path, which a mod can provide through ordinary asset overrides.

`ItemWeaponSource` creates a private copy of an existing native `gun` weapon and its `widget` projectile. `ItemToolDNA` and `ItemToolScript` identify the source tool's DNA and native SLNG script; all three are inferred from the item's tool actor when any `ItemProjectile...` value is set. The item must have exactly one actor descriptor. The loader clones that tool actor, DNA and resource dependencies; a private copy of its script refers to the new weapon ID. Native VM instructions remain unchanged, and the original tool, weapon and projectile definitions retain their values. Source files can be read from the installation or from the mod's own `AssetDirectory`.

Optional projectile overrides are `ItemProjectileRadius` (collision radius, `0.001..10`), `ItemProjectileScale` (visual scale, `0.001..10`), `ItemProjectileSpeed` (forward launch speed, `0.1..1000`), `ItemProjectileLifetime` (`0.01..120`) and `ItemProjectileDamage` (`0..10000`). Omitted values are inherited from the source. These currently apply to the item weapon slot. `AimOffset=x,y,z` overrides the private avatar's normal targeting-camera offset, with each component in `-10..10`. These parameters are separate: reducing collision radius does not reduce the visual effect. The Zurg example's native boss projectile uses radius `5`, so it needs player-sized collision and visual settings. Its accepted visual scale is `0.1`; authors can tune it in the manifest and restart the game. For example, `0.075` is smaller, while `0.25` is larger. This does not change `ItemProjectileRadius`.

The loader generates private DNA in memory, clones the source `Avatar` record, and clones its resource dependency row where the source is defined. It adds comma-separated `PreloadActors`, `PreloadFiles` and `PreloadIncludes` lists to that private row. `PreloadIncludes` names native Include entries without a `|Include` suffix. File types supported in `PreloadFiles` are `DNA`, `Script`, `Entity`, `Cho`, `Audio`, `AnimTreeProject` and `BehaviorMap`, followed by `|` and the logical resource path. Each list accepts at most 64 distinct entries. This stage handles the core, Toy Box and Toy Story actor/dependency tables; availability in other playsets requires their matching actor/resource integration and gameplay tests.

PL: `PreloadIncludes` podaje nazwy natywnych wpisów Include, rozdzielone przecinkami, bez końcówki `|Include`. Zależności trafiają tylko do prywatnej kopii wpisu postaci; oryginał pozostaje zachowany.

Declared `Item` and `Prop` slots become selectable on the marked private avatar instance, including equipment originally hidden from the player menu. Shared tool definitions and original NPC DNA stay unchanged. Native unlock, suppression, group and inventory checks still apply; the hidden `Ability` group keeps its native visibility. Loadout configuration does not grant saved XP or independent progression. The Zurg trial has confirmed the portal entry, private actor spawn, portrait framing, selectable blaster, aiming, forward projectile, camera framing and an acceptable projectile size at scale `0.1`. Damage values have not been independently measured. Full playset coverage and custom attack-animation integration remain separate work. All 29 automated tests pass.

PL: Sekcja `[Character.Gameplay]` tworzy prywatną kopię aktora i DNA, aby konfiguracja wyposażenia nie zmieniała oryginalnego NPC ani bossa. Nowe `Gateway` i `Actor` muszą być zgodne ze sobą i różne od `SourceActor`. `SourceDNA` jest opcjonalne, gdy wskazuje je wiersz aktora. Pola `Item`, `Prop` i `Ability` ustawiają trzy natywne sloty startowe; puste sloty otrzymują wartości automatyczne. Narzędzia muszą należeć do właściwej grupy; zadeklarowane narzędzie spoza profili trafia do prywatnego ekwipunku. Loader uwzględnia tabelę narzędzi dostarczoną przez inny mod.

PL: `PreloadActors` oraz `PreloadFiles` są listami rozdzielonymi przecinkami, do 64 różnych wpisów każda. Zależności trafiają do prywatnego wpisu zasobów. Obsługiwane typy plików to `DNA`, `Script`, `Entity`, `Cho`, `Audio`, `AnimTreeProject` i `BehaviorMap`; po `|` podaje się logiczną ścieżkę zasobu. Ten etap obsługuje tabele core, Toy Boksa i Toy Story. Pozostałe playsety wymagają integracji odpowiednich aktorów i zasobów oraz testów w grze. Zadeklarowane sloty `Item` i `Prop` mogą pojawić się w menu gracza również wtedy, gdy oryginalny wpis był ukryty. Zmiana dotyczy wyłącznie oznaczonej prywatnej instancji; natywne blokady i reguły użycia pozostają aktywne. Próba Zurga potwierdziła wpis w portalu, spawn prywatnego aktora, kadrowanie portretu, wybór blastera, celowanie, lot pocisku, kamerę i akceptowalny rozmiar pocisku przy skali `0.1`. Wartości obrażeń nie zmierzono osobno. Pełna zgodność playsetów i powiązanie własnych animacji ataku pozostają osobnym etapem. Wszystkie 29 testów automatycznych przechodzi.

PL: Opcjonalne `ItemControlSource` i `PropControlSource` pobierają listy celowania, zachowań, kontekstów sterowania i podpowiedzi HUD z innego narzędzia tej samej grupy. Dotyczy to wyłącznie zadeklarowanego slotu prywatnego aktora. `ItemControlSource=ITEM_BLASTER` uzupełnia brakujące sterowanie oryginalnego blastera Zurga, zachowując jego aktora broni, skrypt pocisku, animacje wyposażenia i ekwipunek. Nazwy menu można podać przez `ItemDisplayName`, `PropDisplayName` i `AbilityDisplayName`. Klucze nazw należą do natywnych ID narzędzi; przy konflikcie pierwszeństwo ma pierwszy włączony mod. Ikonę można dostarczyć jako zwykły zasób moda `ui/icons/<ToolID>.mtb`.

PL: `ItemWeaponSource` tworzy prywatną kopię natywnej broni klasy `gun` i jej pocisku klasy `widget`. Wymaga pól `ItemToolDNA` i `ItemToolScript` oraz narzędzia z dokładnie jednym aktorem. Loader przygotowuje prywatnego aktora broni, DNA, zależności i kopię skryptu wskazującą nowy identyfikator broni. Oryginalne instrukcje VM oraz definicje bossa pozostają zachowane. Parametry `ItemProjectileRadius`, `ItemProjectileScale`, `ItemProjectileSpeed`, `ItemProjectileLifetime` i `ItemProjectileDamage` zmieniają odpowiednio kolizję, skalę graficzną, prędkość, czas życia i obrażenia prywatnego pocisku. Pominięte wartości są dziedziczone. `AimOffset=x,y,z` ustawia normalny offset kamery celowania nowej postaci. Kolizja i skala graficzna są niezależnymi parametrami. Zaakceptowany przykład Zurga używa skali `0.1`; autor może ją zmienić w manifeście i uruchomić grę ponownie. Wartość `0.075` zmniejszy pocisk, a `0.25` go powiększy względem `0.1`, bez zmiany `ItemProjectileRadius`.

### Private assets in a character mod / Prywatne zasoby postaci

Add `AssetDirectory=character-assets` to `[Character]` to load a private resource folder belonging to that mod. It is relative to the enabled mod's own directory. Keep the logical resource layout inside it, for example:

```text
mods/my_zurg/mod.ini
mods/my_zurg/character-assets/characters/ts_zurg.dnax
mods/my_zurg/character-assets/characters/zurgbossblaster.dnax
mods/my_zurg/character-assets/characters/zurgbossblaster.sx
mods/my_zurg/character-assets/characters/my_model/my_model.bent
mods/my_zurg/character-assets/ui/icons/my_zurg.mtb
mods/my_zurg/character-assets/textures/0123456789abcdef.tbody
```

Keep `SourceDNA`, `ItemToolDNA`, `ItemToolScript`, `Icon` and typed `PreloadFiles` as logical resource paths. A file in the character's own folder takes precedence for that character. For example, the local `characters/ts_zurg.dnax` supplies the private avatar clone, while the original `av_zurg` actor and its `TS_Zurg.dnax` stay unchanged. Other missing resources continue to use their existing game/mod definitions. `SourceDNA` still identifies the source actor's DNA path in the native actor/dependency tables. Private gameplay `Actor`/`Gateway` names must start with `LCR_`; the loader also rejects collisions with generated item actors.

The loader mounts these files under `characters/literalcrab/assets/<SkuId>/...`. The source names are aliases only within that character; they are never published as global replacements. Two mods can therefore supply different files at the same relative path. Private DNA resource strings, cloned dependency rows and native SLNG resource literals are redirected to that character's aliases. Bare identity/type/state strings are preserved. Source and compiled Lua 5.1 DNA are supported. TEXB v3 texture bundles are rebound to separate per-character texture hashes, preserving the original texture cache entries. This includes named icon tables and nameless model tables inside multi-chunk BNDL v1 containers; other chunks such as MATP retain their bytes and offsets. Unchanged native resources also support disk opens redirected to the mod's own file. Restart after edits.

PL: Obsługiwane są też tekstury modeli z bezimienną tabelą TEXB v3 w kontenerze BNDL v1 zawierającym dodatkowe chunki, np. MATP. Loader zmienia hashe tekstur, zachowując bajty i offsety pozostałych chunków.

The folder must exist and contain files. Absolute/traversal paths, junctions/symlinks, ambiguous paths or hashes, and already-claimed private resource names reject the figure before portal registration. Each folder accepts at most 4096 files and 512 MiB. Files are loose native resources; placing a ZIP here does not unpack it. The ordinary `mods/<mod>/assets` folder retains its global replacement behavior, so use `AssetDirectory` for character-specific changes.

This preserves resource ownership; it does not convert models, rigs or platform formats. Internal binary references in BENT, OCT, animation or other opaque formats are not automatically rewritten. Supply a coherent native bundle, preserving its relative dependencies and using compatible materials/rigs. The shipped Zurg model, for example, refers to shared `../biped/motions/...` resources. Audio banks still need the engine's native bank paths and naming conventions.

The Zurg example has passed a manual restart/gameplay check with its DNA and native blaster script loaded from its own `AssetDirectory`. All 29 automated tests pass. The shipped boss encounter loads `av_zurg` and `tool_zurgbossblaster`; it shares the source `TS_Zurg.dnax` with this example. Regression replay checks all original records in eight actor lists, three dependency maps including the boss zone, and all original weapon/projectile definitions. Private actor, dependency or weapon-name conflicts disable the affected custom figure in the portal and remain blocked until restart. Native actor type groups are intentionally inherited for tool compatibility; the boss encounter has not yet been gameplay-tested with the custom player present.

PL: Dodaj `AssetDirectory=character-assets` w `[Character]`. Folder jest względny wobec katalogu włączonego moda. Zachowaj w nim logiczne ścieżki zasobów, jak w przykładzie powyżej. `SourceDNA`, `ItemToolDNA`, `ItemToolScript`, `Icon` i `PreloadFiles` nadal wskazują nazwy zasobów gry. Lokalny plik ma pierwszeństwo wyłącznie dla tej postaci. Na przykład lokalny `characters/ts_zurg.dnax` dostarcza DNA prywatnego awatara, zachowując oryginalne DNA i aktora `av_zurg`. Brakujące zasoby korzystają z dotychczasowych definicji gry/modów. `SourceDNA` musi odpowiadać ścieżce źródłowego aktora w natywnych tabelach. Prywatne nazwy `Actor`/`Gateway` zaczynają się od `LCR_`; sprawdzane są też konflikty z generowanymi aktorami broni.

PL: Zasoby trafiają pod prywatne ścieżki `characters/literalcrab/assets/<SkuId>/...`. Dwa mody mogą mieć różne pliki o tych samych nazwach względnych. Loader przekierowuje odwołania do zasobów w prywatnym DNA, sklonowanych zależnościach i literałach natywnego SLNG, zachowując same nazwy tożsamości, typów i stanów. Obsługuje tekstowe i skompilowane DNA Lua 5.1. Paczki tekstur TEXB v3 otrzymują osobne hashe dla postaci, bez nadpisania cache oryginału. Niezmienione zasoby mogą być też otwierane z dysku przez przekierowanie do własnego pliku moda. Po edycji uruchom grę ponownie.

PL: Brak lub pusty folder, ścieżki wychodzące poza niego, dowiązania/junctions, niejednoznaczne nazwy lub hashe i zajęte prywatne ścieżki odrzucają postać przed rejestracją w portalu. Limit folderu to 4096 plików i 512 MiB. Używaj luźnych zasobów; ZIP nie jest tu rozpakowywany. Zwykły folder `assets` nadal służy do globalnych podmian. Formatów modeli, szkieletów i platform loader nie konwertuje. Wewnętrzne binarne odwołania BENT, OCT i animacji wymagają spójnego pakietu z zachowanymi zależnościami względnymi; nie są automatycznie przepisywane. Banki audio nadal wymagają natywnych ścieżek i konwencji nazw.

PL: Przykład Zurga przeszedł ręczny test po restarcie z DNA i natywnym skryptem blastera ładowanymi z własnego `AssetDirectory`. Wszystkie 29 testów automatycznych przechodzi. Boss korzysta z `av_zurg`, `tool_zurgbossblaster` i tego samego źródłowego `TS_Zurg.dnax`. Replay porównuje wszystkie oryginalne rekordy ośmiu list aktorów, trzech map zależności wraz ze strefą bossa oraz tabel broni i pocisków. Konflikt prywatnej nazwy aktora, zależności albo broni blokuje daną postać w portalu do restartu. Natywne grupy typu aktora pozostają dziedziczone dla zgodności wyposażenia; walki z bossem z własną postacią jako graczem jeszcze nie sprawdzono w grze.

### Standalone custom tools / Osobne customowe narzędzia

Standalone `[Tool]` mods can register separate native tool IDs and supply private models, scripts, icons and gun/projectile settings for named custom characters. They inherit a native tool's controls and rules, preserve stock inventory profiles and optionally equip the new tool at startup without editing the character mod. By default the tool belongs to its characters like their own `ITEM_<name>`: it needs no purchase or upgrade and is created in every play set. `BaseRules=1` keeps the base tool's upgrade (for example `UP_Blaster`) and Toy Box world list. Without this default, a copied Toy Box blaster stayed locked in play sets: it was selected but its actor was never created. `Source=<native tool>` lets the loader infer the base tool, tool actor, DNA, script and gun. See the [custom tool authoring guide](modding.md). The local TS3 Zurg gun conversion (rigid mesh) has passed loading, aiming, firing and grip checks in game.

PL: Osobny mod `[Tool]` rejestruje nowe ID narzędzia i prywatne modele, skrypty, ikony oraz parametry broni dla wskazanych customowych postaci. Zachowuje reguły natywnej bazy i oryginalne profile; może ustawić wyposażenie początkowe bez edycji moda postaci. `Source=<narzędzie>` pozwala wyznaczyć bazę, aktora, DNA, skrypt i broń automatycznie. Konfigurację opisuje [poradnik narzędzi](modding.md). Lokalny port broni Zurga z TS3 (nieruchoma siatka) przeszedł w grze próbę ładowania, celowania, strzału i chwytu.

### Gameplay audio / Dźwięki w grze

Custom characters and tools can optionally declare gameplay effects and short
barks using loose WAV/Ogg Vorbis clips, without authoring sound banks. The loader
uses native effects/dialog mixer groups, variants and cooldowns, and excludes
cutscenes. Clips are 3D by default and follow the player in the game's own world
space: the PC and Wii U builds pass raw world coordinates to a right-handed FMOD
listener at the camera. Distances default to the shipped events' values (effects
from 5 units, voice from 60, inverse rolloff). `Spatial=0` keeps a clip local.
See [gameplay audio configuration and testing scope](modding.md#gameplay-audio-for-custom-characters-and-tools--dźwięki-customowych-postaci-i-narzędzi).

PL: Customowe postacie i narzędzia mogą korzystać z luźnych plików WAV/Ogg Vorbis
dla efektów i okazjonalnych kwestii, bez tworzenia banków. Loader obsługuje
natywne kanały głośności, warianty i odstępy; cutscenki pozostają zachowane.
Domyślnie dźwięk jest przestrzenny (3D) w pozycji gracza, z zasięgami jak w
natywnych zdarzeniach gry; `Spatial=0` daje dźwięk lokalny.

### Avatar motion adapters / Adaptery animacji awatara

Lua patches can check `LiteralCrab.has(name)` before using these capabilities:

| Function | Result |
| --- | --- |
| `LiteralCrab.PlayAvatarMotion(player, motion)` | Starts one named motion on the native `Misc_OverRide` channel; returns native success. |
| `LiteralCrab.StopAvatarMotion(player, motion)` | Requests stopping that named override; returns whether the native call completed. |
| `LiteralCrab.AvatarMotionEnded(player)` | Returns native completion of the override channel. |
| `LiteralCrab.GetAvatarSocketOrientation(player, socket)` | Reads the native socket quaternion as four numbers `x,y,z,w`; returns no values when unavailable. |

`player` is a zero-based currently present local player index. Motion names accept 1–127 ASCII letters, digits or underscores and must be mapped on that avatar's loaded model. Calls resolve the current actor each time and return `false` for invalid arguments, missing actors or native failure. They expose animation playback only: projectile timing, tool actions, collision, damage and animation-event dispatch need their own gameplay integration. The channel is shared with other native override motions; coordinate interruptions in the mod. Profile and calling-convention tests pass; the adapter profile has been accepted by the supported game at startup.

PL: Skrypty mogą uruchamiać, zatrzymywać i sprawdzać koniec nazwanej animacji bieżącego lokalnego gracza przez trzy funkcje powyżej. Najpierw należy sprawdzić `LiteralCrab.has`. Indeks gracza zaczyna się od zera, a nazwa animacji zawiera 1–127 liter ASCII, cyfr lub znaków `_` i musi być zmapowana w załadowanym modelu. Nieprawidłowe argumenty, brak postaci lub błąd silnika zwracają `false`. Funkcje obsługują odtwarzanie na współdzielonym kanale `Misc_OverRide`; powiązanie animacji z pociskiem, działaniem narzędzia i obrażeniami pozostaje zadaniem integracji rozgrywki.

`GetAvatarSocketOrientation` follows the native `AnimGetNodeOrientation` convention and performs no orientation change. It reads the node's local rotation without composing its parent chain; an identity quaternion does not mean the attachment faces forward in world space. It resolves the current avatar, obtains a native node reference, reads a finite quaternion and releases that reference. Socket names use the same 1–127 ASCII identifier rule, for example `Cntrl_FootCenter_Right`. Capability checks are independent of motion playback; missing profile support withholds only this query. Compare sockets and their parent frames in the same pose.

PL: `GetAvatarSocketOrientation` odczytuje lokalny kwaternion kontrolki zgodnie z natywnym `AnimGetNodeOrientation`, bez obracania postaci lub sprzętu i bez składania obrotów rodziców. Kwaternion jednostkowy nie oznacza ustawienia na wprost w świecie gry. Zwraca cztery liczby albo brak wyników, jeśli kontrolka, gracz lub obsługa profilu są niedostępne. Nazwa ma 1–127 liter ASCII, cyfr lub `_`, np. `Cntrl_FootCenter_Right`. Funkcja zwalnia pobraną referencję kontrolki. Porównuj kontrolki i ich rodziców w tej samej pozie.

## Round disc ownership API

Script mods can query `LiteralCrab.GetDiscOwner(disc)` to reconstruct round-disc ownership after Lua menu state is recreated. It returns player `0`/`1`, `-1` for an absent valid disc, and `-2` for invalid/unknown/error states. This reads the native placed tag and leaves tags, avatar effects and saves unchanged. Its additional layout checks can withhold this function without disabling the existing disc adapters. Mods must opt into the API; the function alone does not restore their cards or refresh effects. See the [API contract](modding.md#engine-adapters-literalcrab).

The 16 automated tests pass, including ownership arguments, both player antennas, absent/stale/unreadable records, native failures, read-only behavior and independent profile validation. The ownership test also passes against a private mapping of the supported original executable. These checks do not replace the mod's gameplay test across frontend/world transitions.

PL: `LiteralCrab.GetDiscOwner(disc)` pozwala modom odbudować właścicieli okrągłych dysków po odtworzeniu menu Lua. Wyniki: gracz `0`/`1`, brak prawidłowego dysku `-1`, nieznany/błędny stan `-2`. Funkcja odczytuje natywny tag bez zmian tagów, efektów avatara i zapisu. Niezgodność dodatkowych sygnatur wyłącza tylko nową funkcję. Mod musi wykorzystać API do odbudowania kart; samo jego udostępnienie nie zmienia menu ani odświeżania efektów. Przeszło 16 testów oraz test adaptera na prywatnym mapowaniu oryginalnego EXE. Próba w grze po przejściu frontend → świat pozostaje odbiorem integracji po stronie moda.
