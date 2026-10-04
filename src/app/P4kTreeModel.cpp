#include "P4kTreeModel.h"

#include "core/i18n/Translator.h"

#include <QLocale>

using engine::p4k::TreeIndex;

std::string_view ExplorerData::recordPath(std::uint32_t item) const
{
    return forge ? forge->recordFileName(recordOf(item)) : std::string_view();
}

P4kTreeModel::P4kTreeModel(QObject *parent) : QAbstractItemModel(parent) {}

void P4kTreeModel::setExplorerData(std::shared_ptr<const ExplorerData> data)
{
    beginResetModel();
    data_ = std::move(data);
    endResetModel();
}

QString P4kTreeModel::formatSize(std::uint64_t bytes)
{
    return QLocale::system().formattedDataSize(static_cast<qint64>(bytes), 1, QLocale::DataSizeTraditionalFormat);
}

QModelIndex P4kTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!data_ || row < 0 || column < 0 || column >= ColumnCount)
        return {};
    const TreeIndex &t = data_->index;
    const TreeIndex::NodeId folder = parent.isValid() ? static_cast<TreeIndex::NodeId>(parent.internalId()) : t.root();
    if (static_cast<std::uint32_t>(row) >= t.node(folder).childCount)
        return {};
    return createIndex(row, column, static_cast<quintptr>(t.child(folder, static_cast<std::uint32_t>(row))));
}

QModelIndex P4kTreeModel::parent(const QModelIndex &child) const
{
    if (!data_ || !child.isValid())
        return {};
    const TreeIndex &t = data_->index;
    const TreeIndex::NodeId p = t.node(static_cast<TreeIndex::NodeId>(child.internalId())).parent;
    if (p == TreeIndex::kNone || p == t.root())
        return {};
    return createIndex(static_cast<int>(t.node(p).row), 0, static_cast<quintptr>(p));
}

int P4kTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!data_ || parent.column() > 0)
        return 0;
    const TreeIndex &t = data_->index;
    const TreeIndex::NodeId n = parent.isValid() ? static_cast<TreeIndex::NodeId>(parent.internalId()) : t.root();
    return static_cast<int>(t.node(n).childCount);
}

int P4kTreeModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

QVariant P4kTreeModel::data(const QModelIndex &index, int role) const
{
    if (!data_ || !index.isValid())
        return {};
    const TreeIndex &t = data_->index;
    const auto id = static_cast<TreeIndex::NodeId>(index.internalId());
    const TreeIndex::Node &n = t.node(id);
    const bool folder = n.item == TreeIndex::kNone;
    switch (role) {
    case NodeRole: return static_cast<int>(id);
    case FolderRole: return folder;
    case RecordRole: return !folder && data_->isRecord(n.item);
    case Qt::DisplayRole: break;
    default: return {};
    }
    switch (index.column()) {
    case ColName: return QString::fromUtf8(n.name.data(), static_cast<qsizetype>(n.name.size()));
    case ColSize:
    case ColPacked:
        // Records (and folders holding only records) have no stored size.
        if (folder ? n.size == 0 && n.files > 0 : data_->isRecord(n.item))
            return QString();
        return formatSize(index.column() == ColSize ? n.size : n.packed);
    case ColMethod: {
        if (folder)
            return core::i18n::tr("scx.explorer_files_count", {{QStringLiteral("count"), QLocale::system().toString(n.files)}});
        if (data_->isRecord(n.item))
            return core::i18n::tr("scx.explorer_record");
        const engine::p4k::Entry &e = data_->archive->entry(n.item);
        QString m = QString::fromLatin1(engine::p4k::methodName(e.method));
        if (e.encrypted)
            m += QStringLiteral(" · ") + core::i18n::tr("scx.explorer_encrypted");
        return m;
    }
    }
    return {};
}

QVariant P4kTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColName: return core::i18n::tr("scx.explorer_col_name");
    case ColSize: return core::i18n::tr("scx.explorer_col_size");
    case ColPacked: return core::i18n::tr("scx.explorer_col_packed");
    case ColMethod: return core::i18n::tr("scx.explorer_col_method");
    }
    return {};
}

QHash<int, QByteArray> P4kTreeModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();
    roles.insert(NodeRole, "node");
    roles.insert(FolderRole, "folder");
    roles.insert(RecordRole, "record");
    return roles;
}

int P4kTreeModel::nodeAt(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<int>(index.internalId()) : -1;
}

QModelIndex P4kTreeModel::indexOfNode(int node) const
{
    if (!data_ || node <= 0 || static_cast<std::size_t>(node) >= data_->index.nodeCount())
        return {};
    const auto id = static_cast<TreeIndex::NodeId>(node);
    return createIndex(static_cast<int>(data_->index.node(id).row), 0, static_cast<quintptr>(id));
}
