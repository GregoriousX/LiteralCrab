# LiteralCrab configuration guide

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

[ToyBox]
LimitScale=1

[Gameplay]
CharactersInAnyPlayset=0

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
| `[Startup]` `LanguageSwitcher` | `off` | Selects a startup language, e.g. `polish`, `italian` or `EnglishPAL`, and preserves it through Steam language detection. Recognizes all 18 PS3 locale IDs and regional aliases. `off` keeps native behavior. Additional languages require their installed PC-compatible assets and locale configuration; missing/unsupported choices retain the native language. Replaces the language pack's separate `PS3Languages.asi`. See [language list and setup](LANGUAGE_SWITCHER.en.md). |
| `[PostProcessing]` `MotionBlur` | `1` | `0` removes the motion blur seen when the camera turns quickly, in every world. `1` keeps each world's own setting. |
| `[PostProcessing]` `DepthOfField` | `1` | `0` removes the distance blur that some worlds and Toy Box skies use. |
| `[PostProcessing]` `Bloom` | `1` | `0` removes the glow around bright lights, the sky and shiny surfaces. |
| `[Camera]` `ReturnDelay` | `default` | Seconds the camera waits after you turn it before it swings back behind the character, for every camera: `0` returns at once, `5` waits five seconds (up to `3600`). `default` keeps each camera's own timing. |
| `[Controller]` `SwapXY` | `0` | `1` exchanges the X and Y buttons of Xbox-compatible (XInput) controllers everywhere in the game. On-screen prompts keep the original letters. |
| `[ToyBox]` `LimitScale` | `1` | Multiplies the Toy Box editor's limits (its "memory" meter): object costs, measured memory and frame time. `4` is generous, `10` very high, up to `100`. Very large Toy Boxes can still slow down or run out of memory. Needs `[Mods] Enabled=1`. |
| `[Gameplay]` `CharactersInAnyPlayset` | `0` | `1` lets every character play in every play set. Visitors use their base actor, all avatar dependency maps are loaded, and tool/motion effects are enabled. Player tool selection and unlocked, unsuppressed personal equipment (such as Buzz's innate Pixar Ball) can cross playset zone restrictions. Shared Toy Box/shop equipment retains its native world filters, even after purchase. Native membership lists, character swaps, unlocks and Cars race rules are preserved. Changes are in memory only. Needs `[Mods] Enabled=1`. See the [repair notes and testing status](CHARACTER_FREEDOM_FIX.en.md). |
| `[ToyBoxes]` `Enabled` | `0` | `1` restores **Toy Box Share** (Toy Box menu > Toy Box Share), the PC release's menu that Gold hides, with the Toy Boxes from the `toyboxes` folder beside the ASI. See [Toy Box Share](#toy-box-share). The menu entry needs `[Mods] Enabled=1`. |
| `[ToyBoxes]` `Servers` | empty | **Experimental, not yet tested at a large scale; use at your own risk.** Web servers to mirror Toy Box packages from, separated by spaces: `http://` or `https://` folder URLs; their Toy Boxes are listed under "Disney's Toy Boxes". See [Toy Box Share](#toy-box-share). |
| `[ToyBoxes]` `MaxToyBoxes` | `5000` | Most Toy Boxes listed, from `1` to `20000`; packages beyond the limit are not read. |
| `[ToyBoxes]` `ServerMaxPackageMB` | `32` | Largest package a server may send, in MB (`1` to `1024`). A hundred Toy Boxes take about 5 MB. Larger packages are refused before they are downloaded, or cut off as soon as they exceed it. |
| `[ToyBoxes]` `ServerMirrorMB` | `1024` | Most disk space for the packages of all servers together, in MB (`16` to `65536`). |
| `[Mods]` `Enabled` | `1` | Loads script and asset mods from the `mods` folder beside the ASI. With no mods installed nothing is hooked. `0` leaves every game script and file untouched. See [MODDING.md](MODDING.md). |
| `[Mods]` `Trace` | `0` | For mod authors. `1` logs the name of every Lua chunk the game compiles and every file its loader requests, which are the names a mod targets. Slows loading; leave `0` for play. |

## Character tools across playsets / Narzędzia postaci w playsetach

The tool exception follows the game's per-avatar equipment data. It preserves personal equipment and dependent abilities; buying a shared toy does not make it available in every playset. For example, the Toy Box Blaster stays restricted in Lone Ranger, while Buzz's innate Pixar Ball can work in Incredibles. Shared equipment remains available wherever the original game allows it, including Toy Box and its own playset. Suppression and progression checks still apply. The regression tests cover both blaster variants, shared equipment without an upgrade prerequisite, per-avatar Pixar Ball eligibility, and the early inventory refresh path. Gameplay confirmation of this restriction fix is pending.

Wyjątek korzysta z danych wyposażenia przypisanych przez grę do danej postaci. Zachowuje osobiste narzędzia i zależne zdolności; kupienie wspólnej zabawki nie udostępnia jej we wszystkich playsetach. Miotacz z Placu Zabaw pozostaje ograniczony w Lone Ranger, a własna piłka Buzza może działać w Iniemamocnych. Wspólne wyposażenie pozostaje dostępne tam, gdzie pozwala na to oryginalna gra, w tym w Toy Box i swoim playsecie. Nadal obowiązują blokady scenariuszowe i warunki postępu. Testy regresji obejmują oba miotacze, wspólne wyposażenie bez wymaganego ulepszenia, dostępność piłki zależną od postaci oraz odświeżanie ekwipunku przy starcie. Potwierdzenie tej poprawki w rozgrywce pozostaje do wykonania.

With `CharactersInAnyPlayset=1`, registered rider avatars also receive the native hitching-post definitions for Lone Ranger, Toy Story, Incredibles and Monsters University. Existing mount preferences, custom typed entries and native purchase/progression locks are preserved. Unknown custom fallback lists are left unchanged. Car avatars keep their vehicle behavior. Cars visitors receive the original shared engine/body sound banks and Cars dialogue rules, and each supported playset permits the native nitro meter; the engine still checks the controlled car and local player. Language selection and per-character voice loading remain native.

Ordinary Lone Ranger pistol/tomahawk targets additionally accept supported handheld projectile types, including Pixar/baseballs, Buzz's laser, Perry's hat, Mickey's spell, frying pans, frozen projectiles, Violet's shields and cherry projectiles. The native target structure holds only eight weapon tags; the previous patch exceeded that capacity. The replacement adds one bounded marker and checks the additional types at a verified native impact gate, preserving the original message, scoring, rewards and impact handling. Full or malformed custom lists are left unchanged. If the native signatures differ, the target patch is withheld. Cannon and Gatling targets retain their original requirements. Shared Toy Box equipment remains subject to its world restrictions. Tests cover the actual x86 gate, register/flag and x87/SSE preservation, read-only impact data, original and additional projectile types, bounded/idempotent data patches and special-target exclusions. Gameplay feedback confirms visitor horse summoning and Cars sound, dialogue and nitro HUD in the checked session. Later target QA supersedes the earlier provisional acceptance; this replacement still requires gameplay verification. Full character/playset coverage has not been established.

Przy `CharactersInAnyPlayset=1` zarejestrowane postacie mogące jeździć na wierzchowcach otrzymują natywne definicje punktów przywołania z Lone Rangera, Toy Story, Iniemamocnych i Uniwersytetu Potwornego. Zachowane są preferencje wierzchowców, własne wpisy innych modów oraz blokady zakupu i postępu. Nieznane własne listy domyślne pozostają bez zmian. Postacie z Aut zachowują sterowanie pojazdem. Dla Aut odwiedzających inne playsety wczytywane są oryginalne wspólne banki silnika/nadwozia i reguły dialogów; dostępny jest też natywny wskaźnik nitro z kontrolą lokalnego gracza i prowadzonego auta. Język i ładowanie głosów postaci nadal obsługuje gra.

Zwykłe tarcze Lone Rangera na pistolet/tomahawk obsługują dodatkowo osobiste pociski, m.in. piłki, laser Buzza, kapelusz Perry'ego, zaklęcie Mickeya, patelnię, lodowe pociski, tarcze Wioli i pociski wiśniowe. Natywna struktura mieści tylko osiem znaczników broni; poprzednia poprawka przekraczała ten limit. Nowa dopisuje jeden znacznik w granicach limitu i rozpoznaje dodatkowe pociski w sprawdzonej natywnej ścieżce trafienia, zachowując oryginalny komunikat, punkty, nagrody i efekty. Pełne lub nieprawidłowe własne listy pozostają bez zmian. Przy niezgodnych sygnaturach kodu poprawka tarcz nie jest włączana. Tarcze armatnie i karabinowe zachowują swoje wymagania, a wspólne wyposażenie Toy Box nadal podlega ograniczeniom świata. Testy obejmują rzeczywisty kod bramki x86, zachowanie rejestrów i flag, brak zmian danych trafienia, oryginalne i dodatkowe typy pocisków, limit listy i pomijanie specjalnych tarcz. Próba w grze potwierdziła przywoływanie konia przez gościa oraz dźwięki, dialogi i HUD nitro Aut. Późniejsze QA tarcz zastępuje wcześniejszą roboczą akceptację; nowa poprawka nadal wymaga odbioru w rozgrywce. Nie potwierdzono pełnego pokrycia wszystkich postaci i playsetów.

Native avatar homes are also read from the playsets' avatar actor records. For example, Randall's shipped `_PS` actor identifies Monsters University as his native world even when its public character list omits him. This preserves native actor selection without expanding those public lists. Particle detours publish all original function pointers before enabling callbacks, and asset override diagnostics copy the request filename before signaling completion. Automated checks cover these paths, but they do not establish the cause of reported Monsters University crashes. Visitor melee damage against crates, mission stalls and upward ledge jumps remain open gameplay issues; the target change does not repair them.

Natywny playset postaci jest ustalany również z wpisów aktorów danego świata. Przykładowo oryginalny aktor `_PS` Randalla wskazuje Uniwersytet Potworny, mimo że publiczna lista postaci go pomija. Zachowuje to wybór natywnego aktora bez rozszerzania publicznych list. Hooki cząsteczek publikują wszystkie wskaźniki oryginalnych funkcji przed włączeniem wywołań, a diagnostyka podmian zasobów kopiuje nazwę żądania przed sygnałem zakończenia. Testy obejmują te ścieżki, ale nie potwierdzają przyczyny zgłoszonych crashy MU. Niszczenie skrzyń zwykłym atakiem gości, blokady misji i skoki w górę z krawędzi pozostają otwartymi problemami rozgrywki; poprawka tarcz ich nie naprawia.

The separate `literalcrab_playset_animations` asset provider fills missing shared playset interaction mappings for all 26 original biped player models. It adds 1004 aliases while preserving every existing mapping, character power, native PS definition and original motion byte. The whole-roster audit resolves 2886 mappings across nine families: bicycle, ship steering, rowboat, horse/elephant riding, three vehicle rider groups and two environmental reactions. Every recursive blend dependency resolves; no per-character whitelist is needed. Missing rig controls are accepted only under the original shared-motion compatibility policy, with optional coat controls excluded from contact requirements. Bob's ship steering and MU bicycle work in the reported gameplay checks; static coverage does not certify every character's rendered hand/foot fit. Cars use a different rig and are outside these biped animation families. This companion provider requires its own enabled mod folder; the ASI alone does not contain its assets.

Osobny provider `literalcrab_playset_animations` uzupełnia brakujące mapowania wspólnych interakcji playsetów dla wszystkich 26 oryginalnych dwunożnych modeli gracza. Dodaje 1004 aliasy, zachowując każde istniejące mapowanie, moce postaci, natywne definicje PS i oryginalne bajty animacji. Audyt całej listy rozwiązuje 2886 mapowań w dziewięciu rodzinach: rower, ster statku, łódka wiosłowa, jazda na koniu/słoniu, trzy grupy pasażerów/kierowców oraz dwie reakcje środowiskowe. Wszystkie zależności animacji złożonych są dostępne; nie potrzeba osobnej listy wyjątków postaci. Brakujące kontrolery szkieletu dopuszcza reguła zgodności z oryginalnymi wspólnymi animacjami; opcjonalny płaszcz nie jest wymaganiem kontaktu. Próby w grze potwierdziły ster i rower MU z Bobem; audyt statyczny nie potwierdza idealnego ułożenia dłoni i stóp każdej postaci. Auta mają inny szkielet i nie należą do tych rodzin animacji. Provider wymaga własnego włączonego folderu moda; sam ASI nie zawiera jego zasobów.

With `CharactersInAnyPlayset` enabled, the original Pirates water/reset filter also accepts the supported visiting biped figures. Its original NPC membership and all other avatar, mission and checkpoint definitions stay intact. The patch resolves the shipped biped groups to concrete mask names, excludes helper/intro/NPC declarations and Cars, and leaves incompatible or customized reset filters unchanged. Code tests and an original-data comparison verify membership and preservation; representative gameplay confirms Bob's water reset in Pirates. This does not establish a fix for every reported missing respawn in other worlds.

Przy `CharactersInAnyPlayset` oryginalny filtr wody/resetu w Piratach dopuszcza również obsługiwane postacie dwunożne z innych playsetów. Zachowane są oryginalne wpisy NPC oraz pozostałe definicje postaci, misji i checkpointów. Poprawka rozwija oryginalne grupy do konkretnych masek, pomija helpery, modele intro/NPC i Auta, a niezgodne lub zmienione przez inne mody filtry pozostawia bez zmian. Testy kodu i porównanie oryginalnych danych potwierdzają członkostwo i zachowanie danych; reprezentatywna próba w grze potwierdziła reset Boba po wodzie w Piratach. Nie potwierdza to naprawy każdego zgłoszonego braku respawnu w innych światach.

With character freedom enabled, Incredibles also permits the native vehicle tool selector for visiting Cars. Tool ownership, availability and other input restrictions still use the engine's normal checks. The Lua tool adapters now remain available alongside LiteralCrab's own availability hook: validation checks its exact detour, the copied native prologue, the return jump and the unchanged function body. An unidentified hook is still rejected. Automated tests cover both the native and hooked profiles, invalid inputs, native calling conventions and the actual character-freedom installation order on a private executable mapping. Gameplay acceptance of these repairs is pending; they do not automatically award story equipment on entry or character swap.

Przy włączonym character freedom Iniemamocni dopuszczają także natywny wybór narzędzi pojazdów dla gości z Aut. Własność, dostępność wyposażenia i pozostałe ograniczenia sterowania nadal sprawdza gra. Adaptery narzędzi Lua pozostają teraz dostępne razem z własnym hookiem dostępności LiteralCraba: weryfikowane są dokładny cel skoku, skopiowany prolog, skok powrotny i niezmieniona dalsza część funkcji. Nierozpoznany hook nadal jest odrzucany. Testy obejmują oba warianty profilu, błędne argumenty, natywne ABI i rzeczywistą kolejność instalacji character freedom na prywatnym mapowaniu EXE. Odbiór w grze pozostaje do wykonania; te poprawki nie przyznają automatycznie wyposażenia fabularnego przy wejściu lub zmianie postaci.

## Optional diagnostics

Release packages and source configuration default diagnostics to off. Missing diagnostic keys in older INI files also default to off. `[Diagnostics] CrashReports=1` enables the in-process crash reporter, `ExceptionLog=1` enables the first-chance exception observer, and `TraceAudioBanks=1` enables dialogue-file and native bank/phrase tracing. QA packages may explicitly enable these options. With all diagnostic options off, startup installs no diagnostic exception observer or crash-report worker; functional mod file/audio access remains available.

PL: W paczkach release i konfiguracji źródłowej diagnostyka jest domyślnie wyłączona; brak kluczy w starszym INI również oznacza OFF. `[Diagnostics] CrashReports=1` włącza raportowanie crashy wewnątrz gry, `ExceptionLog=1` obserwację wyjątków, a `TraceAudioBanks=1` śledzenie odczytów dialogów oraz banków/wypowiedzi. Paczki QA mogą jawnie włączać te opcje. Przy wyłączonych opcjach diagnostycznych nie uruchamia się obserwator wyjątków ani wątek raportowania; dostęp modów do plików i audio nadal działa.

## Added content and native file access

With `[Mods] Enabled=1`, files below an active mod's `assets` directory are available both to the engine's member loader and to native Windows file reads, including files whose original path does not exist. This covers new dialogue banks and sidecars, character resources and other extensions. Earlier active mods win each conflicting path; unique files from later mods remain available. Disabled mods contribute nothing. Replacement game archives keep their existing member-loading behavior.

Loose files are read from their mod folder. Files supplied inside a mod package are extracted to `mods/.cache` during startup, before game/audio initialization, and reused while the package is unchanged. Directory attribute checks recognize directories containing indexed files. No junctions or copies into the original `assets` directory are needed. This provides file access; a new character or content type still needs its appropriate game definitions and compatible formats. Windows directory enumeration is not merged with mod contents.

With `[Diagnostics] TraceAudioBanks=1`, native dialogue opens log the requested path, resolved mod/cache path and requested language directory in `LiteralCrab.log` (successful dialogue logging is limited to 2048 opens per launch). `[Mods] Trace=1` also enables these messages and logs other indexed asset opens and attribute queries. Successful opens are quiet by default. Failed redirected opens are always logged, and native Win32 error codes are preserved. The selected audio directory follows the game's actual requests, including changes made through a language pack's menu.

Automated tests cover PLP/RUR/ARS paths in separate processes and across locale requests within one process, without native language directories or junctions; file precedence, disabled mods, packaged resources, cache reuse, damaged packages, custom character files and unchanged factory-language reads are also covered. They exercise quiet defaults, audio-only tracing and full file tracing while checking the same file delivery and Win32 results. A separate local-pack probe delivered all 184 banks and 184 name tables for each of PLP, RUR and ARS through the hooked Windows APIs, byte-for-byte identical to their source files. These file-access tests do not confirm audible playback in the game. An initial Russian gameplay trial still had no voices despite successful file opens. With `TraceAudioBanks=1`, mods supplying sound banks also enable native bank-loading results and the first 256 phrase lookups, preserving native behavior; their x86 ABI and rollback are tested against the supported executable.

Przy `[Mods] Enabled=1` pliki z katalogu `assets` aktywnego moda są dostępne zarówno dla loadera gry, jak i natywnych odczytów Windows, nawet gdy oryginalna ścieżka nie istnieje. Dotyczy to banków dialogowych, plików pomocniczych, zasobów postaci i innych rozszerzeń. Przy konflikcie ścieżki wygrywa wcześniejszy aktywny mod; unikalne pliki późniejszych modów pozostają dostępne. Wyłączone mody nie dostarczają plików. Zastępowane archiwa gry zachowują dotychczasową obsługę przez loader zawartości.

Pliki luźne są czytane z folderu moda. Pliki z paczek są przygotowywane w `mods/.cache` przed inicjalizacją gry/audio i używane ponownie, dopóki paczka się nie zmieni. Sprawdzanie atrybutów rozpoznaje katalogi zawierające zaindeksowane pliki. Junctiony i kopiowanie do oryginalnego `assets` nie są wymagane. Nowa postać nadal wymaga właściwych definicji gry i zgodnych formatów. Enumeracja katalogów Windows nie jest scalana z zawartością modów.

Przy `[Diagnostics] TraceAudioBanks=1` log zawiera żądaną ścieżkę dialogu, rozstrzygniętą ścieżkę moda/cache i katalog języka; limit udanych odczytów dialogów to 2048 na uruchomienie. `[Mods] Trace=1` również włącza te wpisy oraz pozostałe odczyty i sprawdzanie atrybutów. Domyślnie udane otwarcia nie są śledzone. Błędy przekierowanych otwarć są logowane zawsze, z zachowaniem kodu błędu Windows. Język wynika z faktycznego żądania gry, także po zmianie w menu paczki. Testy obejmują wyłączoną diagnostykę, samą diagnostykę audio i pełny trace plików przy identycznym dostarczaniu danych i wynikach Win32. Dodatkowy próbnik odczytał przez hooki Windows po 184 banki i 184 pliki nazw dla PLP, RUR i ARS, zgodnie ze źródłem co do bajtu, bez junctionów. Potwierdza to dostęp do plików; nie potwierdza słyszalnych dialogów w rozgrywce. Pierwsza próba rosyjskiego w grze nadal nie miała głosów mimo udanych otwarć plików. Przy `TraceAudioBanks=1` i modach dostarczających banki logowany jest także natywny wynik ładowania banku i pierwszych 256 wyszukiwań wypowiedzi, bez zmiany zachowania audio; ABI i rollback tej diagnostyki sprawdzono na obsługiwanym EXE.

## Round disc ownership API

Script mods can query `LiteralCrab.GetDiscOwner(disc)` to reconstruct round-disc ownership after Lua menu state is recreated. It returns player `0`/`1`, `-1` for an absent valid disc, and `-2` for invalid/unknown/error states. This reads the native placed tag and leaves tags, avatar effects and saves unchanged. Its additional layout checks can withhold this function without disabling the existing disc adapters. Mods must opt into the API; the function alone does not restore their cards or refresh effects. See the [API contract](MODDING.md#engine-adapters-literalcrab).

The 16 automated tests pass, including ownership arguments, both player antennas, absent/stale/unreadable records, native failures, read-only behavior and independent profile validation. The ownership test also passes against a private mapping of the supported original executable. These checks do not replace the mod's gameplay test across frontend/world transitions.

PL: `LiteralCrab.GetDiscOwner(disc)` pozwala modom odbudować właścicieli okrągłych dysków po odtworzeniu menu Lua. Wyniki: gracz `0`/`1`, brak prawidłowego dysku `-1`, nieznany/błędny stan `-2`. Funkcja odczytuje natywny tag bez zmian tagów, efektów avatara i zapisu. Niezgodność dodatkowych sygnatur wyłącza tylko nową funkcję. Mod musi wykorzystać API do odbudowania kart; samo jego udostępnienie nie zmienia menu ani odświeżania efektów. Przeszło 16 testów oraz test adaptera na prywatnym mapowaniu oryginalnego EXE. Próba w grze po przejściu frontend → świat pozostaje odbiorem integracji po stronie moda.

## Toy Box Share

With `[ToyBoxes] Enabled=1` the Toy Box menu gets its **Toy Box Share** entry back. "My Toy Boxes" lists every Toy Box from the `toyboxes` folder and "Disney's Toy Boxes" those from the configured servers, each with its picture and description; choose one and *Play Now*. Without a Disney account the game first says that only local content is available; that is expected.

- **Packages:** ordinary ZIP files with any names, in any subfolders of `toyboxes`; ZIPs are recommended because they keep different Toy Boxes' files apart. Loose save files work too: the loose files of each folder count as one package, and a folder holding a single Toy Box needs no particular names. A package may hold one Toy Box or hundreds, as Toy Box Share files (`U`, `UH`, `UC`) or Toy Box saves (`SRR`, `SHRR`, `SCRR`), from the PC, PS3 or Wii U; the names inside do not matter either, files are recognised by their content. The same Toy Box in several packages is listed once. Saves of hubs with progress of their own (Toy Box Launch, Hall of Heroes, the introduction) are left out; Toy Boxes built in the empty, themed or prebuilt Toy Box worlds are listed, autosaves included.
- **Your saves stay untouched.** The Toy Boxes are shown to the game as extra, read-only Toy Box Share slots after its own 101; nothing is copied into the save folder, so they never use up slots or reach Steam Cloud. Deleting one in the menu only hides it until the next start; remove its package to delete it.
- **Untrusted packages:** servers are only read below their URL (no other hosts or folders), with limits on listing size, pages, packages per server (2000), servers (8), redirects and time (10 minutes per start); `ServerMaxPackageMB` and `ServerMirrorMB` bound the downloads and `MaxToyBoxes` the list. Inside packages, file and archive sizes are limited, and each Toy Box is checked before the game parses it: the object count must be sane and its object list at least that long, the world must be installed, its header is written anew from cleaned, length-limited fields, and a screenshot whose size does not match its pixel data is left out.
- **Damaged files** are skipped: every package and Toy Box is checked (ZIP checksums, the save checksums, the layout and the world it needs) before the game sees it, and `LiteralCrab.log` names what was skipped and why.
- **Speed:** packages are converted once into `toyboxes\.cache` on a background thread; later starts reuse the cache and only read changed packages. While playing, nothing runs.
- **Servers (experimental):** reading Toy Boxes from servers has not been tested at a large scale yet, so enable it at your own risk. `Servers=https://example.org/toyboxes/` or `Servers=localhost:8000` (several separated by spaces; `http://` when no scheme is given). A server lists its packages in an `index.txt` beside that URL, one path relative to it per line (any subfolders and names, `#` starts a comment), or simply shows a directory listing, which is followed into subfolders. Packages are mirrored into `toyboxes\.cache\servers` in the background: new and changed ones are downloaded, ones the server no longer lists are removed (an unreachable server keeps its copies). At startup the game waits up to 5 seconds for the servers; Toy Boxes that arrive later appear after the next save or start.

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

## Installation and troubleshooting

- Copy LiteralCrab.asi and LiteralCrab.ini beside DisneyInfinity1.exe and use a compatible 32-bit ASI loader, such as Ultimate ASI Loader as dinput8.dll. Keep the game's original binkw32.dll.
- Disable another display/resolution ASI before testing LiteralCrab; two plugins patching the same functions can conflict. Preserve the disabled file as a backup.
- Change the INI while the game is closed. Alt+Enter is the supported live borderless/windowed toggle. Restart to select exclusive fullscreen.
- Check LiteralCrab.log beside the ASI for the detected game profile, effective display mode, cap, the optional settings in effect and any feature left out because the game's code differs. The log is replaced on each launch. For FPS or mouse problems, set `Diagnostics=1`, reproduce the issue, press Alt+Tab once and attach the log.
- Unsupported executables are rejected by the version/signature checks. Missing settings use the defaults above. Unrecognized Mode/VSync values fall back to borderless/auto.

Installation requires no changes to characters, gameplay scripts or game assets, and LiteralCrab never writes to the game's files. Script mods are optional, separate downloads; LiteralCrab itself ships none.
