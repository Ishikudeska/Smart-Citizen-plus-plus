# Developer CLI over the engine (parity runs, timing). Not shipped.
add_executable(scxtool main.cpp)
target_link_libraries(scxtool PRIVATE scengine)
set_target_properties(scxtool PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
sc_set_warnings(scxtool)
