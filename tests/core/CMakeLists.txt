sc_add_test(tst_core_appinfo
    SOURCES tst_appinfo.cpp
    LIBS sccore
    LABELS unit core
)

sc_add_test(tst_core_text
    SOURCES tst_text.cpp
    LIBS sccore
    LABELS unit core
)

sc_add_test(tst_core_merge
    SOURCES tst_merge.cpp
    LIBS sccore
    LABELS unit core
)

sc_add_test(tst_core_apply
    SOURCES tst_apply.cpp
    LIBS sccore
    LABELS unit core
)

sc_add_test(tst_core_userini
    SOURCES tst_userini.cpp
    LIBS sccore
    LABELS unit core
)

sc_add_test(tst_core_settings
    SOURCES tst_settings.cpp
    LIBS sccore
    LABELS unit core
)

# Builds P4K/DCB fixtures with the engine tests' builders.
sc_add_test(tst_core_pipeline
    SOURCES tst_pipeline.cpp
    LIBS sccore libzstd_static zlibstatic
    LABELS unit core
)
target_compile_definitions(tst_core_pipeline PRIVATE SC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
