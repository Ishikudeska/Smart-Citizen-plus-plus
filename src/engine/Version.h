#pragma once

#include <string>

namespace engine {

// Versions of the third-party libraries linked into the engine.
struct LibraryVersions
{
    std::string zstd;
    std::string zlib;
    std::string pugixml;
};

LibraryVersions libraryVersions();

} // namespace engine
