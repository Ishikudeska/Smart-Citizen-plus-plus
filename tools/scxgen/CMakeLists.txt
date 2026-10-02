# Developer CLI: the enhancements generator over a real DataForge cache (P4 parity). Not shipped.
add_executable(scxgen main.cpp)
target_link_libraries(scxgen PRIVATE sccore)
sc_set_warnings(scxgen)
