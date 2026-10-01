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

qt_add_qml_module(scapp
    URI ScApp
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_link_libraries(scapp PRIVATE
    sccore
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
)
sc_set_warnings(scapp)
