# Single source of truth for the application's identity.
#
# APP_NAME is the display name; it also names the settings file location
# (%APPDATA%\<APP_ORG>\) and the default data folders (Documents\<APP_NAME>\,
# %LOCALAPPDATA%\<APP_NAME>\). APP_EXE_NAME names the executable and the
# release files (<exe>-<version>-Setup.exe, which the update check looks
# for), so it stays plain ASCII: GitHub renames release assets with symbols
# such as '+'. Nothing else in the tree hard-codes the name.

set(APP_NAME     "Smart Citizen++")
set(APP_ORG      "Smart Citizen++")
set(APP_EXE_NAME "SmartCitizenPlusPlus")
set(APP_VERSION  "0.1.0")
set(APP_USER_MODEL_ID "SmartCitizenPlusPlus.App")
# GitHub "owner/repo" whose releases the in-app update check reads. Empty
# turns the check off (no network call is made).
set(APP_UPDATE_REPO  "Ishikudeska/Smart-Citizen-plus-plus")

option(APP_PORTABLE "Portable build: keep settings and data next to the executable" OFF)

if(APP_PORTABLE)
    set(APP_PORTABLE_BOOL true)
else()
    set(APP_PORTABLE_BOOL false)
endif()
