#pragma once

#include "core/pipeline/Patcher.h"
#include "engine/Error.h"
#include "engine/p4k/Archive.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <optional>

namespace core {

// The DataForge subtrees (under libs/foundry/records/) the enhancements
// generator reads. Only these go into the cache. Smart Citizen's
// DATAFORGE_KEEP_SUBPATHS.
const QStringList &dataForgeKeepSubpaths();

// Identity of a Data.p4k for freshness checks. Size is what decides: the RSI
// Launcher's verification touches the file's mtime on ordinary launches,
// while a real patch always changes the size of the ~150 GB archive (#209).
struct P4kStamp
{
    qint64 size = -1;
    qint64 mtimeSecs = 0;

    static std::optional<P4kStamp> of(const QString &p4kPath);
    static std::optional<P4kStamp> read(const QString &stampFile);
    bool write(const QString &stampFile) const;
    QString key() const; // "size:mtime", stamped next to generated enhancements
};

inline const QString kP4kStampName = QStringLiteral(".p4k_stamp");

using StepProgress = std::function<void(const QString &step, qint64 done, qint64 total)>;

// The English global.ini, written to `baseIniPath` and stamped beside it.
engine::Result<void> extractBaseIni(const engine::p4k::Archive &archive, const QString &baseIniPath);

struct DataForgeExtraction
{
    std::size_t records = 0;
    PatchReport patches;
};

// Rebuilds <cacheDir>/raw/libs/foundry/records from the archive's DataForge
// database: exports the keep subtrees to a staging folder, swaps it in,
// applies the patches from `patchRoot`, then stamps the cache. A failure or
// cancel leaves the previous cache in place.
engine::Result<DataForgeExtraction> extractDataForge(const engine::p4k::Archive &archive,
                                                     const QString &cacheDir, const QString &patchRoot,
                                                     const StepProgress &progress = {},
                                                     const std::atomic<bool> *cancel = nullptr);

QString dataForgeRecordsDir(const QString &cacheDir);

// True when the file/cache was produced from this exact Data.p4k.
bool baseIniIsFresh(const QString &p4kPath, const QString &baseIniPath);
bool dataForgeCacheIsFresh(const QString &p4kPath, const QString &cacheDir);

// game_data.json (sc.gamedata's output) from the archive's DataForge
// database. `baseIniPath` resolves display names (empty: prettified ids);
// `overlayPath` backfills vehicle physics from an earlier file; an empty
// `channel` is left out of the JSON. Returns the pass summary lines.
engine::Result<QStringList> exportGameData(const engine::p4k::Archive &archive, const QString &outputPath,
                                           const QString &baseIniPath, const QString &overlayPath = {},
                                           const QString &channel = {}, const StepProgress &progress = {});

} // namespace core
