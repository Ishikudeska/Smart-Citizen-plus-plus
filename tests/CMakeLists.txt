# Test suite (Qt Test + CTest).
#
#   ctest --preset mingw-debug                 all unit tests
#   ctest --preset mingw-debug -L parity       parity tests (need SCX_P4K, see tests/parity)

# Runtime DLL directories, so tests run from the build tree without deployment.
get_filename_component(SC_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(SC_QT_BIN "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
set(SC_TEST_ENV_PATH
    "PATH=path_list_prepend:${SC_QT_BIN}"
    "PATH=path_list_prepend:${SC_COMPILER_BIN}"
)
# On Windows Qt sends QtTest's text log and qDebug/qInfo to the debugger, not
# stdout, when the process has no console (as under CTest); without this,
# --output-on-failure shows nothing.
set(SC_TEST_ENV "QT_FORCE_STDERR_LOGGING=1")

# sc_add_test(<name> SOURCES <files...> LIBS <targets...> [LABELS <labels...>])
function(sc_add_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS;LABELS" ${ARGN})
    add_executable(${name} ${T_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${T_LIBS})
    sc_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "${SC_TEST_ENV}"
        ENVIRONMENT_MODIFICATION "${SC_TEST_ENV_PATH}"
        LABELS "${T_LABELS}"
    )
endfunction()

add_subdirectory(engine)
add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(parity)
