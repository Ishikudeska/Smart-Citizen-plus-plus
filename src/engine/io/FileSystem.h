#pragma once

#include "engine/Error.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace engine::io {

#ifdef _WIN32
// Absolute, normalized, `\\?\`-prefixed form of `path`, so Win32 calls accept
// it past MAX_PATH without the machine-wide LongPathsEnabled policy. DataForge
// record paths run to 170 characters before the cache root is added.
std::wstring longPath(const std::filesystem::path &path);

std::string systemErrorMessage(unsigned long code);
#endif

bool exists(const std::filesystem::path &path);

// Calls `visit` with the '/'-separated path (UTF-8) of every regular file
// under `root`, relative to it. Long-path safe on Windows, where
// std::filesystem's iterators stop at MAX_PATH.
Result<void> forEachFile(const std::filesystem::path &root,
                         const std::function<void(std::string_view relativePath)> &visit);

// Removes `path` and everything under it; long-path safe. Missing is fine.
Result<void> removeAll(const std::filesystem::path &path);

// Like std::filesystem::create_directories, but long-path safe on Windows.
Result<void> createDirectories(const std::filesystem::path &dir);

// A file being written. With `atomic` the bytes go to "<target>.part" and only
// replace the target on commit(), so an interrupted write never leaves a
// truncated file behind (unp4k's skip-if-exists rule then made such files
// permanent). An uncommitted file is deleted on destruction.
class OutputFile
{
public:
    OutputFile() = default;
    OutputFile(OutputFile &&other) noexcept;
    OutputFile &operator=(OutputFile &&other) noexcept;
    OutputFile(const OutputFile &) = delete;
    OutputFile &operator=(const OutputFile &) = delete;
    ~OutputFile();

    static Result<OutputFile> create(const std::filesystem::path &target, bool atomic = true);

    Result<void> write(std::span<const std::uint8_t> bytes);
    Result<void> commit();

private:
    void discard();

#ifdef _WIN32
    void *handle_ = nullptr;
#else
    int fd_ = -1;
#endif
    std::filesystem::path target_;
    std::filesystem::path writingTo_;
};

Result<void> writeFile(const std::filesystem::path &target, std::span<const std::uint8_t> bytes,
                       bool atomic = true);

inline Result<void> writeFile(const std::filesystem::path &target, std::string_view text, bool atomic = true)
{
    return writeFile(target, std::span(reinterpret_cast<const std::uint8_t *>(text.data()), text.size()),
                     atomic);
}

} // namespace engine::io
