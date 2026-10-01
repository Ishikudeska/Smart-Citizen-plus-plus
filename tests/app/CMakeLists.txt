# Launches the real executable headlessly and expects a clean exit.
add_test(NAME app_smoke COMMAND scapp --smoke-test)
set_tests_properties(app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "${SC_TEST_ENV_PATH}"
    LABELS "smoke;app"
    TIMEOUT 30
)
