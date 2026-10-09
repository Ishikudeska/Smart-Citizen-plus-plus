# Developer CLI: the ship loadout catalog over a real DataForge cache. Not shipped.
add_executable(scxloadout main.cpp)
target_link_libraries(scxloadout PRIVATE sccore)
sc_set_warnings(scxloadout)
