# Application logic: settings, INI pipeline, enhancements generator, blueprints, i18n.

configure_file(AppIdentity.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/core/AppIdentity.h @ONLY)

add_library(sccore STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/AppIdentity.h
    AppInfo.h
    AppInfo.cpp
    Settings.h
    Settings.cpp
    Paths.h
    Paths.cpp
    ScInstall.h
    ScInstall.cpp
    EnginePaths.h

    text/PyText.h
    text/PyText.cpp
    text/IniText.h
    text/IniText.cpp
    text/IniFile.h
    text/IniFile.cpp
    text/PyJson.h
    text/PyJson.cpp
    text/PyFormat.h
    text/PyFormat.cpp

    model/StringEntry.h
    model/StringEntry.cpp
    model/StringTable.h
    model/StringTable.cpp
    model/Enhancements.h
    model/Enhancements.cpp

    merge/Merger.h
    merge/Merger.cpp
    merge/SourceLoader.h
    merge/SourceLoader.cpp

    apply/GameFile.h
    apply/GameFile.cpp
    apply/Stamps.h
    apply/Stamps.cpp
    apply/Backups.h
    apply/Backups.cpp
    apply/UserCfg.h
    apply/UserCfg.cpp
    apply/ApplyService.h
    apply/ApplyService.cpp
    apply/LocPack.h
    apply/LocPack.cpp

    tags/TagBuilder.h
    tags/TagBuilder.cpp

    blueprints/LogScanner.h
    blueprints/LogScanner.cpp
    blueprints/OwnedItems.h
    blueprints/OwnedItems.cpp
    blueprints/BlueprintMeta.h
    blueprints/BlueprintMeta.cpp
    blueprints/BlueprintExport.h
    blueprints/BlueprintExport.cpp

    i18n/Translator.h
    i18n/Translator.cpp

    user/UserIni.h
    user/UserIni.cpp

    profile/SettingsProfile.h
    profile/SettingsProfile.cpp

    net/Downloader.h
    net/Downloader.cpp
    net/AppUpdater.h
    net/AppUpdater.cpp

    log/Log.h
    log/Log.cpp
    log/CrashHandler.h
    log/CrashHandler.cpp

    util/Progress.h
    util/Progress.cpp
    util/OneDrive.h
    util/OneDrive.cpp

    enhancements/Xml.h
    enhancements/Xml.cpp
    enhancements/RecordStore.h
    enhancements/RecordStore.cpp
    enhancements/Common.h
    enhancements/Common.cpp
    enhancements/Tags.h
    enhancements/Tags.cpp
    enhancements/Stats.h
    enhancements/Stats.cpp
    enhancements/Lookups.h
    enhancements/Lookups.cpp
    enhancements/Context.h
    enhancements/Categories.h
    enhancements/Categories.cpp
    enhancements/Missions.h
    enhancements/MissionDetails.cpp
    enhancements/MissionRewards.cpp
    enhancements/Crafting.h
    enhancements/Crafting.cpp
    enhancements/Generator.h
    enhancements/Generator.cpp

    pipeline/Patcher.h
    pipeline/Patcher.cpp
    pipeline/Extraction.h
    pipeline/Extraction.cpp

    missions/Places.h
    missions/Places.cpp
    missions/Catalog.h
    missions/Catalog.cpp
    loadout/Catalog.h
    loadout/Catalog.cpp
    loadout/Loadout.h
    loadout/Loadout.cpp
    loadout/Performance.h
    loadout/Performance.cpp
)

target_include_directories(sccore PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(sccore PUBLIC scengine Qt6::Core Qt6::Network Qt6::Concurrent)
if(WIN32)
    target_link_libraries(sccore PRIVATE dbghelp) # minidumps
endif()
sc_set_warnings(sccore)
sc_precompile(sccore <QJsonArray> <QJsonDocument> <QJsonObject> <pugixml.hpp>)
