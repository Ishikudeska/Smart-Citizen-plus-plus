# {app}

*A localization toolkit for Star Citizen*

## About

**{app}** customizes Star Citizen's localization strings: edit any string, add stats and mission details generated from the game's own data, track your blueprints, and apply it all to the game with automatic backups. It also lets you browse and extract the contents of `Data.p4k` and export game data.

{app} is a native C++ and Qt application. It reads `Data.p4k` and its DataForge database directly, so there is no Python runtime and no helper programs to run.

## Where it comes from

{app} is a C++ conversion of two open-source projects, and stays compatible with both:

- [**Smart Citizen**](https://github.com/Osiris-DevWorks/smart-citizen) by **Osiris DevWorks** (Osiris_x, Tichro) and contributors (Stealrull, Ishikudeska, jonigirl, Coerwyn, denis-coach, scubamount, hkstrongside, odw-okano), with translations by Akwa, Nxzzin, Thord82 and others. Its features, generated text and file formats are what {app} reproduces, byte for byte where it matters. Licensed under the Apache License 2.0.
- [**unp4k**](https://github.com/dolkensp/unp4k) by dolkensp, and the Osiris DevWorks fork [**odw-fast-unp4k**](https://github.com/Osiris-DevWorks/odw-fast-unp4k): the reference for reading `Data.p4k`, DataForge and CryXML, and for the game-data export. Licensed under the MIT License.

The non-English game strings are community translations:

- [**Dymerz/StarCitizen-Localization**](https://github.com/Dymerz/StarCitizen-Localization) (French, Brazilian Portuguese, Italian)
- [**Thord82/Star_citizen_ES**](https://github.com/Thord82/Star_citizen_ES) (Spanish)
- [**stdblue/StarCitizenJapaneseResources**](https://github.com/stdblue/StarCitizenJapaneseResources) (Japanese)
- [**42Kit**](https://ini.42kit.com/) (Chinese)
- [**rjcncpt/StarCitizen-Deutsch-INI**](https://github.com/rjcncpt/StarCitizen-Deutsch-INI) (German)

## Main features

- **String Editor**: every localization string with its stock, current and custom value; per-column filters, favorites and ASOP sort order for ships, a side editor and a live preview.
- **Enhancements**: ship, component, weapon, mission, commodity and journal details generated from DataForge, with a Tag Builder for the name tags.
- **Blueprint Tracker**: mark blueprints you own, or scan your game logs for them.
- **P4K Explorer**: browse, search, preview and extract anything in `Data.p4k`, including DataForge records as XML; export `game_data.json`.
- **Safe apply**: a backup before every write, validation of the written file and automatic rollback.
