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

    text/PyText.h
    text/PyText.cpp
    text/IniText.h
    text/IniText.cpp
    text/IniFile.h
    text/IniFile.cpp

    model/StringEntry.h
    model/StringEntry.cpp
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

    user/UserIni.h
    user/UserIni.cpp

    pipeline/Patcher.h
    pipeline/Patcher.cpp
    pipeline/Extraction.h
    pipeline/Extraction.cpp
)

target_include_directories(sccore PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(sccore PUBLIC scengine Qt6::Core Qt6::Network Qt6::Concurrent)
sc_set_warnings(sccore)
