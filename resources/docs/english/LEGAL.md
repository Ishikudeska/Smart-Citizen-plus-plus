# Legal

## Star Citizen / Cloud Imperium

{app} is an **unofficial community tool** for Star Citizen. It is not developed, endorsed, sponsored by or affiliated with Cloud Imperium Games (CIG) or Roberts Space Industries (RSI).

**Star Citizen®**, **Roberts Space Industries®** and **Cloud Imperium®** are registered trademarks of Cloud Imperium Rights LLC and Cloud Imperium Rights Ltd. All Star Citizen game data, including the contents of `Data.p4k`, is the intellectual property of Cloud Imperium Rights LLC.

{app} does not redistribute any CIG or RSI content. It reads files from **your own licensed Star Citizen installation** and writes your customized strings back to that installation. Nothing from the game leaves your computer through {app}.

## Licenses

{app} is derived from Smart Citizen (Apache License 2.0, © Osiris DevWorks and contributors) and unp4k (MIT License, © dolkensp and contributors). The full license texts and attributions ship in the `LICENSE` and `NOTICE` files next to the executable.

Third-party libraries:

- **Qt 6** — The Qt Company, used under the **GNU LGPL v3**, linked dynamically.
- **zstd** — Meta Platforms, **BSD** license.
- **zlib** — Jean-loup Gailly and Mark Adler, **zlib** license.
- **pugixml** — Arseny Kapoulkine, **MIT** license.

The software is provided **"as is"**, without warranties or conditions of any kind.

## Privacy

{app} is a local desktop application. It sends no telemetry, analytics or crash reports, and has no accounts.

It goes online only to download a language's community `global.ini` when you switch to that language, and to fetch an INI file from a URL when you import one. Your edits, settings, backups and caches stay on your computer:

- **Settings** — `%APPDATA%\{app}\settings.ini` (or `data\settings.ini` next to the executable in portable builds).
- **Your edits, backups and string cache** — `Documents\{app}\<channel>\` by default (configurable on the Config page).
- **DataForge cache** — `%LOCALAPPDATA%\{app}\<channel>\cache\dataforge\`.
- **Logs and crash reports** — `Documents\{app}\logs\`.

## Use at your own risk

{app} only changes localization text, which CIG supports for community translations. How you use it is your responsibility.
