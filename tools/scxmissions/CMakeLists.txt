# Developer CLI: the mission catalog over a real DataForge cache. Not shipped.
add_executable(scxmissions main.cpp)
target_link_libraries(scxmissions PRIVATE sccore)
sc_set_warnings(scxmissions)
# The app reads its DataForge patches from qrc; the tool, from the source tree.
target_compile_definitions(scxmissions PRIVATE SC_PATCHES_DIR="${PROJECT_SOURCE_DIR}/resources/patches")
