#include "engine/Version.h"

#include <pugixml.hpp>
#include <zlib.h>
#include <zstd.h>

namespace engine {

LibraryVersions libraryVersions()
{
    const int pugi = PUGIXML_VERSION; // e.g. 1150 for 1.15
    return LibraryVersions{
        .zstd = ZSTD_versionString(),
        .zlib = zlibVersion(),
        .pugixml = std::to_string(pugi / 1000) + "." + std::to_string(pugi % 1000 / 10),
    };
}

} // namespace engine
