#pragma once

#include "engine/Error.h"
#include "engine/crypto/Sha256.h"
#include "engine/io/RandomAccessFile.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace engine::p4k {

// Compression methods seen in P4K archives. ZSTD is CIG's method 100.
enum class Method : std::uint16_t {
    Stored = 0,
    Deflated = 8,
    Zstd = 100,
};

// unp4k's names for the methods ("Stored", "Deflated", "ZStd").
std::string_view methodName(std::uint16_t method);

// AES-128 key CIG uses for encrypted P4K entries (zero IV, CBC, no padding).
inline constexpr std::array<std::uint8_t, 16> kCigKey = {0x5E, 0x7A, 0x20, 0x02, 0x30, 0x2E, 0xEB, 0x1A,
                                                         0x3B, 0xB6, 0x17, 0xC3, 0x0F, 0xDE, 0x1E, 0x47};

struct Entry
{
    std::uint64_t localHeaderOffset = 0;
    std::uint64_t compressedSize = 0;
    std::uint64_t uncompressedSize = 0;
    std::uint32_t nameOffset = 0;
    std::uint32_t centralOffset = 0; // this entry's record, relative to the central directory start
    std::uint32_t crc32 = 0;
    std::uint32_t dosDateTime = 0;
    std::uint16_t nameLength = 0;
    std::uint16_t method = 0;
    std::uint16_t flags = 0; // zip general-purpose flags
    bool encrypted = false;  // CIG AES (0x5002 extra field)
};

// Receives decoded bytes in order. Returning an error stops the read.
using Sink = std::function<Result<void>(std::span<const std::uint8_t>)>;
using ProgressFn = std::function<void(std::uint64_t done, std::uint64_t total)>;

struct OpenOptions
{
    ProgressFn progress; // central-directory bytes parsed so far
    const std::atomic<bool> *cancel = nullptr;
    std::array<std::uint8_t, 16> key = kCigKey;
};

// A Star Citizen P4K archive (also reads plain ZIP/ZIP64 files).
//
// The format is ZIP64 with CIG extensions: method 100 is ZSTD, and a fixed
// 206-byte extra field whose lengths include their own 4-byte headers carries
// the ZIP64 sizes (0x0001), an encryption flag (0x5002) and a SHA-256 of the
// stored bytes (0x5003). Names use backslashes; they are exposed with '/'.
//
// The central directory (443 MB for LIVE) is parsed once at open into a
// compact index. All reads are positional, so one Archive can be shared by
// any number of threads.
class Archive
{
public:
    static Result<std::shared_ptr<const Archive>> open(const std::filesystem::path &path,
                                                       const OpenOptions &options = {});

    const std::filesystem::path &path() const { return file_.path(); }
    std::uint64_t fileSize() const { return file_.size(); }

    std::size_t entryCount() const { return entries_.size(); }
    const Entry &entry(std::size_t index) const { return entries_[index]; }
    std::string_view name(std::size_t index) const;

    // Case-insensitive lookup; accepts '\' or '/'.
    std::optional<std::size_t> find(std::string_view path) const;

    // Decoded content: decrypted if needed, then decompressed. Fails with
    // Errc::Corrupt if the result is not the central directory's size.
    Result<void> read(std::size_t index, const Sink &sink) const;
    Result<std::vector<std::uint8_t>> read(std::size_t index) const;

    // The bytes as stored in the archive (before decryption).
    Result<void> readStored(std::size_t index, const Sink &sink) const;

    // SHA-256 of the stored bytes from CIG's 0x5003 field, read from the
    // central directory on demand. Empty if the entry has none.
    Result<std::optional<crypto::Sha256::Digest>> storedHash(std::size_t index) const;

private:
    Archive() = default;

    Result<void> parseDirectory(const OpenOptions &options);
    Result<std::uint64_t> dataOffset(const Entry &entry) const;
    void buildLookup() const;

    io::RandomAccessFile file_;
    std::vector<Entry> entries_;
    std::vector<char> names_;
    std::uint64_t centralDirectoryOffset_ = 0;
    std::array<std::uint8_t, 16> key_{};

    mutable std::once_flag lookupOnce_;
    mutable std::vector<std::uint32_t> lookup_; // open-addressed hash of lower-cased names
};

} // namespace engine::p4k
