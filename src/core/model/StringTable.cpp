#include "core/model/StringTable.h"

#include "core/text/PyText.h"

#include <QRegularExpression>

#include <algorithm>
#include <numeric>
#include <tuple>
#include <vector>

namespace core::table {

namespace {

const QRegularExpression &orderRe()
{
    static const QRegularExpression re(QStringLiteral(R"(\A(\d{2})-)"),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

bool isMission(const StringEntry &e)
{
    return e.category == category::kMissions;
}

} // namespace

QString sortOrder(const QString &customValue, const QString &favoritePrefix)
{
    QStringView body = customValue;
    if (!favoritePrefix.isEmpty() && body.startsWith(favoritePrefix))
        body = body.sliced(favoritePrefix.size());
    const auto m = orderRe().matchView(body);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString withSortOrder(const QString &customValue, const QString &originalValue, const QString &favoritePrefix,
                      const QString &order)
{
    const bool hasFav = !favoritePrefix.isEmpty() && customValue.startsWith(favoritePrefix);
    QString body = hasFav ? customValue.sliced(favoritePrefix.size()) : customValue;
    if (const auto m = orderRe().match(body); m.hasMatch())
        body = body.sliced(m.capturedEnd());
    const QString base = body.isEmpty() ? originalValue : body;

    QString normalized = order;
    static const QRegularExpression twoDigits(QStringLiteral(R"(\A\d{2}\z)"),
                                              QRegularExpression::UseUnicodePropertiesOption);
    if (!normalized.isEmpty() && !twoDigits.match(normalized).hasMatch()) {
        bool ok = false;
        const int n = py::strip(normalized).toInt(&ok);
        if (!ok)
            return customValue; // int() would raise; the editor rejects it
        normalized =
            n < 0 ? QString::number(n) : QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0')); // {:02d}
    }
    const QString fav = hasFav ? favoritePrefix : QString();
    const QString result = normalized.isEmpty() ? fav + base : fav + normalized + u'-' + base;
    return result == originalValue ? QString() : result;
}

bool isFavorite(const StringEntry &entry, const QString &favoritePrefix)
{
    return entry.isFavoritableShip() && entry.customValue.startsWith(favoritePrefix);
}

bool toggleFavorite(StringEntry &entry, const QString &favoritePrefix)
{
    const bool stranded = !favoritePrefix.isEmpty() && entry.customValue.startsWith(favoritePrefix);
    if (!entry.isFavoritableShip() && !stranded)
        return false;
    if (entry.customValue.startsWith(favoritePrefix)) {
        const QString rest = entry.customValue.sliced(favoritePrefix.size());
        entry.customValue = rest != entry.originalValue ? rest : QString();
    } else {
        entry.customValue =
            favoritePrefix + (entry.customValue.isEmpty() ? entry.originalValue : entry.customValue);
    }
    entry.status = entry.customValue.isEmpty() ? EntryStatus::Unmodified : EntryStatus::Modified;
    return true;
}

void setCustomValue(StringEntry &entry, const QString &value)
{
    entry.customValue = value;
    entry.status = value != entry.originalValue ? EntryStatus::Modified : EntryStatus::Unmodified;
}

QList<int> filterEntryIndices(const QList<StringEntry> &entries, const IniMap &defaults,
                              const FilterCriteria &c)
{
    const QString star = QStringLiteral("★");
    // Case-insensitive search of the stored text: a refilter runs on the GUI
    // thread over ~90k rows, too many to lower-case a copy of each value.
    auto columnContains = [&](const StringEntry &e, int column, const QString &needle) {
        const auto has = [&](QStringView text) { return text.contains(needle, Qt::CaseInsensitive); };
        switch (column) {
        case ColCategory:
            return has(e.category);
        case ColKey:
            return has(e.key);
        case ColDefault: {
            const QString *d = defaults.find(e.key);
            return d && has(*d);
        }
        case ColCurrent:
            return has(e.originalValue);
        case ColStar:
            return isFavorite(e, c.favoritePrefix) && has(star);
        case ColOrder:
            return e.isFavoritableShip() && has(sortOrder(e.customValue, c.favoritePrefix));
        case ColCustom:
            return has(e.customValue);
        case ColStatus:
            return has(statusName(e.status));
        default:
            return false;
        }
    };
    QList<std::pair<int, QString>> active;
    for (int i = 0; i < ColumnCount; ++i)
        if (!c.columnText[i].isEmpty())
            active.push_back({i, c.columnText[i]});

    QList<int> result;
    result.reserve(entries.size());
    for (qsizetype idx = 0; idx < entries.size(); ++idx) {
        const StringEntry &e = entries[idx];
        if (c.hideUnmodified && e.status == EntryStatus::Unmodified)
            continue;
        if (c.category != u"All" && e.category != c.category)
            continue;
        if (c.status != u"All" && statusName(e.status) != c.status)
            continue;
        if (c.shipVehicleNamesOnly && !e.isFavoritableShip())
            continue;
        if (c.favoritesOnly && !isFavorite(e, c.favoritePrefix))
            continue;
        if (c.bpTitlesOnly || c.bpDescsOnly) {
            const QString &val = e.customValue.isEmpty() ? e.originalValue : e.customValue;
            const bool bpTitle = c.bpTitlesOnly && isMission(e) && val.contains(u"[BP");
            const bool bpDesc = c.bpDescsOnly && isMission(e) &&
                                blueprints::hasBpSection(val, c.bpHeader.value_or(QString()));
            if (!bpTitle && !bpDesc)
                continue;
        }
        if (!c.searchText.isEmpty() && !columnContains(e, ColKey, c.searchText) &&
            !columnContains(e, ColCurrent, c.searchText) && !columnContains(e, ColCustom, c.searchText) &&
            !columnContains(e, ColDefault, c.searchText))
            continue;
        bool skip = false;
        for (const auto &[column, text] : active)
            if (!columnContains(e, column, text)) {
                skip = true;
                break;
            }
        if (!skip)
            result.push_back(static_cast<int>(idx));
    }
    return result;
}

std::pair<QString, int> groupSortKey(const QString &key)
{
    static const QRegularExpression item(QStringLiteral(R"(\A(item_)(Name|Desc|name|desc)(.*))"),
                                         QRegularExpression::CaseInsensitiveOption |
                                             QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression vehicle(QStringLiteral(R"(\A(vehicle_)(Name|Desc)(.*))"),
                                            QRegularExpression::CaseInsensitiveOption |
                                                QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression mission(QStringLiteral(R"(\A(.*?)_(title|desc|content)(_.+)?\z)"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression commodity(
        QStringLiteral(R"(\A(items_commodities_\w+?)(?:_(desc?|description))?\z)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    // Each pattern needs a literal its guard tests first: a grouped sort runs
    // this for every row, and most keys match none of them.
    constexpr auto ci = Qt::CaseInsensitive;
    if (key.startsWith(u"item_", ci))
        if (const auto m = item.match(key); m.hasMatch())
            return {(QStringLiteral("item_") + m.captured(3)).toLower(),
                    m.captured(2).toLower() == u"name" ? 0 : 1};
    if (key.startsWith(u"vehicle_", ci))
        if (const auto m = vehicle.match(key); m.hasMatch())
            return {(QStringLiteral("vehicle_") + m.captured(3)).toLower(),
                    m.captured(2).toLower() == u"name" ? 0 : 1};
    if (key.startsWith(u"items_commodities_", ci))
        if (const auto m = commodity.match(key); m.hasMatch())
            return {m.captured(1).toLower(), m.hasCaptured(2) ? 1 : 0};
    if (key.contains(u"_title", ci) || key.contains(u"_desc", ci) || key.contains(u"_content", ci))
        if (const auto m = mission.match(key); m.hasMatch())
            return {(m.captured(1) + m.captured(3)).toLower(), m.captured(2).toLower() == u"title" ? 0 : 1};
    return {key.toLower(), 0};
}

QString ownedName(const StringEntry &entry, const IniMap &defaults, const OwnedState &owned)
{
    const QString *stock = defaults.find(entry.key);
    return blueprints::normalizeItemName(entry.customValue.isEmpty() ? entry.originalValue
                                                                     : entry.customValue,
                                         owned.enclosings, stock ? *stock : QString());
}

bool isBlueprintItem(const StringEntry &entry, const IniMap &defaults, const OwnedState &owned)
{
    return !owned.blueprintItems.isEmpty() &&
           owned.blueprintItems.contains(ownedName(entry, defaults, owned));
}

bool isOwned(const StringEntry &entry, const IniMap &defaults, const OwnedState &owned)
{
    if (owned.blueprintItems.isEmpty())
        return false;
    const QString name = ownedName(entry, defaults, owned);
    return owned.blueprintItems.contains(name) && owned.owned.contains(name);
}

void sortIndices(QList<int> &indices, const QList<StringEntry> &entries, const IniMap &defaults,
                 Column column, bool descending, bool grouped, const QString &favoritePrefix,
                 const OwnedState &owned)
{
    if (indices.isEmpty())
        return;
    // (int, text, text, int) covers every key shape the Python builds. Keys
    // are stored by position and a permutation sorted, so each comparison is
    // two array reads rather than two hash lookups.
    using Key = std::tuple<int, QString, QString, int>;
    std::vector<Key> keys;
    keys.reserve(static_cast<std::size_t>(indices.size()));
    for (const int idx : indices) {
        const StringEntry &e = entries[idx];
        Key k;
        if (column == ColKey && grouped) {
            const auto [group, sub] = groupSortKey(e.key);
            k = {0, group, QString(), sub};
        } else {
            switch (column) {
            case ColCategory:
                k = {0, e.category.toLower(), {}, 0};
                break;
            case ColDefault: {
                const QString *d = defaults.find(e.key);
                k = {0, d ? d->toLower() : QString(), {}, 0};
                break;
            }
            case ColCurrent:
                k = {0, e.originalValue.toLower(), {}, 0};
                break;
            case ColCustom:
                k = {0, e.customValue.toLower(), {}, 0};
                break;
            case ColStatus:
                k = {0, statusName(e.status).toLower(), {}, 0};
                break;
            case ColOwned:
                k = {isOwned(e, defaults, owned) ? 0 : 1, e.key.toLower(), {}, 0};
                break;
            case ColStar:
                k = {isFavorite(e, favoritePrefix) ? 0 : 1, e.key.toLower(), {}, 0};
                break;
            case ColOrder: {
                const QString order =
                    e.isFavoritableShip() ? sortOrder(e.customValue, favoritePrefix) : QString();
                k = {order.isEmpty() ? 1 : 0, order, e.key.toLower(), 0};
                break;
            }
            default:
                k = {0, e.key.toLower(), {}, 0};
                break;
            }
        }
        keys.push_back(std::move(k));
    }
    const auto less = [](const Key &a, const Key &b) {
        if (std::get<0>(a) != std::get<0>(b))
            return std::get<0>(a) < std::get<0>(b);
        if (std::get<1>(a) != std::get<1>(b))
            return py::less(std::get<1>(a), std::get<1>(b));
        if (std::get<2>(a) != std::get<2>(b))
            return py::less(std::get<2>(a), std::get<2>(b));
        return std::get<3>(a) < std::get<3>(b);
    };
    std::vector<qsizetype> order(keys.size());
    std::iota(order.begin(), order.end(), qsizetype(0));
    std::stable_sort(order.begin(), order.end(), [&](qsizetype x, qsizetype y) {
        const Key &a = keys[static_cast<std::size_t>(x)], &b = keys[static_cast<std::size_t>(y)];
        return descending ? less(b, a) : less(a, b);
    });
    QList<int> sorted;
    sorted.reserve(indices.size());
    for (const qsizetype p : order)
        sorted.push_back(indices[p]);
    indices = std::move(sorted);
}

QStringList filterCategories(const QList<StringEntry> &entries)
{
    QSet<QString> set = {category::kShips, category::kShipItems, category::kMissions, category::kCommodities,
                         category::kOther};
    for (const StringEntry &e : entries)
        set.insert(e.category);
    QStringList out(set.begin(), set.end());
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return py::less(a, b); });
    return out;
}

QString filteredAsTsv(const QList<StringEntry> &entries, const QList<int> &rows)
{
    QStringList lines = {QStringLiteral("Key\tOriginal Value\tCurrent Value\tCustom Value\tStatus")};
    for (const int idx : rows) {
        const StringEntry &e = entries[idx];
        lines << QStringList{e.key, e.originalValue, e.originalValue, e.customValue, statusName(e.status)}
                     .join(u'\t');
    }
    return lines.join(u'\n');
}

QString journalStampFor(const StringEntry &entry, const QString &appName, const QString &version)
{
    static const QRegularExpression titleKey(
        QStringLiteral(R"(_(?:title|shorttitle|subtitle|subheading|from)(?:,P)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (entry.category != category::kJournal || titleKey.match(entry.key).hasMatch())
        return {};
    if (entry.customValue.isEmpty() && entry.sourceFile != u"enhancements")
        return {};
    return QStringLiteral("[Edited with %1 v%2]").arg(appName, version);
}

QString previewHtml(const QString &key, const QString &raw, const QString &stamp)
{
    QString text = raw;
    if (!stamp.isEmpty())
        text += QStringLiteral(R"(\n\n)") + stamp;
    QString body;
    if (text.isEmpty()) {
        body = QStringLiteral("<em style='color:#888;'>(empty)</em>");
    } else {
        static const QRegularExpression em3(QStringLiteral("&lt;EM3&gt;(.*?)&lt;/EM3&gt;"),
                                            QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression em4(QStringLiteral("&lt;EM4&gt;(.*?)&lt;/EM4&gt;"),
                                            QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression token(QStringLiteral(R"(~mission\(([^|)]+)(?:\|[^)]*)?\))"));
        body = text.toHtmlEscaped();
        body.replace(QStringLiteral(R"(\n)"), QStringLiteral("<br>"));
        body.replace(em3, QStringLiteral("<span style=\"text-decoration:underline;\">\\1</span>"));
        body.replace(em4, QStringLiteral("<span style=\"font-weight:bold;color:#4a9eff;\">\\1</span>"));
        body.replace(token, QStringLiteral("<span style=\"color:#888;font-style:italic;\">[\\1]</span>"));
    }
    return QStringLiteral(
               "<div style=\"font-family:Segoe UI,sans-serif;font-size:10pt;line-height:1.45;\">"
               "<div "
               "style=\"color:#888;font-size:8pt;margin-bottom:8px;font-family:Consolas,monospace;\">%1</div>"
               "<br>%2</div>")
        .arg(key.toHtmlEscaped(), body);
}

QHash<QString, QString> pendingEdits(const QList<StringEntry> &entries)
{
    QHash<QString, QString> out;
    for (const StringEntry &e : entries)
        if (!e.customValue.isEmpty())
            out.insert(e.key, e.customValue);
    return out;
}

int restorePendingEdits(QList<StringEntry> &entries, const QHash<QString, QString> &pending)
{
    int restored = 0;
    for (StringEntry &e : entries) {
        const auto it = pending.constFind(e.key);
        if (it == pending.cend() || *it == e.customValue)
            continue;
        setCustomValue(e, *it);
        ++restored;
    }
    return restored;
}

} // namespace core::table
