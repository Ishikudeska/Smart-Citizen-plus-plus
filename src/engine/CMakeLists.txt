# Format library: P4K archives, DataForge, CryXML, game-data export.
# Deliberately Qt-free so it can be tested and reused without an event loop.

add_library(scengine STATIC
    Version.h
    Version.cpp
)

target_include_directories(scengine PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(scengine
    PUBLIC  pugixml::static
    PRIVATE libzstd_static zlibstatic
)
set_target_properties(scengine PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
sc_set_warnings(scengine)
