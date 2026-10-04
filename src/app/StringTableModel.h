#pragma once

#include "core/model/StringTable.h"

#include <QAbstractTableModel>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

// The String Editor's table: every entry, filtered and sorted into rows.
// Owns the entries; App reads them back for saving and applying. Filters
// are properties the QML filter row binds to; any change re-filters and
// keeps the current row selected by key when it survives.
class StringTableModel : public QAbstractTableModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by App")

    Q_PROPERTY(int totalCount READ totalCount NOTIFY dataReset)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY filtered)
    Q_PROPERTY(int modifiedCount READ modifiedCount NOTIFY countsChanged)
    Q_PROPERTY(int enhancedCount READ enhancedCount NOTIFY countsChanged)
    Q_PROPERTY(QStringList categories READ categories NOTIFY dataReset)
    Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString statusFilter READ statusFilter WRITE setStatusFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY filtersChanged)
    Q_PROPERTY(bool hideUnmodified READ hideUnmodified WRITE setHideUnmodified NOTIFY filtersChanged)
    Q_PROPERTY(bool favoritesOnly READ favoritesOnly WRITE setFavoritesOnly NOTIFY filtersChanged)
    Q_PROPERTY(bool shipNamesOnly READ shipNamesOnly WRITE setShipNamesOnly NOTIFY filtersChanged)
    Q_PROPERTY(bool bpTitlesOnly READ bpTitlesOnly WRITE setBpTitlesOnly NOTIFY filtersChanged)
    Q_PROPERTY(bool bpDescsOnly READ bpDescsOnly WRITE setBpDescsOnly NOTIFY filtersChanged)
    Q_PROPERTY(int sortColumn READ sortColumn NOTIFY sortChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending NOTIFY sortChanged)
    Q_PROPERTY(bool groupedSort READ groupedSort NOTIFY sortChanged)

public:
    enum Roles {
        EditRole = Qt::UserRole + 1,
        ForegroundRole,
        BackgroundRole,
        EditableRole,
        CenteredRole,
        TooltipRole,
        KindRole, // "star", "order", "owned", "status" or "text"
    };

    explicit StringTableModel(QObject *parent = nullptr);

    // Replaces everything (after a load). Entry order is the table's
    // natural order.
    void setEntries(QList<core::StringEntry> entries, core::IniMap defaults);
    const QList<core::StringEntry> &entries() const { return entries_; }
    QList<core::StringEntry> &mutableEntries() { return entries_; }
    const core::IniMap &defaults() const { return defaults_; }
    void setFavoritePrefix(const QString &prefix);
    void setOwnedState(core::table::OwnedState owned);
    void setBlueprintHeader(const QString &header);
    void setAppStamp(const QString &appName, const QString &version);
    // Re-reads every row after entries changed in place.
    void refreshAll();

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    int totalCount() const { return static_cast<int>(entries_.size()); }
    int visibleCount() const { return static_cast<int>(rows_.size()); }
    int modifiedCount() const { return modified_; }
    int enhancedCount() const { return enhanced_; }
    QStringList categories() const { return categories_; }

    QString categoryFilter() const { return criteria_.category; }
    void setCategoryFilter(const QString &v);
    QString statusFilter() const { return criteria_.status; }
    void setStatusFilter(const QString &v);
    QString searchText() const { return criteria_.searchText; }
    void setSearchText(const QString &v);
    bool hideUnmodified() const { return criteria_.hideUnmodified; }
    void setHideUnmodified(bool v);
    bool favoritesOnly() const { return criteria_.favoritesOnly; }
    void setFavoritesOnly(bool v);
    bool shipNamesOnly() const { return criteria_.shipVehicleNamesOnly; }
    void setShipNamesOnly(bool v);
    bool bpTitlesOnly() const { return criteria_.bpTitlesOnly; }
    void setBpTitlesOnly(bool v);
    bool bpDescsOnly() const { return criteria_.bpDescsOnly; }
    void setBpDescsOnly(bool v);
    int sortColumn() const { return sortColumn_; }
    bool sortDescending() const { return descending_; }
    bool groupedSort() const { return grouped_; }

    Q_INVOKABLE void setColumnFilter(int column, const QString &text);
    Q_INVOKABLE QString columnFilter(int column) const;
    Q_INVOKABLE void clearFilters();
    Q_INVOKABLE void sortBy(int column); // a header click: toggles the direction on the same column
    Q_INVOKABLE void sortGrouped();
    Q_INVOKABLE QString key(int row) const;
    Q_INVOKABLE int rowForKey(const QString &key) const;
    Q_INVOKABLE QString effectiveValue(int row) const; // custom, else current
    Q_INVOKABLE QString customValue(int row) const;
    Q_INVOKABLE QString previewHtml(int row) const;
    Q_INVOKABLE bool isFavoritable(int row) const;
    Q_INVOKABLE void toggleFavorite(int row);
    Q_INVOKABLE void setCustom(int row, const QString &text);
    Q_INVOKABLE void resetRow(int row); // back to the merged baseline
    Q_INVOKABLE QString filteredTsv() const;
    Q_INVOKABLE int defaultColumnWidth(int column) const;

signals:
    void dataReset();
    void filtered();
    void filtersChanged();
    void sortChanged();
    void countsChanged();
    void edited(); // a user change: the Apply button lights up
    void rowsRelaid(int keepRow); // after a filter/sort; keepRow is the selected key's new row or -1

public slots:
    void retranslate();

private:
    const core::StringEntry *entryAt(int row) const;
    void setBoolFilter(bool core::table::FilterCriteria::*field, bool v);
    void refilter();
    void resort();
    void recount();
    void notifyRow(int entryIndex);

    QList<core::StringEntry> entries_;
    core::IniMap defaults_;
    QList<int> rows_;    // entry indices, in display order
    QHash<int, int> rowOf_; // entry index -> row
    core::table::FilterCriteria criteria_;
    core::table::OwnedState owned_;
    QString appName_, version_;
    QStringList categories_;
    QStringList headers_;
    int sortColumn_ = core::table::ColKey;
    bool descending_ = false;
    bool grouped_ = false;
    int modified_ = 0, enhanced_ = 0;
};
