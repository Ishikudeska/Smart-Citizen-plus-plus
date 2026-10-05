#pragma once

#include "core/blueprints/BlueprintMeta.h"
#include "core/blueprints/OwnedItems.h"

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QString>

#include <expected>

// Owned blueprints as JSON or CSV, and reading them back. The JSON has the
// shape of SCMDB's export (version / exportedAt / missions / blueprints[])
// without SCMDB's own "tag" / "url" ids; "missions" stays empty since only
// ownership is tracked. Ports blueprint_export.py.
namespace core::blueprints {

// Items sorted case-insensitively, each under its tagged display name when
// `meta` knows it. JSON is json.dumps(indent=2) with LF line ends.
QString exportOwnedBlueprintsJson(const QSet<QString> &owned, const QMap<QString, BlueprintItem> &meta,
                                  const QDateTime &now = QDateTime::currentDateTimeUtc());
QString exportOwnedBlueprintsCsv(const QSet<QString> &owned, const QMap<QString, BlueprintItem> &meta);

// The normalized names in a .json (ours or SCMDB's) or .csv (a "name"
// column) file; an error message for anything else, malformed content or a
// CSV that is not UTF-8.
std::expected<QSet<QString>, QString> parseImportNames(const QString &path,
                                                       const Enclosings &enclosings = defaultEnclosings());

struct ImportMatch
{
    QSet<QString> matched;   // `known`'s own spelling, ready for the owned set
    QSet<QString> unmatched; // imported names with no tracker item
};
// Splits `imported` (already normalized) against the tracker's names;
// with a `catalogue`, foreign-editor names are recovered first (#372).
ImportMatch matchImportNames(const QSet<QString> &imported, const QSet<QString> &known,
                             const QSet<QString> &catalogue = {},
                             const Enclosings &enclosings = defaultEnclosings());

} // namespace core::blueprints
