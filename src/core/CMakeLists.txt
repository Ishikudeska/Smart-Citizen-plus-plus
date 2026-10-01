# Application logic: settings, INI pipeline, enhancements generator, blueprints, i18n.

configure_file(AppIdentity.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/core/AppIdentity.h @ONLY)

add_library(sccore STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/AppIdentity.h
    AppInfo.h
    AppInfo.cpp
)

target_include_directories(sccore PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(sccore PUBLIC scengine Qt6::Core Qt6::Network Qt6::Concurrent)
sc_set_warnings(sccore)
