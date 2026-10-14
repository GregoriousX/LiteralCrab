# LanguageSwitcher

Language selection is integrated into **LiteralCrab.asi**. It is disabled by default:

```ini
[Startup]
LanguageSwitcher=off
```

With the game closed, install the language pack and follow its audio setup, then set:

```ini
[Startup]
LanguageSwitcher=polish
```

Restart the game. Remove the pack's separate **PS3Languages.asi** from the game directory when using this integrated option. Keep the language pack's assets and Lua patches installed. LiteralCrab supplies the engine behavior; it does not contain translations, fonts or voice banks.

The following 18 locale IDs are defined in the PS3 `locale/locale.gfg` files (Japanese appears in the update). Enter any ID below as `LanguageSwitcher=<ID>`; case does not matter.

| Language | INI value | Dictionary stem |
| --- | --- | --- |
| English, North America | `EnglishNTSC` | `english_ntsc` |
| English, Europe | `EnglishPAL` | `english_pal` |
| French, Canada | `FrenchNTSC` | `frenchcanada` |
| French, France | `FrenchPAL` | `frenchparisian` |
| Italian | `Italian` | `italian` |
| German | `German` | `german` |
| Spanish, Latin America | `SpanishNTSC` | `spanishlatinamerica` |
| Spanish, Spain | `SpanishPAL` | `castilianspanish` |
| Japanese | `Japanese` | `japanese` |
| Swedish | `Swedish` | `swedish` |
| Dutch | `Dutch` | `dutch` |
| Danish | `Danish` | `danish` |
| Norwegian | `Norwegian` | `norwegian` |
| Polish | `Polish` | `polish` |
| Russian | `Russian` | `russian` |
| Arabic | `Arabic` | `arabic` |
| Finnish | `Finnish` | `finnish` |
| Portuguese, Brazil | `BrazilianPortuguese` | `brazilianportuguese` |

Short aliases `english`, `french`, `spanish` and `portuguese` select `EnglishNTSC`, `FrenchPAL`, `SpanishNTSC` and `BrazilianPortuguese`. Regional aliases include `englishus`, `englishuk`, `frenchcanadian` (also `frenchcanada`), `frenchparisian`, `spanishlatinamerica`, and `spanishspain` (also `castilianspanish`). For example, `LanguageSwitcher=italian` or `LanguageSwitcher=englishuk`. An exact additional native locale ID can also be supplied; the loaded game configuration must support it. Empty, `off`, `auto` and `default` leave both startup calls untouched.

This list identifies supported settings, not bundled language packs. Additional languages require a PC-compatible pack that registers the locale in the PC build of `locale.gfg`, supplies converted Win32 dictionaries, fonts and any voices, and prepares native audio directories. Raw PS3 files alone are insufficient. Gold's existing five locales remain factory choices; other entries require the dictionary preflight below.

Language mods need `[Mods] Enabled=1` and their own enabled configuration. The preflight uses the resolved asset provider, so pack folder names are not hardcoded and ordinary ZIP mods are supported. For added languages it checks that nonempty dictionaries are available for all eight game groups: `main`, `ca`, `in`, `lr`, `mu`, `pi`, `tb` and `ts`. For Polish these are `lang/<group>/polish_win32.dct`. Deliberately installed loose files under the game's `assets` directory are also accepted. Disabled mods do not make their assets available. A missing dictionary leaves the startup calls unchanged and logs the missing path.

The native supported-language check runs at both initial locale setup and the later Steam callback. If a requested locale is unsupported, the original incoming language and native return value are preserved. If native selection fails, the wrapper retries the original incoming language. The INI setting takes priority on each launch; to resume the original Steam language behavior, set it back to `off`. The switcher does not read or write the saved-language registry setting and does not add language menu entries or change the language during play.

The PS3-derived 0.2.3 packs still need their existing audio-directory setup. Their `Install-Audio.ps1` creates a directory junction such as `assets/sound/dialog/PLP` pointing to the pack's audio folder. Integration does not create or replace these junctions, and text selection alone does not verify speech. Keep the junction's target pack installed. English voice/video fallbacks and incomplete translations remain properties of the pack.

The implementation adapts the two-call startup mechanism documented in the DI1-PS3-Dubbing project. It verifies both x86 Gold call sites, decoded targets, relocated locale singleton and native function signatures before changing either call operand. Original functions remain intact; changes are in memory. A conflicting standalone ASI or different executable profile is refused. The switcher installs after mod assets are indexed and before native locale/audio startup, with no timer or frame polling. Decisions are recorded in **LiteralCrab.log**.

Validation: Release build, eight CTest cases, and private-mapped supported-EXE tests passed. These cover default-off behavior, all 18 PS3 IDs and dictionary stems, regional aliases, missing/disabled assets, both call sites and their ABI, unsupported/failed native selections, original arguments and full return values, conflicting patches, and complete rollback. Engine stand-ins exercise 130 calls per stage for 26 choices (18 IDs, seven aliases and a custom locale), checking all eight requested dictionary paths for added languages. The user confirmed Polish menu text after two cold launches using only LiteralCrab.asi; the retained second-start log confirms native success at both stages. Integrated speech and the other PS3 language packs have not been separately confirmed in gameplay.

Development branch: `dev`; no new release was created. [Polska dokumentacja](LANGUAGE_SWITCHER.pl.md).
