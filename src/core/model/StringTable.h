#pragma once

#include "core/blueprints/OwnedItems.h"
#include "core/model/StringEntry.h"
#include "core/text/IniFile.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <utility>

// The String Editor's table logic, without Qt widgets: columns, the
// favourite/sort-order prefixes on ship names, filtering, grouped sorting
// and the preview pane's HTML. Ports string_table_model.py,
// ship_sort_prefix.py, entry_filter.py and the preview helpers of
// main_window.py.
namespace core::table {

enum Column {
    ColCategory,
    ColKey,
    ColDefault,
    ColCurrent,
    ColStar,
    ColOrder,
    ColCustom,
    ColStatus,
    ColOwned,
    ColumnCount
};

// ── favourites and ASOP sort order on ship names ───────────────────────

// The "NN" sort order after an optional favourite prefix ("*05-Avenger"),
// or "". The "-" is required, so names starting with digits (300i) never
// read as an order.
QString sortOrder(const QString &customValue, const QString &favoritePrefix);

// `customValue` with its order set to `order` ("" clears; "5" pads to "05").
// The favourite prefix stays first. Collapses to "" when the result is just
// the original name.
QString withSortOrder(const QString &customValue, const QString &originalValue, const QString &favoritePrefix,
                      const QString &order);

bool isFavorite(const StringEntry &entry, const QString &favoritePrefix);

// Adds or removes the favourite prefix (toggle_favorite). Adding needs a
// ship/vehicle name row; removing works on any row that carries the prefix
// (pre-#329 builds let other Ships rows be starred). Returns false when the
// row can't be toggled.
bool toggleFavorite(StringEntry &entry, const QString &favoritePrefix);

// Sets a user edit the way the table's inline editor does.
void setCustomValue(StringEntry &entry, const QString &value);

// ── filtering ─────────────────────────────────────────────────────────────

struct FilterCriteria
{
    std::array<QString, ColumnCount> columnText; // lower-cased substrings; empty = no filter
    QString category = QStringLiteral("All");
    QString status = QStringLiteral("All");      // statusName() or "All"
    bool hideUnmodified = false;
    bool favoritesOnly = false;
    bool shipVehicleNamesOnly = false;
    bool bpTitlesOnly = false;
    bool bpDescsOnly = false;
    QString favoritePrefix = QStringLiteral("*");
    std::optional<QString> bpHeader; // the configured "blueprints" mission header
};

// Indices of `entries` passing every active filter, in entry order.
QList<int> filterEntryIndices(const QList<StringEntry> &entries, const IniMap &defaults,
                              const FilterCriteria &criteria);

// ── sorting ───────────────────────────────────────────────────────────────

// Groups an item's name with its description, a mission's title with its
// description, and so on: (group key, 0 for the name/title, 1 for the rest).
std::pair<QString, int> groupSortKey(const QString &key);

struct OwnedState
{
    QSet<QString> blueprintItems; // names appearing in blueprint lists
    QSet<QString> owned;
    blueprints::Enclosings enclosings = blueprints::defaultEnclosings();
};

// The name an entry is matched against blueprint bullets by.
QString ownedName(const StringEntry &entry, const IniMap &defaults, const OwnedState &owned);
bool isBlueprintItem(const StringEntry &entry, const IniMap &defaults, const OwnedState &owned);
bool isOwned(const StringEntry &entry, const IniMap &defaults, const OwnedState &owned);

// Sorts `indices` by `column` like the Python's sorted(key=...), stable.
// `grouped` (Group Sort) only applies to the key column.
void sortIndices(QList<int> &indices, const QList<StringEntry> &entries, const IniMap &defaults, Column column,
                 bool descending, bool grouped, const QString &favoritePrefix, const OwnedState &owned);

// The categories offered in the filter: the standard ones plus any in use,
// sorted.
QStringList filterCategories(const QList<StringEntry> &entries);

// Tab-separated rows for "Copy Filtered".
QString filteredAsTsv(const QList<StringEntry> &entries, const QList<int> &rows);

// ── preview ───────────────────────────────────────────────────────────────

// The apply-time journal stamp an entry's preview shows, or "".
QString journalStampFor(const StringEntry &entry, const QString &appName, const QString &version);

// HTML for the preview pane: literal "\n" as line breaks, EM3 underlined,
// EM4 highlighted, ~mission(...) tokens shown as [Token].
QString previewHtml(const QString &key, const QString &raw, const QString &stamp = {});

// In-memory edits a reload must keep: key -> custom value.
QHash<QString, QString> pendingEdits(const QList<StringEntry> &entries);
int restorePendingEdits(QList<StringEntry> &entries, const QHash<QString, QString> &pending);

} // namespace core::table
