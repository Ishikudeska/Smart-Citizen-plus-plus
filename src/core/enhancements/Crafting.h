#pragma once

#include "core/enhancements/Context.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <utility>
#include <vector>

// Commodities used in crafting ([CF|QDRV|...] name tags, BLUEPRINT DATA
// and Locations sections) and the Mining Compendium journal. Ports
// scan_crafting_blueprints and its helpers.
namespace core::enh {

QString normalizeCommodityName(const QString &raw);
QString humanizeCraftCategory(const QString &category);
QString craftUsageKey(const QString &categoryPath); // "" to skip
QString craftUsageLegend(const tags::TagConfig *config);
QString qdSizeRange(std::vector<int> sizes);
QStringList condenseCraftedItems(const std::vector<std::pair<QString, QString>> &items); // (category, name)
// Mining Compendium "Mineral - loc, loc" paragraphs: lower-cased mineral -> sorted locations.
QHash<QString, QStringList> parseCompendiumLocations(const QString &content);
const QStringList *lookupCommodityLocations(const QHash<QString, QStringList> &locations, const QString &display,
                                            const QString &internalName);
QString commodityTag(const tags::TagConfig *config, bool crafting, bool collection, const QStringList &usageKeys = {});

// (commodity output, journal output).
std::pair<Loc, Loc> generateCommodityJournal(const Context &ctx);

} // namespace core::enh
