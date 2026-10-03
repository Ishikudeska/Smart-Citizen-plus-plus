cmake_minimum_required(VERSION 3.25)

include(cmake/AppIdentity.cmake)

project(ScApp VERSION ${APP_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SC_BUILD_TESTS "Build the test suite" ON)
option(SC_BUILD_TOOLS "Build developer tools (scxtool, scxgen)" ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Network Concurrent Qml Quick QuickControls2 Test QuickTest)
qt_standard_project_setup(REQUIRES 6.8)
# QML modules at the build root, where qmlls (-b <build dir>) looks for them.
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

add_subdirectory(src/engine)
add_subdirectory(src/core)
add_subdirectory(src/app)

if(SC_BUILD_TOOLS)
    add_subdirectory(tools/scxtool)
    add_subdirectory(tools/scxgen)
endif()

if(SC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
