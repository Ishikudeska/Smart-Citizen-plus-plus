#pragma once

#include "core/text/IniFile.h"

#include <QSet>
#include <QString>
#include <QStringList>

#include <map>

namespace core {

// Named sources ("global", "enhancements", "user", ...) keyed by name.
using SourceMap = std::map<QString, IniMap>;

// Merges sources in hierarchy order (later wins), then `userOverrides` on
// top, then syncs item_Name*/item_Desc* key variants. Ports
// merge_sources_by_hierarchy.
IniMap mergeSourcesByHierarchy(const SourceMap &sources, const QStringList &hierarchy,
                               const IniMap *userOverrides = nullptr);

// Makes variants of the same item key agree, e.g. item_Name_QDRV_RSI_S02_Hemera
// and item_nameQDRV_RSI_S02_Hemera_SCItem. Only item_Name*/item_Desc* keys
// take part (#255: across the whole table, unrelated keys such as Stanton2
// and Stanton_2 collide). The longest value wins, first in insertion order on
// ties, except that a user-edited variant always beats unedited ones.
void syncKeyVariants(IniMap &merged, const QSet<QString> &userEditedKeys = {});

// The form two variant keys share: "_SCItem" dropped, lower-cased,
// underscores removed, then '_' put back before component codes.
QString canonicalItemKey(const QString &key);

} // namespace core
