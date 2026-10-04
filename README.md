# Smart Citizen++

A complete rewrite of Smart Citizen in C++.

Smart Citizen++ is a localization toolkit for Star Citizen. It changes the text the game shows you, so ship, item and mission information you would normally look up on a wiki shows up in the game itself. It reads your installed `Data.p4k` directly, generates that text from the game's own data, merges it with any edits you make, and writes the result to the game with a backup first.

It is a native C++ and Qt application. There is no Python runtime to install and no helper programs to run.

# ON AI USAGE:
 NOTICE, this repository was ***ORIGINALLY*** a fork of https://github.com/Osiris-DevWorks/smart-citizen. I have, after consideration, taken it OFF of that fork network. Seeing as this will become more than what smart citizen does, I feel as though there's a need to differentiate myself. Yes, you will see that Claude and Copilot are both contributors of this repo. However, this is a half-truth. The original Smart Citizen project uses AI. I however, cannot AFFORD to pay for Claude, Copilot, or any other AI (machine learning) program. I AM LITERALLY BROKE AND JOBLESS.

## What it does

- **Generates enhanced game text** from the game's DataForge records: name tags and stat blocks on ships, components, weapons and missiles; mission details, rewards and hauling routes on contracts; mining and crafting information on commodities.
- **Lets you edit any string in the game** in a searchable table with a live preview of how it will look in-game.
- **Tracks the blueprints you own** by scanning your game logs, and marks them in mission blueprint lists.
- **Browses and extracts `Data.p4k`**, including DataForge records as XML and a `game_data.json` export.
- **Applies everything safely**: a backup before every write, a check of the written file, and automatic rollback if something goes wrong.

## How it works

The workflow is four steps. In Simple mode one button runs all of them; in Advanced mode each has its own control.

1. **Extract.** Smart Citizen++ opens `Data.p4k` for the channel you have selected and unpacks two things: the stock English `global.ini`, and the DataForge database, which it converts to XML. DataForge is where the game keeps its records for every ship, component, weapon, mission, commodity and blueprint. Everything comes from your own install, not from a download or community mirror, so after a patch you extract again and the data matches the build you are playing.
2. **Generate.** The enhancement generator reads those records and writes a set of enhancement INI files, one per category (ships, components, ship weapons, FPS weapons, missiles, mission rewards, commodities, journal, medical consumables). Lookups run in parallel across all CPU cores.
3. **Merge.** The string table is built in layers: the stock strings, then a community translation if you picked another language, then the generated enhancements, then your own edits from `user.ini`. Each row shows where its current value came from.
4. **Apply.** The merged result is written to `StarCitizen\<channel>\data\Localization\<language>\global.ini`. The existing file is backed up first, the written file is validated, and the backup is restored automatically if validation fails. `g_language` is set in `user.cfg` so the game loads the right file, and a short line is added under the version label on the main menu so you can confirm the pack is active.

The generated text matches the original Python Smart Citizen byte for byte. A parity test suite in `tests/parity` checks this.

## Enhancements

Every category, and most individual lines, can be turned on or off from the Enhancements tab.

### Name tags

Bracketed tags on item names tell you what an item is without opening its description:

- **Components** (shields, power plants, coolers, quantum drives, jump drives) get class, size and grade, for example `[MIL-S1-A] Bracer`. An optional Type element adds the component type.
- **Ship weapons** get damage type and size, for example `[Energy-S3]`. Mining lasers get type and size instead.
- **Missiles** get tracking type and size, for example `[IR-S2] Arrester`.
- **Commodities** can show a label, what their crafting materials are used for, and a collection tag.

The **Tag Builder** controls how tags look: the order of the parts, which parts appear, abbreviation length (`M`, `MIL` or `Military`), the separator, the bracket style, and whether the tag goes before or after the name. A live preview shows the result.

### Stat blocks

Descriptions get a stats section built from the item's records. Depending on the item, this covers things like SCM speed, cargo capacity, shield HP, DPS, mining laser beam stats (fracture and extraction), and handheld salvage tool rates. The block can sit above or below the lore text.

### Missions

- **MISSION DETAILS** block on contract descriptions: mission type, difficulty, spawns, reputation, blueprints, ace pilots and resource signatures. Each line can be shown or hidden.
- **XP with its reputation track**, for example `750 XP (Hauling)`.
- **POTENTIAL BLUEPRINTS** and **ITEM REWARDS** sections listing what a contract can pay out, read from the game's contract generators.
- **Title tags**: reputation reward, `[ACE]`, and `[BP]` when every version of a mission pays a blueprint or `[BP?]` when it only sometimes does.
- **Section labels** (MISSION DETAILS, POTENTIAL BLUEPRINTS and so on) and their emphasis style can be renamed and restyled.

### Hauling routes in titles

Hauling contract titles can lead with their route, for example `Area18 > Lorville - <original title>`. Multi-stop hauls list every drop-off (`Area18 > Lorville, New Babbage`). You choose whether the route is prepended, appended or replaces the title, the arrow style (including arrows that show one or many endpoints on each side), and how much of each location to show. Optional shortening trims the stock title (for example "Opportunity for Independent Cargo Hauler" becomes "Intro") and abbreviates cargo sizes ("Extra Small" becomes "XS") so the route fits.

### Mining, crafting and commodities

- Ore names can carry their base resource signature, for example `Aluminium (RS 4285)`, which shows up everywhere the game displays the name, including the mission tracker.
- Recco Battaglia scan and mining contracts get an `[RS ####]` title tag and an optional breakdown of each target ore's RS values.
- The Mining Compendium journal entry lists each ore's base RS next to the locations where it can be mined.
- Crafting materials show a BLUEPRINT DATA section with what they are used to craft and where to find them.

### Other enhancements

- **Medical consumables**: MedPen, OxyPen, AdrenaPen and the other CureLife pens get a plain line saying what they actually do.
- **Ship favorites**: star a ship and its name gets a prefix (default `*`) that sorts it to the top of the in-game ship list.
- **Fixes for CIG data bugs**: declarative patch files under `resources/patches/` work around known mistakes in the game data, such as a contract pointing at another mission's description. They are reapplied on every regeneration and can be deleted once CIG fixes the bug.

## String Editor

The Advanced mode table lists every localization string in the game:

- **Columns** for the stock value, the current value (stock plus any imported layers), your custom value, and a status of Modified, Enhanced, Unmodified or New.
- **Search and filters** by key or text, category (Ships, Ship Items, Missions, Gear, Commodities, Journal, Other) and status, with toggles for hiding unmodified rows, showing only ship names, or showing only favorites. Each column header also has its own filter box.
- **Preview pane** that renders the game's formatting tokens (line breaks, underlined headings, highlighted values, mission placeholders) so you can check a long description before applying it.
- **Edits are saved automatically** to `user.ini`, kept separately for each channel (LIVE, PTU, EPTU, HOTFIX, TECH-PREVIEW). They survive game patches and are reapplied on top of fresh stock strings.
- **Import INI** merges another INI file into your edits, with a per-key choice to keep yours, use the imported value, append, prepend or write a custom value.
- **Export loc-pack** bundles the applied `global.ini` into a zip that anyone can drop into their game folder without installing Smart Citizen++.

## Blueprint Tracker

- Two lists, available and owned, with search and filters by mission, type, class, size and grade. Hovering a blueprint shows its details and every mission that can drop it.
- **Scan Logs for Owned Blueprints** reads your Star Citizen logs for blueprints you have received and marks them owned. Only new log entries are read on each scan. LIVE and HOTFIX share account progression, so both are scanned by default.
- Owned blueprints get an `[Owned]` tag in mission POTENTIAL BLUEPRINTS lists, so a contract tells you at a glance what you still need.
- The owned list can be exported to and imported from JSON or CSV, including exports from scmdb.net.

## P4K Explorer

- Browse and search the contents of any channel's `Data.p4k`, or another archive.
- Preview entries as XML (CryXML is converted), text or hex.
- Extract a selection of files.
- Browse DataForge records as XML.
- Export `game_data.json`.

## Languages

English uses the stock strings extracted from your `Data.p4k`. For French, German, Spanish, Italian, Brazilian Portuguese, Japanese and Chinese, Smart Citizen++ downloads a community translation and layers it over the English strings, so anything the translation does not cover falls back to English. You can point a language at a different `global.ini` URL, such as your own fork. Generated enhancements stay in English.

The interface is available in the same languages. French, Brazilian Portuguese and Spanish are partly translated by people; any string without a human translation falls back to a machine translation and then to English.

## Simple and Advanced mode

- **Simple mode** is a two-button screen. **Apply Enhancements** extracts, generates and applies with your saved settings, taking a backup first. The other button switches to Advanced mode.
- **Advanced mode** is the full app: the String Editor, Enhancements, Blueprint Tracker, P4K Explorer, Config and Log tabs.

## Safety and undo

- Smart Citizen++ only writes the game's `global.ini` for the selected channel and language, and the `g_language` line in `user.cfg`. Nothing else in the install is touched.
- A timestamped backup is taken before every apply, and the last 5 are kept. **Restore Backup** rolls back to any of them.
- **Clear Localization** deletes the custom `global.ini` and returns the game to its stock text in one click. Your edits stay saved in the app.
- `user.ini` is also backed up before every change, and **Restore user.ini** rolls your edits back to an earlier snapshot.
- It only changes localization text. It does not touch game logic or communicate with CIG's servers. CIG has [publicly backed community localization](https://robertsspaceindustries.com/spectrum/community/SC/forum/1/thread/star-citizen-community-localization-update), but use it at your own risk.

## Other features

- **Export and import settings** to move your whole setup, including every channel's edits, to another PC.
- **Update check** against this repository's GitHub releases, with a download of the new installer.
- **Guided tour** that walks through the main workflow on first launch, and can be replayed from the toolbar.
- **Built-in help, FAQ and log** tabs. The log can be filtered and exported for bug reports.
- **Dark and light themes**, and a remembered window and column layout.

## Download

Windows builds are published on the [Releases](https://github.com/Ishikudeska/Smart-Citizen-plus-plus/releases) page in two forms:

- `SmartCitizenPlusPlus-<version>-Setup.exe`: an installer. Settings are stored in `%APPDATA%\Smart Citizen++\`, and data (edits, generated files, backups) in `Documents\Smart Citizen++\`.
- `SmartCitizenPlusPlus-Portable-<version>.zip`: a portable build that keeps its settings and data next to the executable.

The app is not code-signed yet, so Windows SmartScreen will warn that it is unrecognized. Choose **More info → Run anyway**, and only download builds from this repository's Releases page.

## Building from source

Requirements:

- Qt 6.8 or newer (Core, Gui, Network, Concurrent, Qml, Quick, QuickControls2, Test, QuickTest)
- CMake 3.25 or newer and Ninja
- A C++23 compiler

zstd, zlib and pugixml are downloaded and built automatically by CMake, pinned by SHA-256.

The presets in `CMakePresets.json` expect Qt 6.12 with MinGW 13.1 installed under `C:\Qt`:

```sh
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug -LE parity
```

The parity tests are excluded above because they need the original Python app as a reference.

To build the release installer and portable zip into `dist/`:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\package.ps1
```

The installer step needs [Inno Setup 6](https://jrsoftware.org/isinfo.php) and is skipped if it is not installed.

On other platforms, configure with CMake and point `CMAKE_PREFIX_PATH` at your Qt install. See the next sections for the state of Linux support.

## Why?

Well, for a few reasons:

> 1. Python, while not terrible, is going to be slower at everything this application is trying to accomplish.
> 2. C++ (IMO) can be modified and added to with minimal work, whereas the Python side takes quite some time for a new version.
> 3. Cross-platform compatibility. This application was built using the C++ standard library (`std::`), meaning it can easily be run on any flavor of Linux you so choose!

## Linux and non-Windows platforms

Should work "out of the box". The application uses Qt as its backend for the UI, so your mileage will vary. If something doesn't work, please let me know by creating an issue.

## Acknowledgements

Smart Citizen++ stands on the shoulders of these projects and people.

### Smart Citizen

This is a port of [**Smart Citizen**](https://github.com/Osiris-DevWorks/smart-citizen) by [**Osiris DevWorks**](https://github.com/Osiris-DevWorks). Its features, merge logic, settings formats and translations all come from the original Python app. Thank you to the Smart Citizen developers and contributors:

- [**Osiris DevWorks**](https://github.com/Osiris-DevWorks)
- [**Stealrull**](https://github.com/Stealrull)
- **jonigirl**
- [**Coerwyn**](https://github.com/Coerwyn)
- [**denis-coach**](https://github.com/denis-coach) (also [h0use](https://github.com/h0useRus))
- [**scubamount**](https://github.com/scubamount)
- **hkstrongside**
- [**odw-okano**](https://github.com/odw-okano)

Smart Citizen in turn credits [**ExoAE's ScCompLangPack**](https://github.com/ExoAE/ScCompLangPack) for the original concept and merge logic, and [**MrKraken**](https://github.com/MrKraken/StarStrings) for the ASOP terminal enhancements and mission contract localization work.

### unp4k

The `Data.p4k` reader and DataForge → XML converter in `src/engine/` are a C++ port of [**unp4k / unforge**](https://github.com/dolkensp/unp4k) by **Peter Dolkens** and contributors (MIT License), and of Osiris DevWorks' parallelized fork, [**odw-fast-unp4k**](https://github.com/Osiris-DevWorks/odw-fast-unp4k). Without their work on reverse-engineering the p4k and DataForge formats, none of this would be possible.

### Translations

- **Akwa**: French interface translation
- **Nxzzin**: Brazilian Portuguese interface translation
- [**Thord82**](https://github.com/Thord82): Spanish interface translation and the [Spanish `global.ini`](https://github.com/Thord82/Star_citizen_ES)
- [**Dymerz/StarCitizen-Localization**](https://github.com/Dymerz/StarCitizen-Localization): French, Brazilian Portuguese and Italian `global.ini`
- [**stdblue/StarCitizenJapaneseResources**](https://github.com/stdblue/StarCitizenJapaneseResources): Japanese `global.ini`
- [**42Kit**](https://ini.42kit.com/): Chinese `global.ini`
- [**rjcncpt**](https://github.com/rjcncpt): [German `global.ini`](https://github.com/rjcncpt/StarCitizen-Deutsch-INI)

And the testers and the wider **Star Citizen community**, whose feedback shaped Smart Citizen.

## License

Smart Citizen++ is licensed under the **Apache License, Version 2.0**, the same as Smart Citizen. See [LICENSE](LICENSE), and [NOTICE](NOTICE) for the Smart Citizen and unp4k attributions and the Star Citizen / CIG trademark notice. Smart Citizen++ is a fan project and is not affiliated with Cloud Imperium Games, Roberts Space Industries, or Osiris DevWorks.
