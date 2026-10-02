# Smart Citizen++

A Complete re-write of Smart Citizen in C++. 

# Acknowledgements

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

### License

Smart Citizen++ is licensed under the **Apache License, Version 2.0**, the same as Smart Citizen. See [LICENSE](LICENSE), and [NOTICE](NOTICE) for the Smart Citizen and unp4k attributions and the Star Citizen / CIG trademark notice. Smart Citizen++ is a fan project and is not affiliated with Cloud Imperium Games, Roberts Space Industries, or Osiris DevWorks.



# Why?

 Well for a few reasons:
 > 1. Python, while not terrible, is going to be slower at everything this application is trying to accomplish.
 > 2. C++ (IMO) can be modified and added to with minimal work. Whereas the Python side takes quite some time for a new version. 
 > 3. Cross-Platform Compatibility. This application was built using the STD:: library. Meaning it can easily be run on any flavor of linux you so choose!

 # Linux and non-windows platforms:

 Should work "out of box". The application uses Qt as it's backend for UI, so your milage will vary. 
    If something doesn't work please let me know by creating an issue.

# Contribution details:
