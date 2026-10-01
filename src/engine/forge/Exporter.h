#pragma once

#include "engine/Error.h"
#include "engine/forge/DataForge.h"
#include "engine/forge/RecordBuilder.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::forge {

struct ExportOptions
{
    BuildOptions build;
    // Which record paths to write (e.g. "libs/foundry/records/entities/...");
    // all when empty.
    std::function<bool(std::string_view path)> include;
    unsigned threads = 0; // 0 = hardware concurrency
    // Called on the calling thread, roughly ten times a second.
    std::function<void(std::size_t done, std::size_t total)> progress;
    const std::atomic<bool> *cancel = nullptr;
};

struct ExportStats
{
    std::size_t written = 0;
    std::size_t skipped = 0; // rendered empty; unforge writes nothing for these
    std::uint64_t bytes = 0;
    std::vector<std::string> failures;
};

// The records unforge's Save() writes (one per distinct path, last record
// wins), filtered by `include`.
std::vector<std::uint32_t> selectRecords(const DataForge &forge,
                                         const std::function<bool(std::string_view)> &include);

// Writes each selected record to <outRoot>/<record path> in parallel, in
// unforge's exact format. Files are written in place: point outRoot at a
// staging folder and swap it in afterwards.
Result<ExportStats> exportRecords(const DataForge &forge, const std::filesystem::path &outRoot,
                                  const ExportOptions &options);

// "libs/foundry/records/a/b.xml" -> outRoot/libs/foundry/records/a/b.xml
std::filesystem::path recordOutputPath(const std::filesystem::path &outRoot, std::string_view recordPath);

} // namespace engine::forge
