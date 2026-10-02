#include "LogModel.h"

#include "AppController.h"

#include "core/log/Log.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>

namespace {

constexpr qsizetype kMaxLines = 2000;

int levelForType(int type)
{
    switch (type) {
    case QtDebugMsg: return 10;
    case QtInfoMsg: return 20;
    case QtWarningMsg: return 30;
    case QtCriticalMsg: return 40;
    case QtFatalMsg: return 50;
    }
    return 20;
}

} // namespace

LogModel::LogModel(QObject *parent) : QAbstractListModel(parent)
{
    auto &hub = core::log::LogHub::instance();
    for (const QString &line : hub.recentLines())
        all_.push_back({line, levelOf(line)});
    while (all_.size() > kMaxLines)
        all_.removeFirst();
    rebuild();
    connect(&hub, &core::log::LogHub::lineLogged, this,
            [this](const QString &line, int type) { append(line, levelForType(type)); }, Qt::QueuedConnection);
}

int LogModel::levelOf(const QString &line)
{
    static const std::pair<const char *, int> names[] = {
        {" - DEBUG - ", 10}, {" - INFO - ", 20}, {" - WARNING - ", 30}, {" - ERROR - ", 40}, {" - CRITICAL - ", 50}};
    for (const auto &[name, level] : names)
        if (line.contains(QLatin1String(name)))
            return level;
    return 20;
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= visible_.size())
        return {};
    const Line &l = visible_[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case TextRole: return l.text;
    case LevelRole: return l.level;
    case ColorRole:
        return l.level >= 40 ? QStringLiteral("#f44336")
               : l.level >= 30 ? QStringLiteral("#ff9800")
               : l.level <= 10 ? QStringLiteral("#888888")
                               : QString();
    }
    return {};
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {{TextRole, "text"}, {LevelRole, "level"}, {ColorRole, "lineColor"}};
}

void LogModel::setMinLevel(int level)
{
    if (level == minLevel_)
        return;
    minLevel_ = level;
    rebuild();
    emit minLevelChanged();
}

void LogModel::append(const QString &line, int level)
{
    all_.push_back({line, level});
    if (all_.size() > kMaxLines)
        all_.removeFirst();
    if (level < minLevel_)
        return;
    if (visible_.size() >= kMaxLines) {
        beginRemoveRows({}, 0, 0);
        visible_.removeFirst();
        endRemoveRows();
    }
    const int row = static_cast<int>(visible_.size());
    beginInsertRows({}, row, row);
    visible_.push_back({line, level});
    endInsertRows();
    emit countChanged();
}

void LogModel::rebuild()
{
    beginResetModel();
    visible_.clear();
    for (const Line &l : all_)
        if (l.level >= minLevel_)
            visible_.push_back(l);
    endResetModel();
    emit countChanged();
}

void LogModel::clear()
{
    all_.clear();
    core::log::LogHub::instance().clearRecent();
    rebuild();
}

QString LogModel::allText() const
{
    QStringList lines;
    for (const Line &l : visible_)
        lines << l.text;
    return lines.join(u'\n');
}

QString LogModel::defaultExportPath() const
{
    const QString name = QStringLiteral("%1_%2.log")
                             .arg(QCoreApplication::applicationName().toLower(),
                                  QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    return QDir(AppController::instance()->paths().logsDir()).filePath(name);
}

bool LogModel::exportTo(const QUrl &target)
{
    QFile f(target.isLocalFile() ? target.toLocalFile() : target.toString());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        AppController::instance()->prompts()->error(core::i18n::tr("log.export_failed_title"), f.errorString());
        return false;
    }
    f.write(allText().toUtf8());
    f.write("\n");
    return true;
}
