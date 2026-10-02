# The executable: C++ view-models exposed to the QML frontend.

qt_add_executable(scapp WIN32
    main.cpp
)
if(WIN32)
    # windres does not quote -I paths, so a space anywhere in the source path
    # breaks it. Compile the resource in its own object library with no include
    # directories and reference the manifest by absolute path.
    set(SC_APP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")
    configure_file(app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    add_library(scapp_resources OBJECT ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
    set_target_properties(scapp_resources PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_sources(scapp PRIVATE $<TARGET_OBJECTS:scapp_resources>)
endif()

set_target_properties(scapp PROPERTIES OUTPUT_NAME ${APP_EXE_NAME})

set(SC_QML_SINGLETONS
    qml/Theme.qml
)
set_source_files_properties(${SC_QML_SINGLETONS} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(scapp
    URI ScApp
    VERSION 1.0
    SOURCES
        AppController.h AppController.cpp
        BlueprintController.h BlueprintController.cpp
        ConfigController.h ConfigController.cpp
        DocsController.h DocsController.cpp
        LogModel.h LogModel.cpp
        EnhancementsController.h EnhancementsController.cpp
        ExplorerController.h ExplorerController.cpp
        P4kTreeModel.h P4kTreeModel.cpp
        PromptService.h PromptService.cpp
        StringTableModel.h StringTableModel.cpp
        TaskRunner.h TaskRunner.cpp
        TutorialController.h TutorialController.cpp
    QML_FILES
        qml/Main.qml
        ${SC_QML_SINGLETONS}
        qml/components/AppButton.qml
        qml/components/AppCheckBox.qml
        qml/components/Section.qml
        qml/components/ProgressOverlay.qml
        qml/components/PromptHost.qml
        qml/components/StatusFooter.qml
        qml/components/NavBar.qml
        qml/components/CoachMarkOverlay.qml
        qml/pages/StringsPage.qml
        qml/pages/ConfigPage.qml
        qml/pages/EnhancementsPage.qml
        qml/pages/LogPage.qml
        qml/pages/BlueprintsPage.qml
        qml/pages/ExplorerPage.qml
        qml/pages/SimplePage.qml
        qml/pages/DocsPage.qml
)

# Bundled data: UI translations, DataForge patches, docs and the tour, read from qrc.
file(GLOB_RECURSE SC_LANGUAGE_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/languages/*)
file(GLOB_RECURSE SC_PATCH_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/patches/*)
file(GLOB_RECURSE SC_DOC_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/docs/*)
qt_add_resources(scapp "bundled_data"
    PREFIX "/"
    BASE ${PROJECT_SOURCE_DIR}/resources
    FILES
        ${SC_LANGUAGE_FILES}
        ${SC_PATCH_FILES}
        ${SC_DOC_FILES}
        ${PROJECT_SOURCE_DIR}/resources/tutorial.json
)

target_include_directories(scapp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(scapp PRIVATE
    sccore
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
sc_set_warnings(scapp)

# ── install / deploy ──────────────────────────────────────────────────────
# `cmake --install <build> --prefix <dir>` produces a runnable folder: the
# exe, the Qt DLLs, plugins and QML modules windeployqt picks (Qt stays as
# DLLs, which keeps LGPL compliance simple), and the license files.
install(TARGETS scapp RUNTIME DESTINATION .)
qt_generate_deploy_qml_app_script(
    TARGET scapp
    OUTPUT_SCRIPT SC_DEPLOY_SCRIPT
    NO_TRANSLATIONS
    NO_UNSUPPORTED_PLATFORM_ERROR
    # The QML debugger plugins are only useful attached to Qt Creator.
    DEPLOY_TOOL_OPTIONS --skip-plugin-types qmltooling
)
# The Qt DLLs go beside the exe, not in bin/ (Qt's default).
install(CODE [[set(QT_DEPLOY_BIN_DIR ".")]])
install(SCRIPT ${SC_DEPLOY_SCRIPT})

install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/NOTICE DESTINATION .)
install(FILES ${zstd_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME zstd-BSD-3-Clause.txt)
install(FILES ${zlib_SOURCE_DIR}/LICENSE DESTINATION licenses RENAME zlib.txt)
install(FILES ${pugixml_SOURCE_DIR}/LICENSE.md DESTINATION licenses RENAME pugixml-MIT.txt)
# Qt's LGPLv3 text ships with the Qt installer, two levels above the kit.
get_filename_component(SC_QT_LICENSE "${QT6_INSTALL_PREFIX}/../../Licenses/LICENSE" ABSOLUTE)
if(EXISTS "${SC_QT_LICENSE}")
    install(FILES "${SC_QT_LICENSE}" DESTINATION licenses RENAME Qt-LGPL-3.0.txt)
else()
    message(WARNING "Qt license text not found at ${SC_QT_LICENSE}; add it to licenses/ by hand")
endif()
if(APP_PORTABLE)
    # Portable builds keep settings and data in data/ beside the exe.
    install(DIRECTORY DESTINATION data)
endif()
