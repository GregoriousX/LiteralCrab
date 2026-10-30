# Installation and troubleshooting

- Copy LiteralCrab.asi and LiteralCrab.ini beside DisneyInfinity1.exe and use a compatible 32-bit ASI loader, such as Ultimate ASI Loader as dinput8.dll. Keep the game's original binkw32.dll.
- Disable another display/resolution ASI before testing LiteralCrab; two plugins patching the same functions can conflict. Preserve the disabled file as a backup.
- Change the INI while the game is closed. Alt+Enter is the supported live borderless/windowed toggle. Restart to select exclusive fullscreen.
- Check LiteralCrab.log beside the ASI for the detected game profile, effective display mode, cap, the optional settings in effect and any feature left out because the game's code differs. The log is replaced on each launch. For FPS or mouse problems, set `Diagnostics=1`, reproduce the issue, press Alt+Tab once and attach the log.
- Unsupported executables are rejected by the version/signature checks. Missing settings use the defaults above. Unrecognized Mode/VSync values fall back to borderless/auto.

Installation requires no changes to characters, gameplay scripts or game assets, and LiteralCrab never writes to the game's files. Script mods are optional, separate downloads; LiteralCrab itself ships none.
