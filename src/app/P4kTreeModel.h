#pragma once

#include "engine/forge/DataForge.h"
#include "engine/p4k/Archive.h"
#include "engine/p4k/TreeIndex.h"

#include <QAbstractItemModel>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <string>
#include <vector>

// What the P4K Explorer browses: an open archive, optionally its DataForge
// database, and the tree over both. Items [0, archiveItems) are archive
// entries; the rest are DataForge records, listed as a virtual
// "<dcb> (records)/libs/foundry/records/..." folder beside the .dcb.
// Immutable once built, so worker threads can share it.
struct ExplorerData
{
    std::shared_ptr<const engine::p4k::Archive> archive;
    std::shared_ptr<const engine::forge::DataForge> forge;
    std::string dcbName;                 // "Data/Game2.dcb"
    std::vector<std::string> recordPaths; // full virtual paths (the tree views into these)
    std::vector<std::uint32_t> records;   // record index per virtual item
    std::uint32_t archiveItems = 0;
    engine::p4k::TreeIndex index;

    bool isRecord(std::uint32_t item) const { return item >= archiveItems; }
    std::uint32_t recordOf(std::uint32_t item) const { return records[item - archiveItems]; }
    // The record's own path ("libs/foundry/records/...").
    std::string_view recordPath(std::uint32_t item) const;
};

// The explorer's tree for QML's TreeView: name, size, compressed size and
// method/encryption per node. The node id is the index's internal id.
class P4kTreeModel : public QAbstractItemModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by ExplorerController")

public:
    enum Columns { ColName, ColSize, ColPacked, ColMethod, ColumnCount };
    enum Roles { NodeRole = Qt::UserRole + 1, FolderRole, RecordRole };

    explicit P4kTreeModel(QObject *parent = nullptr);

    void setExplorerData(std::shared_ptr<const ExplorerData> data);
    const ExplorerData *explorerData() const { return data_.get(); }

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int nodeAt(const QModelIndex &index) const;
    Q_INVOKABLE QModelIndex indexOfNode(int node) const;

    static QString formatSize(std::uint64_t bytes);

private:
    std::shared_ptr<const ExplorerData> data_;
};
