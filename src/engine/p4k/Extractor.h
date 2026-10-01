#pragma once

#include "engine/Error.h"
#include "engine/Parallel.h"
#include "engine/p4k/Archive.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace engine::p4k {

// unp4k's command-line filter. "*.ext" means ".ext"; "*" and ".*" match
// everything; otherwise a case-insensitive substring of the entry name, so
// "global.ini" matches every language's file. A filter ending in "xml" also
// selects .dcb files.
bool unp4kFilterMatches(std::string_view filter, std::string_view name);

// Case-insensitive glob over the whole entry name: '*' matches any run of
// characters (including '/'), '?' matches one. "Data/Game*.dcb".
bool globMatches(std::string_view pattern, std::string_view name);

std::vector<std::size_t> selectEntries(const Archive &archive,
                                       const std::function<bool(std::string_view name)> &predicate);

struct ExtractOptions
{
    std::filesystem::path outputDir;
    bool skipExisting = false;
    unsigned threads = 0; // 0 = hardware concurrency
    ProgressCallback progress;
    const std::atomic<bool> *cancel = nullptr;
    // Rewrites an entry's decoded bytes before they are written (e.g.
    // CryXmlB to XML). Entries are streamed to disk when this is empty.
    std::function<Result<std::vector<std::uint8_t>>(std::string_view name, std::vector<std::uint8_t> bytes)>
        transform;
};

struct ExtractStats
{
    std::size_t extracted = 0;
    std::size_t skipped = 0; // already present (skipExisting)
    std::uint64_t bytes = 0;
    std::vector<std::string> failures;
};

// Writes each entry to <outputDir>/<entry name>, in parallel. Each file is
// written to a temporary name and renamed into place, so a failed or
// cancelled run never leaves a truncated file that skipExisting would then
// keep forever (unp4k's behaviour).
Result<ExtractStats> extract(const Archive &archive, std::span<const std::size_t> entries,
                             const ExtractOptions &options);

// Output path of an entry under `outputDir`.
std::filesystem::path entryOutputPath(const std::filesystem::path &outputDir, std::string_view name);

} // namespace engine::p4k
