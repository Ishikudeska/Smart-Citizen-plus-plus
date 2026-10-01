#include "engine/forge/Exporter.h"

#include "engine/Parallel.h"
#include "engine/io/FileSystem.h"
#include "engine/xml/DotNetXmlWriter.h"

#include <mutex>
#include <unordered_set>

namespace engine::forge {

std::vector<std::uint32_t> selectRecords(const DataForge &forge,
                                         const std::function<bool(std::string_view)> &include)
{
    std::vector<std::uint32_t> selected;
    for (const std::uint32_t index : forge.fileRecords()) {
        if (!include || include(forge.recordFileName(index)))
            selected.push_back(index);
    }
    return selected;
}

std::filesystem::path recordOutputPath(const std::filesystem::path &outRoot, std::string_view recordPath)
{
    std::string utf8;
    xml::appendLatin1AsUtf8(utf8, recordPath);
    std::filesystem::path relative(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
    return outRoot / relative.make_preferred();
}

Result<ExportStats> exportRecords(const DataForge &forge, const std::filesystem::path &outRoot,
                                  const ExportOptions &options)
{
    const std::vector<std::uint32_t> records = selectRecords(forge, options.include);

    ExportStats stats;
    std::mutex mutex; // guards stats.failures and createdDirs
    std::unordered_set<std::filesystem::path::string_type> createdDirs;
    std::atomic<std::size_t> written{0};
    std::atomic<std::size_t> skipped{0};
    std::atomic<std::uint64_t> bytes{0};

    auto writeOne = [&](std::uint32_t index, const std::string &xml) -> Result<void> {
        const std::filesystem::path path = recordOutputPath(outRoot, forge.recordFileName(index));
        const std::filesystem::path dir = path.parent_path();
        bool needDir = false;
        {
            std::lock_guard lock(mutex);
            needDir = !createdDirs.contains(dir.native());
        }
        if (needDir) {
            if (auto ok = io::createDirectories(dir); !ok)
                return ok;
            std::lock_guard lock(mutex);
            createdDirs.insert(dir.native());
        }
        return io::writeFile(path, xml, /*atomic=*/false);
    };

    parallelFor(
        records.size(), options.threads,
        [&] {
            return [&, builder = RecordBuilder(forge, options.build), tree = xml::XmlTree(),
                    out = std::string()](std::size_t i) mutable {
                const std::uint32_t index = records[i];
                std::string failure;
                try {
                    if (!writeRecordXml(builder, tree, index, out)) {
                        skipped.fetch_add(1, std::memory_order_relaxed);
                    } else if (auto ok = writeOne(index, out); !ok) {
                        failure = ok.error().message;
                    } else {
                        written.fetch_add(1, std::memory_order_relaxed);
                        bytes.fetch_add(out.size(), std::memory_order_relaxed);
                    }
                } catch (const std::exception &e) {
                    failure = std::string(forge.recordFileName(index)) + ": " + e.what();
                }
                if (!failure.empty()) {
                    std::lock_guard lock(mutex);
                    stats.failures.push_back(std::move(failure));
                }
            };
        },
        options.progress, options.cancel);

    if (options.cancel && options.cancel->load())
        return fail(Errc::Cancelled, "cancelled");
    stats.written = written.load();
    stats.skipped = skipped.load();
    stats.bytes = bytes.load();
    return stats;
}

} // namespace engine::forge
