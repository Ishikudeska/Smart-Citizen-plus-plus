#include "StringTableModel.h"

#include "core/i18n/Translator.h"

#include <QColor>

using namespace core;
using namespace core::table;
using core::i18n::tr;

namespace {

const QColor kGold(0xFF, 0xD7, 0x00);
const QColor kGrey(0x66, 0x66, 0x66);

QColor statusColor(EntryStatus status)
{
    switch (status) {
    case EntryStatus::Modified: return QColor(0x4C, 0xAF, 0x50);
    case EntryStatus::Enhanced: return QColor(0x21, 0x96, 0xF3);
    case EntryStatus::Unmodified: return QColor(0x99, 0x99, 0x99);
    case EntryStatus::New: return QColor(0xFF, 0x98, 0x00);
    }
    return {};
}

const char *const kHeaderKeys[] = {
    "strings_tab.col_category", "strings_tab.col_key",    "strings_tab.col_default_value",
    "strings_tab.col_current_value", "strings_tab.col_star", "strings_tab.col_order",
    "strings_tab.col_custom_value", "strings_tab.col_status", "strings_tab.col_owned",
};

} // namespace

StringTableModel::StringTableModel(QObject *parent) : QAbstractTableModel(parent)
{
    retranslate();
}

void StringTableModel::retranslate()
{
    headers_.clear();
    for (const char *key : kHeaderKeys)
        headers_ << tr(key);
    emit headerDataChanged(Qt::Horizontal, 0, ColumnCount - 1);
}

void StringTableModel::setEntries(QList<StringEntry> entries, IniMap defaults)
{
    beginResetModel();
    entries_ = std::move(entries);
    defaults_ = std::move(defaults);
    categories_ = filterCategories(entries_);
    if (criteria_.category != u"All" && !categories_.contains(criteria_.category))
        criteria_.category = QStringLiteral("All");
    rows_ = filterEntryIndices(entries_, defaults_, criteria_);
    sortIndices(rows_, entries_, defaults_, static_cast<Column>(sortColumn_), descending_, grouped_,
                criteria_.favoritePrefix, owned_);
    rowOf_.clear();
    for (int r = 0; r < rows_.size(); ++r)
        rowOf_.insert(rows_[r], r);
    endResetModel();
    recount();
    emit dataReset();
    emit filtered();
}

void StringTableModel::setFavoritePrefix(const QString &prefix)
{
    beginResetModel();
    criteria_.favoritePrefix = prefix;
    endResetModel();
}

void StringTableModel::setOwnedState(OwnedState owned)
{
    beginResetModel();
    owned_ = std::move(owned);
    endResetModel();
}

void StringTableModel::setBlueprintHeader(const QString &header)
{
    criteria_.bpHeader = header;
    if (criteria_.bpDescsOnly)
        refilter();
}

void StringTableModel::setAppStamp(const QString &appName, const QString &version)
{
    appName_ = appName;
    version_ = version;
}

void StringTableModel::refreshAll()
{
    if (!rows_.isEmpty())
        emit dataChanged(index(0, 0), index(static_cast<int>(rows_.size()) - 1, ColumnCount - 1));
    recount();
}

int StringTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int StringTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

const StringEntry *StringTableModel::entryAt(int row) const
{
    if (row < 0 || row >= rows_.size())
        return nullptr;
    return &entries_[rows_[row]];
}

QVariant StringTableModel::data(const QModelIndex &index, int role) const
{
    const StringEntry *e = entryAt(index.row());
    if (!e)
        return {};
    const int col = index.column();
    const QString &prefix = criteria_.favoritePrefix;
    const bool favoritable = e->isFavoritableShip();
    switch (role) {
    case Qt::DisplayRole:
        switch (col) {
        case ColCategory: return e->category;
        case ColKey: return e->key;
        case ColDefault: {
            const QString *d = defaults_.find(e->key);
            return d ? *d : QString();
        }
        case ColCurrent: return e->originalValue;
        case ColStar:
            return favoritable ? (e->customValue.startsWith(prefix) ? QStringLiteral("★") : QStringLiteral("☆"))
                               : QString();
        case ColOrder: return favoritable ? sortOrder(e->customValue, prefix) : QString();
        case ColCustom: return e->customValue;
        case ColStatus: return statusName(e->status);
        case ColOwned:
            if (!isBlueprintItem(*e, defaults_, owned_))
                return QString();
            return isOwned(*e, defaults_, owned_) ? QStringLiteral("★") : QStringLiteral("☆");
        }
        return {};
    case EditRole:
        if (col == ColCustom)
            return e->customValue;
        if (col == ColOrder)
            return sortOrder(e->customValue, prefix);
        return data(index, Qt::DisplayRole);
    case ForegroundRole:
        if (col == ColStar && favoritable)
            return e->customValue.startsWith(prefix) ? kGold : kGrey;
        if (col == ColOwned && isBlueprintItem(*e, defaults_, owned_))
            return isOwned(*e, defaults_, owned_) ? kGold : kGrey;
        if (col == ColStatus)
            return statusColor(e->status);
        return QVariant();
    case BackgroundRole:
        return favoritable && e->customValue.startsWith(prefix) ? QVariant(true) : QVariant(false);
    case EditableRole:
        return col == ColCustom || (col == ColOrder && favoritable);
    case CenteredRole:
        return col == ColStar || col == ColOrder || col == ColOwned;
    case KindRole:
        switch (col) {
        case ColStar: return QStringLiteral("star");
        case ColOrder: return QStringLiteral("order");
        case ColOwned: return QStringLiteral("owned");
        case ColStatus: return QStringLiteral("status");
        default: return QStringLiteral("text");
        }
    case TooltipRole:
        if (col == ColStar)
            return favoritable ? (e->customValue.startsWith(prefix) ? tr("strings_tab.star_tooltip_remove")
                                                                    : tr("strings_tab.star_tooltip_add"))
                               : QString();
        if (col == ColOrder)
            return favoritable ? tr("strings_tab.order_tooltip") : QString();
        if (col == ColOwned) {
            if (!isBlueprintItem(*e, defaults_, owned_))
                return QString();
            return isOwned(*e, defaults_, owned_) ? tr("strings_tab.owned_tooltip_owned")
                                                  : tr("strings_tab.owned_tooltip_ownable");
        }
        return data(index, Qt::DisplayRole);
    }
    return {};
}

QVariant StringTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < headers_.size())
        return headers_[section];
    return {};
}

bool StringTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole && role != EditRole)
        return false;
    const int row = index.row();
    if (row < 0 || row >= rows_.size())
        return false;
    StringEntry &e = entries_[rows_[row]];
    const QString text = value.toString();
    if (index.column() == ColCustom) {
        if (text == e.customValue)
            return false;
        setCustomValue(e, text);
    } else if (index.column() == ColOrder) {
        if (!e.isFavoritableShip())
            return false;
        const QString updated = withSortOrder(e.customValue, e.originalValue, criteria_.favoritePrefix, text);
        if (updated == e.customValue)
            return false;
        e.customValue = updated;
        e.status = updated.isEmpty() ? EntryStatus::Unmodified : EntryStatus::Modified;
    } else {
        return false;
    }
    notifyRow(rows_[row]);
    recount();
    emit edited();
    return true;
}

Qt::ItemFlags StringTableModel::flags(const QModelIndex &index) const
{
    const Qt::ItemFlags base = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    const StringEntry *e = entryAt(index.row());
    switch (index.column()) {
    case ColCustom: return base | Qt::ItemIsEditable;
    case ColStar: return e && !e->isFavoritableShip() ? Qt::ItemIsEnabled : base;
    case ColOrder: return e && e->isFavoritableShip() ? base | Qt::ItemIsEditable : Qt::ItemIsEnabled;
    case ColOwned: return Qt::ItemIsEnabled;
    }
    return base;
}

QHash<int, QByteArray> StringTableModel::roleNames() const
{
    return {
        {Qt::DisplayRole, "display"},       {EditRole, "edit"},         {ForegroundRole, "foreground"},
        {BackgroundRole, "favoriteRow"},    {EditableRole, "editable"}, {CenteredRole, "centered"},
        {TooltipRole, "tooltip"},           {KindRole, "kind"},
    };
}

void StringTableModel::setCategoryFilter(const QString &v)
{
    if (criteria_.category == v)
        return;
    criteria_.category = v.isEmpty() ? QStringLiteral("All") : v;
    emit filtersChanged();
    refilter();
}

void StringTableModel::setStatusFilter(const QString &v)
{
    if (criteria_.status == v)
        return;
    criteria_.status = v.isEmpty() ? QStringLiteral("All") : v;
    emit filtersChanged();
    refilter();
}

#define SC_BOOL_FILTER(setter, field)                                                                                  \
    void StringTableModel::setter(bool v)                                                                              \
    {                                                                                                                  \
        if (criteria_.field == v)                                                                                      \
            return;                                                                                                    \
        criteria_.field = v;                                                                                           \
        emit filtersChanged();                                                                                         \
        refilter();                                                                                                    \
    }
SC_BOOL_FILTER(setHideUnmodified, hideUnmodified)
SC_BOOL_FILTER(setFavoritesOnly, favoritesOnly)
SC_BOOL_FILTER(setShipNamesOnly, shipVehicleNamesOnly)
SC_BOOL_FILTER(setBpTitlesOnly, bpTitlesOnly)
SC_BOOL_FILTER(setBpDescsOnly, bpDescsOnly)
#undef SC_BOOL_FILTER

void StringTableModel::setColumnFilter(int column, const QString &text)
{
    if (column < 0 || column >= ColumnCount)
        return;
    const QString lowered = text.toLower();
    if (criteria_.columnText[column] == lowered)
        return;
    criteria_.columnText[column] = lowered;
    emit filtersChanged();
    refilter();
}

QString StringTableModel::columnFilter(int column) const
{
    return column >= 0 && column < ColumnCount ? criteria_.columnText[column] : QString();
}

void StringTableModel::clearFilters()
{
    const QString prefix = criteria_.favoritePrefix;
    const auto header = criteria_.bpHeader;
    criteria_ = {};
    criteria_.favoritePrefix = prefix;
    criteria_.bpHeader = header;
    emit filtersChanged();
    refilter();
}

void StringTableModel::sortBy(int column)
{
    if (column < 0 || column >= ColumnCount)
        return;
    descending_ = column == sortColumn_ && !grouped_ ? !descending_ : false;
    sortColumn_ = column;
    grouped_ = false;
    emit sortChanged();
    resort();
}

void StringTableModel::sortGrouped()
{
    grouped_ = true;
    sortColumn_ = ColKey;
    descending_ = false;
    emit sortChanged();
    resort();
}

QString StringTableModel::key(int row) const
{
    const StringEntry *e = entryAt(row);
    return e ? e->key : QString();
}

int StringTableModel::rowForKey(const QString &key) const
{
    for (int r = 0; r < rows_.size(); ++r)
        if (entries_[rows_[r]].key == key)
            return r;
    return -1;
}

QString StringTableModel::effectiveValue(int row) const
{
    const StringEntry *e = entryAt(row);
    return !e ? QString() : e->customValue.isEmpty() ? e->originalValue : e->customValue;
}

QString StringTableModel::customValue(int row) const
{
    const StringEntry *e = entryAt(row);
    return e ? e->customValue : QString();
}

QString StringTableModel::previewHtml(int row) const
{
    const StringEntry *e = entryAt(row);
    if (!e)
        return {};
    return core::table::previewHtml(e->key, effectiveValue(row), journalStampFor(*e, appName_, version_));
}

bool StringTableModel::isFavoritable(int row) const
{
    const StringEntry *e = entryAt(row);
    return e && (e->isFavoritableShip() ||
                 (!criteria_.favoritePrefix.isEmpty() && e->customValue.startsWith(criteria_.favoritePrefix)));
}

void StringTableModel::toggleFavorite(int row)
{
    if (row < 0 || row >= rows_.size())
        return;
    if (core::table::toggleFavorite(entries_[rows_[row]], criteria_.favoritePrefix)) {
        notifyRow(rows_[row]);
        recount();
        emit edited();
    }
}

void StringTableModel::setCustom(int row, const QString &text)
{
    setData(index(row, ColCustom), text, Qt::EditRole);
}

void StringTableModel::resetRow(int row)
{
    if (row < 0 || row >= rows_.size())
        return;
    StringEntry &e = entries_[rows_[row]];
    e.customValue.clear();
    e.status = EntryStatus::Unmodified;
    notifyRow(rows_[row]);
    recount();
    emit edited();
}

QString StringTableModel::filteredTsv() const
{
    return rows_.isEmpty() ? QString() : filteredAsTsv(entries_, rows_);
}

int StringTableModel::defaultColumnWidth(int column) const
{
    static const int widths[] = {110, 300, 320, 320, 44, 56, 320, 96, 56};
    return column >= 0 && column < ColumnCount ? widths[column] : 100;
}

void StringTableModel::refilter()
{
    rows_ = filterEntryIndices(entries_, defaults_, criteria_);
    resort();
    emit filtered();
}

void StringTableModel::resort()
{
    beginResetModel();
    sortIndices(rows_, entries_, defaults_, static_cast<Column>(sortColumn_), descending_, grouped_,
                criteria_.favoritePrefix, owned_);
    rowOf_.clear();
    for (int r = 0; r < rows_.size(); ++r)
        rowOf_.insert(rows_[r], r);
    endResetModel();
    emit rowsRelaid(-1);
}

void StringTableModel::recount()
{
    int modified = 0, enhanced = 0;
    for (const StringEntry &e : entries_) {
        modified += e.status == EntryStatus::Modified;
        enhanced += e.status == EntryStatus::Enhanced;
    }
    if (modified != modified_ || enhanced != enhanced_) {
        modified_ = modified;
        enhanced_ = enhanced;
        emit countsChanged();
    }
}

void StringTableModel::notifyRow(int entryIndex)
{
    const auto it = rowOf_.constFind(entryIndex);
    if (it != rowOf_.cend())
        emit dataChanged(index(*it, 0), index(*it, ColumnCount - 1));
}
