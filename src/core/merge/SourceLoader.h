#pragma once

#include "core/merge/Merger.h"
#include "core/model/StringEntry.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace core {

inline const QString kSourceGlobal = QStringLiteral("global");
inline const QString kSourceEnhancements = QStringLiteral("enhancements");
inline const QString kSourceUser = QStringLiteral("user");

// Where the sources live for the active channel and language.
struct SourceFiles
{
    QString baseIni;                // the "global" source (base.ini)
    QString enhancementsDir;        // folder holding the *_enhancements.ini files
    QStringList enhancementFileIds; // enabled files, e.g. "ship_descs" (see enhancements::files())
    QString userIni;                // the "user" source; may not exist yet
};

struct LoadedSources
{
    SourceMap sources;
    QStringList hierarchy;                         // "global", then "enhancements", then "user"
    QHash<QString, QString> enhancementCategories; // enhancement key -> table category
    QStringList problems;                          // e.g. base.ini missing
};

// Reads base.ini, the enabled enhancement INIs (combined into one
// "enhancements" source, later files winning) and user.ini (values kept
// verbatim). Ports load_sources_from_settings without the retired URL
// sources and language-overlay files.
LoadedSources loadSources(const SourceFiles &files);

// The strings table: one entry per key in the merged base sources plus any
// key only the user has, with status and category. Ports load_source_files.
// `userOverrides` replaces the "user" source when given and non-empty.
QList<StringEntry> buildEntries(const LoadedSources &loaded, const IniMap *userOverrides = nullptr);

// Status of a key whose value did not come from a user edit.
EntryStatus statusFromSource(const QString &source, const QString &baseSource, bool keyInBase,
                             bool inGlobalSource);

} // namespace core
