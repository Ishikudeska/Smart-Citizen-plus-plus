#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// The Log page: the session's log lines (the last 2,000 at or above the
// chosen level), live as they are logged. Ports log_tab.py.
class LogModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int minLevel READ minLevel WRITE setMinLevel NOTIFY minLevelChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { TextRole = Qt::UserRole + 1, LevelRole, ColorRole };

    explicit LogModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int minLevel() const { return minLevel_; }
    void setMinLevel(int level);

    Q_INVOKABLE void clear();
    Q_INVOKABLE QString defaultExportPath() const;
    Q_INVOKABLE bool exportTo(const QUrl &target);
    Q_INVOKABLE QString allText() const;

signals:
    void minLevelChanged();
    void countChanged();

private:
    struct Line
    {
        QString text;
        int level = 20;
    };
    static int levelOf(const QString &line);
    void append(const QString &line, int level);
    void rebuild();

    QList<Line> all_;     // everything since the last clear (bounded)
    QList<Line> visible_; // at or above minLevel_
    int minLevel_ = 10;
};
