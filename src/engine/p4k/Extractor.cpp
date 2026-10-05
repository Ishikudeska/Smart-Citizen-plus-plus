#include "engine/p4k/Extractor.h"

#include "engine/Try.h"
#include "engine/io/FileSystem.h"

#include <mutex>
#include <unordered_set>

namespace engine::p4k {

namespace {

inline char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

bool icontains(std::string_view haystack, std::string_view needle)
{
    if (needle.size() > haystack.size())
        return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i)
        if (iequals(haystack.substr(i, needle.size()), needle))
            return true;
    return false;
}

bool iendsWith(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && iequals(s.substr(s.size() - suffix.size()), suffix);
}

} // namespace

bool unp4kFilterMatches(std::string_view filter, std::string_view name)
{
    if (filter.starts_with("*."))
        filter.remove_prefix(1);
    if (filter == ".*" || filter == "*")
        return true;
    if (icontains(name, filter))
        return true;
    return iendsWith(filter, "xml") && iendsWith(name, ".dcb");
}

bool globMatches(std::string_view pattern, std::string_view name)
{
    // Iterative wildcard match with single-star backtracking.
    std::size_t p = 0, n = 0;
    std::size_t starP = std::string_view::npos, starN = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || lower(pattern[p]) == lower(name[n]) ||
                                   (pattern[p] == '\\' && name[n] == '/'))) {
            ++p;
            ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            starP = p++;
            starN = n;
        } else if (starP != std::string_view::npos) {
            p = starP + 1;
            n = ++starN;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

std::vector<std::size_t> selectEntries(const Archive &archive,
                                       const std::function<bool(std::string_view name)> &predicate)
{
    std::vector<std::size_t> selected;
    for (std::size_t i = 0; i < archive.entryCount(); ++i)
        if (predicate(archive.name(i)))
            selected.push_back(i);
    return selected;
}

std::filesystem::path entryOutputPath(const std::filesystem::path &outputDir, std::string_view name)
{
    std::filesystem::path relative(
        std::u8string(reinterpret_cast<const char8_t *>(name.data()), name.size()));
    return outputDir / relative.make_preferred();
}

Result<ExtractStats> extract(const Archive &archive, std::span<const std::size_t> entries,
                             const ExtractOptions &options)
{
    ExtractStats stats;
    std::mutex mutex;
    std::unordered_set<std::filesystem::path::string_type> createdDirs;
    std::atomic<std::size_t> extracted{0};
    std::atomic<std::size_t> skipped{0};
    std::atomic<std::uint64_t> bytes{0};

    auto extractOne = [&](std::size_t index) -> Result<void> {
        const std::string_view name = archive.name(index);
        const std::filesystem::path path = entryOutputPath(options.outputDir, name);
        if (options.skipExisting && io::exists(path)) {
            skipped.fetch_add(1, std::memory_order_relaxed);
            return {};
        }

        const std::filesystem::path dir = path.parent_path();
        bool needDir = false;
        {
            std::lock_guard lock(mutex);
            needDir = !createdDirs.contains(dir.native());
        }
        if (needDir) {
            SC_TRY(io::createDirectories(dir));
            std::lock_guard lock(mutex);
            createdDirs.insert(dir.native());
        }

        std::uint64_t written = 0;
        if (options.transform) {
            auto decoded = archive.read(index);
            if (!decoded)
                return std::unexpected(decoded.error());
            auto converted = options.transform(name, std::move(*decoded));
            if (!converted)
                return std::unexpected(converted.error());
            SC_TRY(io::writeFile(path, *converted));
            written = converted->size();
        } else {
            auto file = io::OutputFile::create(path);
            if (!file)
                return std::unexpected(file.error());
            SC_TRY(archive.read(index, [&](std::span<const std::uint8_t> chunk) -> Result<void> {
                written += chunk.size();
                return file->write(chunk);
            }));
            SC_TRY(file->commit());
        }
        extracted.fetch_add(1, std::memory_order_relaxed);
        bytes.fetch_add(written, std::memory_order_relaxed);
        return {};
    };

    parallelFor(
        entries.size(), options.threads,
        [&] {
            return [&](std::size_t i) {
                const std::size_t index = entries[i];
                if (auto ok = extractOne(index); !ok) {
                    std::lock_guard lock(mutex);
                    stats.failures.push_back(std::string(archive.name(index)) + ": " + ok.error().message);
                }
            };
        },
        options.progress, options.cancel);

    if (options.cancel && options.cancel->load())
        return fail(Errc::Cancelled, "cancelled");
    stats.extracted = extracted.load();
    stats.skipped = skipped.load();
    stats.bytes = bytes.load();
    return stats;
}

} // namespace engine::p4k
