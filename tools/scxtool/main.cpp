// scxtool: developer CLI over the engine, for parity runs and timing.
// Not shipped with the app.

#include "engine/Parallel.h"
#include "engine/cryxml/CryXml.h"
#include "engine/forge/DataForge.h"
#include "engine/forge/Exporter.h"
#include "engine/forge/RecordBuilder.h"
#include "engine/gamedata/GameData.h"
#include "engine/io/FileSystem.h"
#include "engine/io/RandomAccessFile.h"
#include "engine/p4k/Archive.h"
#include "engine/p4k/Extractor.h"

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace engine;

namespace {

using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

int usage()
{
    std::fputs("usage:\n"
               "  scxtool info <archive>\n"
               "  scxtool list <archive> [substring]\n"
               "  scxtool cat  <archive> <entry> [out-file]\n"
               "  scxtool verify <archive> [every-nth=100]\n"
               "  scxtool extract <archive> <filter> <out-dir> [--glob] [--xml]\n"
               "  scxtool cryxml <archive> <entry>\n"
               "  scxtool cryxml-check <archive> [every-nth=1]\n"
               "  scxtool forge <archive|dcb> <out-dir> [subpath...]\n"
               "  scxtool forge-compare <archive|dcb> <unforge-root> [subpath...]\n"
               "\n"
               "subpaths are under libs/foundry/records/; forge-compare defaults to\n"
               "Smart Citizen's 14 cached subtrees.\n",
               stderr);
    return 2;
}

std::filesystem::path toPath(const char *utf8)
{
    return std::filesystem::path(reinterpret_cast<const char8_t *>(utf8));
}

std::shared_ptr<const p4k::Archive> openArchive(const char *path)
{
    const auto start = Clock::now();
    auto archive = p4k::Archive::open(toPath(path));
    if (!archive) {
        std::fprintf(stderr, "error: %s\n", archive.error().message.c_str());
        return nullptr;
    }
    std::fprintf(stderr, "opened %zu entries in %.2fs\n", (*archive)->entryCount(), secondsSince(start));
    return *archive;
}

bool containsIgnoreCase(std::string_view haystack, std::string_view needle)
{
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; };
    if (needle.empty())
        return true;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        std::size_t k = 0;
        while (k < needle.size() && lower(haystack[i + k]) == lower(needle[k]))
            ++k;
        if (k == needle.size())
            return true;
    }
    return false;
}

bool endsWithIgnoreCase(std::string_view s, std::string_view suffix)
{
    if (s.size() < suffix.size())
        return false;
    return containsIgnoreCase(s.substr(s.size() - suffix.size()), suffix);
}

int cmdInfo(const char *path)
{
    auto archive = openArchive(path);
    if (!archive)
        return 1;
    std::uint64_t stored = 0, deflated = 0, zstd = 0, other = 0, encrypted = 0, total = 0;
    for (std::size_t i = 0; i < archive->entryCount(); ++i) {
        const auto &e = archive->entry(i);
        total += e.uncompressedSize;
        switch (e.method) {
        case 0:
            ++stored;
            break;
        case 8:
            ++deflated;
            break;
        case 100:
            ++zstd;
            break;
        default:
            ++other;
            break;
        }
        encrypted += e.encrypted ? 1 : 0;
    }
    std::printf("entries:    %zu\nstored:     %llu\ndeflated:   %llu\nzstd:       %llu\nother:      %llu\n"
                "encrypted:  %llu\nunpacked:   %.1f GB\n",
                archive->entryCount(), (unsigned long long)stored, (unsigned long long)deflated,
                (unsigned long long)zstd, (unsigned long long)other, (unsigned long long)encrypted,
                double(total) / 1e9);
    return 0;
}

int cmdList(const char *path, const char *filter)
{
    auto archive = openArchive(path);
    if (!archive)
        return 1;
    for (std::size_t i = 0; i < archive->entryCount(); ++i) {
        const auto name = archive->name(i);
        if (!containsIgnoreCase(name, filter ? filter : ""))
            continue;
        const auto &e = archive->entry(i);
        std::printf("%-8s %-5s %12llu %12llu %08x  %.*s\n", std::string(p4k::methodName(e.method)).c_str(),
                    e.encrypted ? "Crypt" : "Plain", (unsigned long long)e.uncompressedSize,
                    (unsigned long long)e.compressedSize, e.crc32, int(name.size()), name.data());
    }
    return 0;
}

int cmdCat(const char *path, const char *entryName, const char *outPath)
{
    auto archive = openArchive(path);
    if (!archive)
        return 1;
    const auto index = archive->find(entryName);
    if (!index) {
        std::fprintf(stderr, "error: no entry %s\n", entryName);
        return 1;
    }
    std::FILE *out = outPath ? std::fopen(outPath, "wb") : stdout;
    if (!out) {
        std::fprintf(stderr, "error: cannot write %s\n", outPath);
        return 1;
    }
    const auto start = Clock::now();
    auto ok = archive->read(*index, [out](std::span<const std::uint8_t> bytes) -> Result<void> {
        if (std::fwrite(bytes.data(), 1, bytes.size(), out) != bytes.size())
            return fail(Errc::Io, "write failed");
        return {};
    });
    if (outPath)
        std::fclose(out);
    if (!ok) {
        std::fprintf(stderr, "error: %s\n", ok.error().message.c_str());
        return 1;
    }
    std::fprintf(stderr, "decoded %llu bytes in %.2fs\n",
                 (unsigned long long)archive->entry(*index).uncompressedSize, secondsSince(start));
    return 0;
}

// Decodes entries and checks them against the archive's own records: the
// stored bytes against the 0x5003 SHA-256, the decoded size against the
// central directory. `every` = 1 checks all entries; encrypted ones are always
// checked.
int cmdVerify(const char *path, std::size_t every)
{
    auto archive = openArchive(path);
    if (!archive)
        return 1;
    const auto start = Clock::now();
    std::size_t checked = 0, failures = 0, noHash = 0;
    std::uint64_t bytes = 0;
    for (std::size_t i = 0; i < archive->entryCount(); ++i) {
        const auto &e = archive->entry(i);
        if (!e.encrypted && i % every != 0)
            continue;
        ++checked;
        const auto name = archive->name(i);

        crypto::Sha256 sha;
        auto stored = archive->readStored(i, [&sha](std::span<const std::uint8_t> b) -> Result<void> {
            sha.update(b);
            return {};
        });
        auto expected = archive->storedHash(i);
        if (!stored || !expected) {
            ++failures;
            std::printf("FAIL read %.*s: %s\n", int(name.size()), name.data(),
                        (!stored ? stored.error() : expected.error()).message.c_str());
            continue;
        }
        if (!*expected)
            ++noHash;
        else if (sha.finish() != **expected) {
            ++failures;
            std::printf("FAIL sha256 %.*s\n", int(name.size()), name.data());
        }

        auto decoded = archive->read(i, [&bytes](std::span<const std::uint8_t> b) -> Result<void> {
            bytes += b.size();
            return {};
        });
        if (!decoded) {
            ++failures;
            std::printf("FAIL decode %.*s: %s\n", int(name.size()), name.data(),
                        decoded.error().message.c_str());
        }
    }
    std::printf("checked %zu entries (%.1f GB decoded) in %.1fs: %zu failures, %zu without a hash\n", checked,
                double(bytes) / 1e9, secondsSince(start), failures, noHash);
    return failures ? 1 : 0;
}

// unp4k-style extraction: `filter` uses unp4k's rules unless --glob.
int cmdExtract(int argc, char **argv)
{
    auto archive = openArchive(argv[2]);
    if (!archive)
        return 1;
    const std::string filter = argv[3];
    bool glob = false, toXml = false;
    for (int i = 5; i < argc; ++i) {
        glob |= std::string_view(argv[i]) == "--glob";
        toXml |= std::string_view(argv[i]) == "--xml";
    }
    const auto entries = p4k::selectEntries(*archive, [&](std::string_view name) {
        return glob ? p4k::globMatches(filter, name) : p4k::unp4kFilterMatches(filter, name);
    });
    std::fprintf(stderr, "%zu entries match\n", entries.size());

    p4k::ExtractOptions options;
    options.outputDir = toPath(argv[4]);
    options.progress = [](std::size_t done, std::size_t total) {
        std::fprintf(stderr, "\r%zu / %zu", done, total);
    };
    if (toXml) {
        options.transform = [](std::string_view,
                               std::vector<std::uint8_t> bytes) -> Result<std::vector<std::uint8_t>> {
            if (!cryxml::isCryXml(bytes))
                return bytes;
            auto text = cryxml::toXml(bytes);
            if (!text)
                return std::unexpected(text.error());
            return std::vector<std::uint8_t>(text->begin(), text->end());
        };
    }
    const auto start = Clock::now();
    auto stats = p4k::extract(*archive, entries, options);
    std::fputc('\n', stderr);
    if (!stats) {
        std::fprintf(stderr, "error: %s\n", stats.error().message.c_str());
        return 1;
    }
    std::printf("extracted %zu files (%.1f MB), skipped %zu, %zu failures, in %.2fs\n", stats->extracted,
                double(stats->bytes) / 1048576.0, stats->skipped, stats->failures.size(),
                secondsSince(start));
    for (std::size_t i = 0; i < std::min<std::size_t>(stats->failures.size(), 20); ++i)
        std::printf("  %s\n", stats->failures[i].c_str());
    return stats->failures.empty() ? 0 : 1;
}

int cmdCryXml(const char *path, const char *entryName)
{
    auto archive = openArchive(path);
    if (!archive)
        return 1;
    const auto index = archive->find(entryName);
    if (!index) {
        std::fprintf(stderr, "error: no entry %s\n", entryName);
        return 1;
    }
    auto bytes = archive->read(*index);
    if (!bytes) {
        std::fprintf(stderr, "error: %s\n", bytes.error().message.c_str());
        return 1;
    }
    auto text = cryxml::toXml(*bytes);
    if (!text) {
        std::fprintf(stderr, "error: %s\n", text.error().message.c_str());
        return 1;
    }
    std::fwrite(text->data(), 1, text->size(), stdout);
    return 0;
}

// Converts every CryXml entry and checks that the result is well-formed XML.
int cmdCryXmlCheck(const char *path, std::size_t every)
{
    auto archive = openArchive(path);
    if (!archive)
        return 1;
    // Extensions CryEngine stores as XML (binary or text).
    static const std::vector<std::string_view> kXmlExtensions = {
        ".xml",  ".mtl",        ".chrparams", ".cdf", ".adb", ".bspace",
        ".comb", ".animevents", ".entxml",    ".rmp", ".lyr"};
    std::vector<std::size_t> xmlEntries;
    for (std::size_t i = 0; i < archive->entryCount(); ++i) {
        const auto name = archive->name(i);
        if (std::any_of(kXmlExtensions.begin(), kXmlExtensions.end(),
                        [&](std::string_view ext) { return endsWithIgnoreCase(name, ext); }))
            xmlEntries.push_back(i);
    }
    std::vector<std::size_t> candidates;
    for (std::size_t k = 0; k < xmlEntries.size(); k += std::max<std::size_t>(every, 1))
        candidates.push_back(xmlEntries[k]);
    std::fprintf(stderr, "%zu of %zu XML-type entries\n", candidates.size(), xmlEntries.size());

    std::mutex mutex;
    std::vector<std::string> failures;
    std::atomic<std::size_t> converted{0}, plain{0}, other{0};
    const auto start = Clock::now();
    parallelFor(
        candidates.size(), 0,
        [&] {
            return [&](std::size_t n) {
                const std::size_t i = candidates[n];
                auto bytes = archive->read(i);
                if (!bytes) {
                    std::lock_guard lock(mutex);
                    failures.push_back(std::string(archive->name(i)) + ": " + bytes.error().message);
                    return;
                }
                if (!cryxml::isCryXml(*bytes)) {
                    (cryxml::isPlainXml(*bytes) ? plain : other).fetch_add(1);
                    return;
                }
                auto text = cryxml::toXml(*bytes);
                pugi::xml_document doc;
                std::string problem;
                if (!text)
                    problem = text.error().message;
                else if (auto r = doc.load_buffer(text->data(), text->size()); !r)
                    problem = std::string("not well-formed: ") + r.description();
                if (!problem.empty()) {
                    std::lock_guard lock(mutex);
                    failures.push_back(std::string(archive->name(i)) + ": " + problem);
                } else {
                    converted.fetch_add(1);
                }
            };
        },
        nullptr, nullptr);
    std::printf("CryXml: %zu converted, %zu failed; plain XML %zu; other %zu; in %.1fs\n", converted.load(),
                failures.size(), plain.load(), other.load(), secondsSince(start));
    std::sort(failures.begin(), failures.end());
    for (std::size_t i = 0; i < std::min<std::size_t>(failures.size(), 20); ++i)
        std::printf("  %s\n", failures[i].c_str());
    return failures.empty() ? 0 : 1;
}

// --- DataForge ---------------------------------------------------------------

constexpr std::string_view kRecordsRoot = "libs/foundry/records/";

// Smart Citizen's DATAFORGE_KEEP_SUBPATHS (src/utils/pak_extractor.py).
const std::vector<std::string> kKeepSubpaths = {
    "entities/scitem",
    "entities/spaceships",
    "entities/missions",
    "entities/contracts",
    "entities/jobterminal",
    "contracts/contractgenerator",
    "contracts/contracttemplates",
    "crafting/blueprintrewards",
    "crafting/blueprints/crafting",
    "missionbroker/pu_missions",
    "ammoparams/vehicle",
    "ammoparams/fps",
    "reputation/rewards/missionrewards_reputation",
    "reputation/standings",
};

std::optional<std::vector<std::uint8_t>> readWholeFile(const std::filesystem::path &path)
{
    auto file = io::RandomAccessFile::open(path);
    if (!file)
        return std::nullopt;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file->size()));
    if (!file->readAt(0, bytes))
        return std::nullopt;
    return bytes;
}

std::optional<forge::DataForge> loadForge(const char *source)
{
    std::vector<std::uint8_t> bytes;
    auto start = Clock::now();
    if (endsWithIgnoreCase(source, ".dcb")) {
        auto data = readWholeFile(toPath(source));
        if (!data) {
            std::fprintf(stderr, "error: cannot read %s\n", source);
            return std::nullopt;
        }
        bytes = std::move(*data);
    } else {
        auto archive = openArchive(source);
        if (!archive)
            return std::nullopt;
        std::optional<std::size_t> dcb;
        for (std::size_t i = 0; i < archive->entryCount() && !dcb; ++i)
            if (endsWithIgnoreCase(archive->name(i), ".dcb"))
                dcb = i;
        if (!dcb) {
            std::fprintf(stderr, "error: no .dcb in %s\n", source);
            return std::nullopt;
        }
        start = Clock::now();
        auto data = archive->read(*dcb);
        if (!data) {
            std::fprintf(stderr, "error: %s\n", data.error().message.c_str());
            return std::nullopt;
        }
        std::fprintf(stderr, "read %.*s (%zu bytes) in %.2fs\n", int(archive->name(*dcb).size()),
                     archive->name(*dcb).data(), data->size(), secondsSince(start));
        bytes = std::move(*data);
    }
    start = Clock::now();
    auto df = forge::DataForge::load(std::move(bytes));
    if (!df) {
        std::fprintf(stderr, "error: %s\n", df.error().message.c_str());
        return std::nullopt;
    }
    std::fprintf(stderr,
                 "DataForge v%d: %zu structs, %zu properties, %zu records, %zu files; loaded in %.2fs\n",
                 df->version(), df->structs().size(), df->properties().size(), df->records().size(),
                 df->fileRecords().size(), secondsSince(start));
    return std::move(*df);
}

std::vector<std::string> subpathPrefixes(int argc, char **argv, int first, bool defaultToKeepList)
{
    std::vector<std::string> prefixes;
    for (int i = first; i < argc; ++i)
        prefixes.push_back(std::string(kRecordsRoot) + argv[i] + "/");
    if (prefixes.empty() && defaultToKeepList)
        for (const auto &s : kKeepSubpaths)
            prefixes.push_back(std::string(kRecordsRoot) + s + "/");
    return prefixes;
}

bool underAny(std::string_view path, const std::vector<std::string> &prefixes)
{
    if (prefixes.empty())
        return true;
    return std::any_of(prefixes.begin(), prefixes.end(),
                       [path](const std::string &p) { return path.starts_with(p); });
}

int cmdForge(int argc, char **argv)
{
    auto df = loadForge(argv[2]);
    if (!df)
        return 1;
    const auto prefixes = subpathPrefixes(argc, argv, 4, false);
    forge::ExportOptions options;
    options.include = [&prefixes](std::string_view path) { return underAny(path, prefixes); };
    options.progress = [](std::size_t done, std::size_t total) {
        std::fprintf(stderr, "\r%zu / %zu", done, total);
    };
    const auto start = Clock::now();
    auto stats = forge::exportRecords(*df, toPath(argv[3]), options);
    std::fputc('\n', stderr);
    if (!stats) {
        std::fprintf(stderr, "error: %s\n", stats.error().message.c_str());
        return 1;
    }
    std::printf("wrote %zu files (%.1f MB), skipped %zu empty, %zu failures, in %.2fs\n", stats->written,
                double(stats->bytes) / 1048576.0, stats->skipped, stats->failures.size(),
                secondsSince(start));
    for (std::size_t i = 0; i < std::min<std::size_t>(stats->failures.size(), 20); ++i)
        std::printf("  %s\n", stats->failures[i].c_str());
    return stats->failures.empty() ? 0 : 1;
}

// unforge's output has a JIT-dependent extra stack frame in a few of its
// null-pointer error messages; drop it so both forms compare equal.
void normalizeUnforgeOutput(std::string &text)
{
    static constexpr std::string_view kExtraFrame =
        "&#xD;&#xA;   at unforge.DataForge.ReadDataMappingAtIndex(Int64 index)";
    for (std::size_t pos = text.find(kExtraFrame); pos != std::string::npos;
         pos = text.find(kExtraFrame, pos))
        text.erase(pos, kExtraFrame.size());
}

std::string firstDifference(const std::string &expected, const std::string &actual)
{
    std::size_t line = 1, lineStart = 0, i = 0;
    while (i < expected.size() && i < actual.size() && expected[i] == actual[i]) {
        if (expected[i] == '\n') {
            ++line;
            lineStart = i + 1;
        }
        ++i;
    }
    auto lineAt = [lineStart](const std::string &s) {
        const auto end = s.find('\n', lineStart);
        return s.substr(lineStart,
                        std::min<std::size_t>(end == std::string::npos ? s.size() : end, lineStart + 300) -
                            lineStart);
    };
    return "line " + std::to_string(line) + "\n      expected: " + lineAt(expected) +
           "\n      actual:   " + lineAt(actual);
}

int cmdForgeCompare(int argc, char **argv)
{
    auto df = loadForge(argv[2]);
    if (!df)
        return 1;
    const std::filesystem::path root = toPath(argv[3]);
    const auto prefixes = subpathPrefixes(argc, argv, 4, true);

    // Every cached file under the chosen subtrees.
    std::vector<std::string> cached;
    auto listed = io::forEachFile(root, [&](std::string_view path) {
        if (underAny(path, prefixes) && path.ends_with(".xml"))
            cached.emplace_back(path);
    });
    if (!listed) {
        std::fprintf(stderr, "error: %s\n", listed.error().message.c_str());
        return 1;
    }
    std::sort(cached.begin(), cached.end());
    std::fprintf(stderr, "%zu cached files to compare\n", cached.size());

    std::mutex mutex;
    std::vector<std::string> mismatches, noRecord, notWritten;
    std::atomic<std::size_t> next{0}, matched{0};
    const auto start = Clock::now();
    auto worker = [&] {
        forge::RecordBuilder builder(*df);
        xml::XmlTree tree;
        std::string actual;
        for (std::size_t i; (i = next.fetch_add(1)) < cached.size();) {
            const std::string &path = cached[i];
            const auto index = df->recordByPath(path);
            if (!index) {
                std::lock_guard lock(mutex);
                noRecord.push_back(path);
                continue;
            }
            if (!forge::writeRecordXml(builder, tree, *index, actual)) {
                std::lock_guard lock(mutex);
                notWritten.push_back(path);
                continue;
            }
            auto bytes =
                readWholeFile(root / std::filesystem::path(std::u8string(
                                         reinterpret_cast<const char8_t *>(path.data()), path.size())));
            std::string expected(bytes ? reinterpret_cast<const char *>(bytes->data()) : "",
                                 bytes ? bytes->size() : 0);
            normalizeUnforgeOutput(expected);
            if (expected == actual) {
                matched.fetch_add(1);
            } else {
                std::lock_guard lock(mutex);
                mismatches.push_back(path + "\n      " + firstDifference(expected, actual));
            }
        }
    };
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < std::max(1u, std::thread::hardware_concurrency()); ++t)
        threads.emplace_back(worker);
    for (auto &t : threads)
        t.join();

    // Records the cache should have but doesn't.
    std::vector<std::string> missing;
    std::vector<std::string_view> sortedCached(cached.begin(), cached.end());
    for (const auto index : df->fileRecords()) {
        const auto path = df->recordFileName(index);
        if (underAny(path, prefixes) && !std::binary_search(sortedCached.begin(), sortedCached.end(), path)) {
            forge::RecordBuilder builder(*df);
            xml::XmlTree tree;
            std::string out;
            if (forge::writeRecordXml(builder, tree, index, out))
                missing.emplace_back(path);
        }
    }

    std::sort(mismatches.begin(), mismatches.end());
    std::printf("compared %zu files in %.2fs: %zu identical, %zu different, %zu without a record, "
                "%zu the C++ skips, %zu records missing from the cache\n",
                cached.size(), secondsSince(start), matched.load(), mismatches.size(), noRecord.size(),
                notWritten.size(), missing.size());
    auto show = [](const char *title, const std::vector<std::string> &items) {
        if (items.empty())
            return;
        std::printf("%s:\n", title);
        for (std::size_t i = 0; i < std::min<std::size_t>(items.size(), 15); ++i)
            std::printf("  %s\n", items[i].c_str());
        if (items.size() > 15)
            std::printf("  ... and %zu more\n", items.size() - 15);
    };
    show("different", mismatches);
    show("no record", noRecord);
    show("skipped by C++", notWritten);
    show("missing from cache", missing);
    return mismatches.empty() && noRecord.empty() && notWritten.empty() && missing.empty() ? 0 : 1;
}

// sc.gamedata's CLI: game_data.json from the archive's (or a) Game2.dcb.
int cmdGameData(int argc, char **argv)
{
    gamedata::Options options;
    std::string output = "game_data.json";
    for (int i = 3; i + 1 < argc; i += 2) {
        const std::string_view flag = argv[i];
        if (flag == "--base-ini")
            options.baseIniPath = argv[i + 1];
        else if (flag == "--output")
            output = argv[i + 1];
        else if (flag == "--overlay")
            options.overlayPath = argv[i + 1];
        else if (flag == "--channel")
            options.channel = argv[i + 1];
        else if (flag == "--generated-at")
            options.generatedAt = argv[i + 1];
        else
            return usage();
    }
    const auto df = loadForge(argv[2]);
    if (!df)
        return 1;
    const auto start = Clock::now();
    const auto json = gamedata::buildGameDataJson(
        *df, options, [](const std::string &line) { std::fprintf(stderr, "%s\n", line.c_str()); });
    if (!json) {
        std::fprintf(stderr, "error: %s\n", json.error().message.c_str());
        return 1;
    }
    std::FILE *f = _wfopen(toPath(output.c_str()).c_str(), L"wb");
    if (!f) {
        std::fprintf(stderr, "error: cannot write %s\n", output.c_str());
        return 1;
    }
    std::fwrite(json->data(), 1, json->size(), f);
    std::fclose(f);
    std::fprintf(stderr, "Wrote %s (%.1f KiB) in %.2fs\n", output.c_str(), json->size() / 1024.0,
                 secondsSince(start));
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
        return usage();
    const std::string_view cmd = argv[1];
    if (cmd == "info")
        return cmdInfo(argv[2]);
    if (cmd == "verify")
        return cmdVerify(argv[2], argc > 3 ? std::stoul(argv[3]) : 100);
    if (cmd == "list")
        return cmdList(argv[2], argc > 3 ? argv[3] : nullptr);
    if (cmd == "cat" && argc >= 4)
        return cmdCat(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    if (cmd == "extract" && argc >= 5)
        return cmdExtract(argc, argv);
    if (cmd == "cryxml" && argc >= 4)
        return cmdCryXml(argv[2], argv[3]);
    if (cmd == "cryxml-check")
        return cmdCryXmlCheck(argv[2], argc > 3 ? std::stoul(argv[3]) : 1);
    if (cmd == "forge" && argc >= 4)
        return cmdForge(argc, argv);
    if (cmd == "forge-compare" && argc >= 4)
        return cmdForgeCompare(argc, argv);
    if (cmd == "gamedata")
        return cmdGameData(argc, argv);
    return usage();
}
