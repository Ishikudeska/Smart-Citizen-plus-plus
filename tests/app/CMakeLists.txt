# Launches the real executable headlessly and expects a clean exit.
add_test(NAME app_smoke COMMAND scapp --smoke-test)
set_tests_properties(app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;${SC_TEST_ENV}"
    ENVIRONMENT_MODIFICATION "${SC_TEST_ENV_PATH}"
    LABELS "smoke;app"
    TIMEOUT 30
)

# The app layer is an executable, so its units compile straight into the test.
sc_add_test(tst_app_taskrunner
    SOURCES tst_taskrunner.cpp ${PROJECT_SOURCE_DIR}/src/app/TaskRunner.cpp ${PROJECT_SOURCE_DIR}/src/app/TaskRunner.h
    LIBS sccore Qt6::Qml
    LABELS unit app
)
target_include_directories(tst_app_taskrunner PRIVATE ${PROJECT_SOURCE_DIR}/src/app)

sc_add_test(tst_app_stringtablemodel
    SOURCES tst_stringtablemodel.cpp ${PROJECT_SOURCE_DIR}/src/app/StringTableModel.cpp ${PROJECT_SOURCE_DIR}/src/app/StringTableModel.h
    LIBS sccore Qt6::Qml Qt6::Gui
    LABELS unit app
)
target_include_directories(tst_app_stringtablemodel PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
