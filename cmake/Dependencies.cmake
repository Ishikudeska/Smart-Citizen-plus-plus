# Third-party libraries, built from source with the same toolchain as the app.
# Pinned by URL + SHA-256 so builds are reproducible.

include(FetchContent)

# --- zstd (P4K entries are ZSTD frames) -------------------------------------
set(ZSTD_BUILD_PROGRAMS       OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS          OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_CONTRIB        OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED         OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC         ON  CACHE BOOL "" FORCE)
set(ZSTD_LEGACY_SUPPORT       OFF CACHE BOOL "" FORCE)
set(ZSTD_MULTITHREAD_SUPPORT  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zstd
    URL      https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
    SOURCE_SUBDIR build/cmake
    EXCLUDE_FROM_ALL
)

# --- zlib (raw DEFLATE entries; zip writer) ---------------------------------
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL    ON  CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
    URL      https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
    URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
    EXCLUDE_FROM_ALL
)

# --- pugixml (XML DOM + XPath for DataForge records) ------------------------
set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PUGIXML_INSTALL     OFF CACHE BOOL "" FORCE)
FetchContent_Declare(pugixml
    URL      https://github.com/zeux/pugixml/releases/download/v1.15/pugixml-1.15.tar.gz
    URL_HASH SHA256=655ade57fa703fb421c2eb9a0113b5064bddb145d415dd1f88c79353d90d511a
    EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(zstd zlib pugixml)

# zlib's CMakeLists does not attach include directories to its targets, and
# zconf.h is generated into the binary dir.
target_include_directories(zlibstatic INTERFACE
    $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
    $<BUILD_INTERFACE:${zlib_BINARY_DIR}>
)

# Third-party code is not ours to warn about.
foreach(_dep libzstd_static zlibstatic pugixml-static)
    if(TARGET ${_dep})
        set_target_properties(${_dep} PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_compile_options(${_dep} PRIVATE -w)
    endif()
endforeach()
