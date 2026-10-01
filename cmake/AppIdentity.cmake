# Single source of truth for the application's identity.
#
# APP_NAME is a working title. Changing it here renames the executable, the
# settings file location (%APPDATA%\<APP_ORG>\) and the default data folders
# (Documents\<APP_NAME>\, %LOCALAPPDATA%\<APP_NAME>\). Nothing else in the
# tree hard-codes the name.

set(APP_NAME     "SCX")
set(APP_ORG      "SCX")
set(APP_EXE_NAME "SCX")
set(APP_VERSION  "0.1.0")
set(APP_USER_MODEL_ID "SCX.App")

option(APP_PORTABLE "Portable build: keep settings and data next to the executable" OFF)

if(APP_PORTABLE)
    set(APP_PORTABLE_BOOL true)
else()
    set(APP_PORTABLE_BOOL false)
endif()
