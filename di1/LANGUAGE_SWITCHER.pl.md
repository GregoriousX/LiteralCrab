# LanguageSwitcher

Przełączanie języka jest częścią **LiteralCrab.asi**. Domyślnie pozostaje wyłączone:

```ini
[Startup]
LanguageSwitcher=off
```

Przy zamkniętej grze wgraj paczkę językową do aktywnego katalogu `mods`, a następnie ustaw:

```ini
[Startup]
LanguageSwitcher=polish
```

Uruchom grę ponownie. Usuń osobny **PS3Languages.asi** z katalogu gry, gdy korzystasz z tej opcji. Zostaw pliki paczki i jej poprawki Lua. LiteralCrab dostarcza mechanizm wyboru języka; tłumaczenia, fonty i nagrania pozostają w paczce.

Poniższe 18 identyfikatorów występuje w plikach PS3 `locale/locale.gfg` (japoński znajduje się w aktualizacji). Wpisz wybraną wartość jako `LanguageSwitcher=<identyfikator>`; wielkość liter nie ma znaczenia.

| Język | Wartość INI | Nazwa słownika bez końcówki |
| --- | --- | --- |
| Angielski, Ameryka Północna | `EnglishNTSC` | `english_ntsc` |
| Angielski, Europa | `EnglishPAL` | `english_pal` |
| Francuski, Kanada | `FrenchNTSC` | `frenchcanada` |
| Francuski, Francja | `FrenchPAL` | `frenchparisian` |
| Włoski | `Italian` | `italian` |
| Niemiecki | `German` | `german` |
| Hiszpański, Ameryka Łacińska | `SpanishNTSC` | `spanishlatinamerica` |
| Hiszpański, Hiszpania | `SpanishPAL` | `castilianspanish` |
| Japoński | `Japanese` | `japanese` |
| Szwedzki | `Swedish` | `swedish` |
| Niderlandzki | `Dutch` | `dutch` |
| Duński | `Danish` | `danish` |
| Norweski | `Norwegian` | `norwegian` |
| Polski | `Polish` | `polish` |
| Rosyjski | `Russian` | `russian` |
| Arabski | `Arabic` | `arabic` |
| Fiński | `Finnish` | `finnish` |
| Portugalski, Brazylia | `BrazilianPortuguese` | `brazilianportuguese` |

Skróty `english`, `french`, `spanish` i `portuguese` wybierają `EnglishNTSC`, `FrenchPAL`, `SpanishNTSC` i `BrazilianPortuguese`. Dostępne są też skróty regionalne: `englishus`, `englishuk`, `frenchcanadian` (również `frenchcanada`), `frenchparisian`, `spanishlatinamerica` oraz `spanishspain` (również `castilianspanish`). Przykłady: `LanguageSwitcher=italian` lub `LanguageSwitcher=englishuk`. Można też podać dodatkowy dokładny natywny identyfikator obsługiwany przez konfigurację gry. Pusta wartość, `off`, `auto` i `default` pozostawiają oba wywołania startowe bez zmian.

Lista opisuje obsługiwane ustawienia; paczki nie są dołączone do ASI. Dodatkowy język wymaga paczki zgodnej z PC, która rejestruje język w konfiguracji PC w `locale.gfg` oraz dostarcza przekonwertowane słowniki Win32, fonty i ewentualne nagrania. Same surowe pliki PS3 nie wystarczą. Pięć języków fabrycznych Gold nadal korzysta z natywnej kontroli dostępności; pozostałe wymagają kontroli słowników opisanej poniżej.

Paczki modów wymagają `[Mods] Enabled=1` i własnej aktywnej konfiguracji. Kontrola plików korzysta z indeksu aktywnych zasobów, więc nie zależy od konkretnej nazwy folderu moda i obsługuje zwykłe paczki ZIP. Dla dodatkowego języka sprawdza niepuste słowniki ośmiu grup gry: `main`, `ca`, `in`, `lr`, `mu`, `pi`, `tb` i `ts`. Polski używa ścieżek `lang/<grupa>/polish_win32.dct`. Obsługiwane są również celowo wgrane luźne pliki w katalogu `assets` gry. Wyłączony mod nie udostępnia swoich zasobów. Brak słownika pozostawia wywołania startowe bez zmian i jest opisany w logu.

Gra sprawdza obsługę języka przed pierwszym wyborem lokalizacji i ponownie przy odczycie języka ze Steam. Nieobsługiwany język pozostawia oryginalny argument i wynik. Jeśli natywny wybór nie powiedzie się, przełącznik próbuje oryginalnego języka. Wartość INI ma pierwszeństwo przy każdym starcie; `off` przywraca oryginalne zachowanie Steam. Przełącznik nie odczytuje ani nie zapisuje ustawienia języka w rejestrze, nie dodaje pozycji menu i nie zmienia języka w trakcie rozgrywki.

Aktualny loader z brancha `dev` udostępnia nowe pliki aktywnych modów natywnemu audio przed jego inicjalizacją, więc dawne obejście z junctionami tworzonymi przez `Install-Audio.ps1` nie jest potrzebne. Luźne banki pozostają w folderze moda, a banki z paczek są przygotowywane w `mods/.cache`. Przy konflikcie ścieżki wygrywa wcześniejszy aktywny mod. Nie usuwaj istniejącego katalogu natywnego bez sprawdzenia, że jest wyłącznie tymczasowym junctionem moda. Sam tekst nie potwierdza odtwarzania głosów. Pozostałe angielskie nagrania, filmy i niepełne tłumaczenia wynikają z zawartości paczki. Zobacz [obsługę odczytów natywnych i weryfikację](README.md#added-content-and-native-file-access).

Implementacja przenosi mechanizm dwóch wywołań z projektu DI1-PS3-Dubbing. Przed zmianą pamięci sprawdza oba miejsca wywołania, ich cele, adres singletona po relokacji i sygnatury funkcji natywnych w obsługiwanym EXE Gold x86. Oryginalne funkcje pozostają nienaruszone; zmiany dotyczą pamięci. Konflikt z osobnym ASI lub innym profilem EXE powoduje odmowę instalacji. Opcja uruchamia się po indeksowaniu zasobów modów, przed startem lokalizacji/audio, bez timera i odpytywania co klatkę. Wyniki trafiają do **LiteralCrab.log**.

Weryfikacja: build Release, osiem testów CTest oraz testy na prywatnie zmapowanym EXE przeszły. Obejmują wyłączenie domyślne, wszystkie 18 identyfikatorów PS3 i nazwy słowników, skróty regionalne, brakujące/wyłączone zasoby, oba wywołania i ABI, nieobsługiwany język oraz nieudany wybór, zachowanie oryginalnych argumentów i pełnych wyników, konflikty i cofanie zmian. Atrapy silnika wykonują po 130 wywołań na etap dla 26 ustawień (18 identyfikatorów, siedem skrótów i własny język), sprawdzając wszystkie osiem ścieżek słowników dodatkowych języków. Użytkownik potwierdził polskie menu po dwóch startach z samym LiteralCrab.asi; log drugiego uruchomienia potwierdza natywny sukces obu etapów. Dubbing w integracji i pozostałe paczki językowe PS3 nie były osobno potwierdzane w grze.

Branch rozwojowy: `dev`; nie utworzono nowego wydania. [English documentation](LANGUAGE_SWITCHER.en.md).
