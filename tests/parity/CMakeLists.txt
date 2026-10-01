# Parity against the original Python/.NET tools' output on a real install.
# Opt in: set SCX_P4K (see tst_parity_engine.cpp), then
#   ctest --preset mingw-release -L parity
sc_add_test(tst_parity_engine
    SOURCES tst_parity_engine.cpp
    LIBS scengine Qt6::Core
    LABELS parity
)
sc_add_test(tst_parity_core
    SOURCES tst_parity_core.cpp
    LIBS sccore
    LABELS parity
)
target_compile_definitions(tst_parity_core PRIVATE SC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# The opt-in full sweep (SCX_PARITY_FULL=1) reads the whole 158 GB archive
# twice; lift QtTest's 5-minute per-function limit and CTest's timeout.
set_tests_properties(tst_parity_engine PROPERTIES
    TIMEOUT 10800
    ENVIRONMENT "${SC_TEST_ENV};QTEST_FUNCTION_TIMEOUT=10800000"
)
