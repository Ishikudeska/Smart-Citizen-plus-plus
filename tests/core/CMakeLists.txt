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
target_compile_definitions(tst_core_settings PRIVATE SC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# Builds P4K/DCB fixtures with the engine tests' builders.
sc_add_test(tst_core_pipeline
    SOURCES tst_pipeline.cpp
    LIBS sccore libzstd_static zlibstatic
    LABELS unit core
)
target_compile_definitions(tst_core_pipeline PRIVATE SC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

sc_add_test(tst_core_tags
    SOURCES tst_tags.cpp
    LIBS sccore
    LABELS unit core
)
target_compile_definitions(tst_core_tags PRIVATE SC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

sc_add_test(tst_core_missions
    SOURCES tst_missions.cpp
    LIBS sccore
    LABELS unit core
)

# The Kraken parity case reads Smart Citizen's own test fixture.
sc_add_test(tst_core_blueprints
    SOURCES tst_blueprints.cpp
    LIBS sccore
    LABELS unit core
)
target_compile_definitions(tst_core_blueprints PRIVATE SC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

sc_add_test(tst_core_i18n
    SOURCES tst_i18n.cpp
    LIBS sccore
    LABELS unit core
)
target_compile_definitions(tst_core_i18n PRIVATE SC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

sc_add_test(tst_core_services
    SOURCES tst_services.cpp
    LIBS sccore Qt6::Network
    LABELS unit core
)
target_compile_definitions(tst_core_services PRIVATE SC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# Replays tests/fixtures/generator_calls.json (tools/parity/gen_generator_fixtures.py).
sc_add_test(tst_core_generator
    SOURCES tst_generator.cpp
    LIBS sccore
    LABELS unit core
)
target_compile_definitions(tst_core_generator PRIVATE SC_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

sc_add_test(tst_core_stringtable
    SOURCES tst_stringtable.cpp
    LIBS sccore
    LABELS unit core
)

sc_add_test(tst_core_appupdater
    SOURCES tst_appupdater.cpp
    LIBS sccore
    LABELS unit core
)
