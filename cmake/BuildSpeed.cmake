# Build speed: precompiled headers, and the lld linker for Debug builds.

# The Qt and standard headers most files include, parsed once per target
# instead of once per file. Clang tools reading compile_commands.json
# (clazy, clang-tidy, clangd) skip GCC's precompiled headers and parse the
# headers themselves; pass them -Wno-ignored-gch, or -Werror makes the skip an
# error.
option(SC_USE_PCH "Precompile the common Qt and standard headers" ON)

set(SC_PCH_HEADERS
    <algorithm> <functional> <memory> <optional> <string> <string_view> <utility> <vector>
    <QByteArray> <QDir> <QFile> <QFileInfo> <QHash> <QList> <QMap> <QObject> <QRegularExpression>
    <QSet> <QString> <QStringList> <QVariant>
)

# sc_precompile(<target> [headers...]): the common headers plus `headers`.
function(sc_precompile target)
    if(SC_USE_PCH)
        target_precompile_headers(${target} PRIVATE ${SC_PCH_HEADERS} ${ARGN})
    endif()
endfunction()

# GNU ld takes about 50 s to link the Debug app; lld takes a few. Used when
# a working ld.lld is on PATH (LLVM, which GitHub's Windows runners have),
# else the default linker. Release builds, which ship, keep GNU ld.
option(SC_USE_LLD "Link Debug builds with lld when it is available" ON)
if(SC_USE_LLD AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # find_program looks again on each configure until it succeeds, so a
    # build picks lld up once LLVM is installed; the check's result sticks.
    find_program(SC_LLD ld.lld)
    if(SC_LLD)
        include(CheckLinkerFlag)
        check_linker_flag(CXX "-fuse-ld=lld" SC_HAVE_LLD)
        if(SC_HAVE_LLD)
            add_link_options($<$<CONFIG:Debug>:-fuse-ld=lld>)
        endif()
    endif()
endif()
