#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <memory>

// The DataForge XML cache (<forge>/raw/libs/foundry/records), listed once.
// Lists come back in the order the Python generator saw files, because when
// two records share a loc key the last one wins: pathlib's rglob order
// (a directory's own files when its parent is scanned, its subdirectories
// later from a LIFO stack), with each directory's files sorted by path for
// the cached index. Directory listings follow NTFS's order (upper-cased
// UTF-16 compare). Paths are absolute with forward slashes.
namespace core::enh {

class RecordStore
{
public:
    // Lists every *.xml under `recordsDir`; nothing when it doesn't exist.
    static std::shared_ptr<const RecordStore> scan(const QString &recordsDir);

    const QString &recordsDir() const { return root_; }
    QString absolute(const QString &relative) const;
    bool dirExists(const QString &relative) const;
    qsizetype fileCount() const { return fileCount_; }

    // _index_rglob(xml_path_index, records/<relDir>, records).
    QStringList indexRglob(const QString &relDir) const;
    // The index's own list for exactly records/<relDir>.
    QStringList filesIn(const QString &relDir) const;
    // (records/<relDir>).rglob("*.xml").
    QStringList rglob(const QString &relDir) const;
    // (records/<relDir>).glob(pattern) with a "*"/"?" wildcard on file names.
    QStringList glob(const QString &relDir, const QString &pattern) const;

    // NTFS directory order for two names.
    static bool ntfsLess(const QString &a, const QString &b);

private:
    struct Dir
    {
        QStringList subdirs; // NTFS order
        QStringList files;   // *.xml names, NTFS order
    };

    // Directories in the order rglob yields their files, starting at `rel`.
    QStringList walkOrder(const QString &rel) const;

    QString root_;
    QHash<QString, Dir> dirs_;          // "" is the records root; '/'-separated
    QStringList indexOrder_;            // directories with files, in the index's key order
    qsizetype fileCount_ = 0;
};

} // namespace core::enh
