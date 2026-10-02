#pragma once

#include <QObject>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class AppController;

// The Blueprint Tracker: blueprints the loaded missions can reward, split
// into available and owned, with search and facet filters; owning one
// weaves [Owned] into mission texts. Also scans the game's logs for
// received blueprints and imports/exports the owned list. Ports
// blueprint_tracker_tab.py and its main_window.py handlers.
class BlueprintController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList available READ available NOTIFY listsChanged)
    Q_PROPERTY(QVariantList owned READ owned NOTIFY listsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY listsChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filtersChanged)
    Q_PROPERTY(QString mission READ mission WRITE setMission NOTIFY filtersChanged)
    Q_PROPERTY(QString type READ type WRITE setType NOTIFY filtersChanged)
    Q_PROPERTY(QString cls READ cls WRITE setCls NOTIFY filtersChanged)
    Q_PROPERTY(QString size READ size WRITE setSize NOTIFY filtersChanged)
    Q_PROPERTY(QString grade READ grade WRITE setGrade NOTIFY filtersChanged)
    Q_PROPERTY(QStringList missions READ missions NOTIFY listsChanged)
    Q_PROPERTY(QStringList types READ types NOTIFY listsChanged)
    Q_PROPERTY(QStringList classes READ classes NOTIFY listsChanged)
    Q_PROPERTY(QStringList sizes READ sizes NOTIFY listsChanged)
    Q_PROPERTY(QStringList grades READ grades NOTIFY listsChanged)
    Q_PROPERTY(bool showTags READ showTags WRITE setShowTags NOTIFY listsChanged)
    Q_PROPERTY(bool scanOtherChannels READ scanOtherChannels WRITE setScanOtherChannels NOTIFY optionsChanged)
    Q_PROPERTY(bool forceRescan READ forceRescan WRITE setForceRescan NOTIFY optionsChanged)
    Q_PROPERTY(bool ownedDirty READ ownedDirty NOTIFY optionsChanged)

public:
    explicit BlueprintController(QObject *parent = nullptr);

    QVariantList available() const { return available_; }
    QVariantList owned() const { return owned_; }
    int totalCount() const;
    QString search() const { return search_; }
    void setSearch(const QString &v);
    QString mission() const { return mission_; }
    void setMission(const QString &v);
    QString type() const { return type_; }
    void setType(const QString &v);
    QString cls() const { return cls_; }
    void setCls(const QString &v);
    QString size() const { return size_; }
    void setSize(const QString &v);
    QString grade() const { return grade_; }
    void setGrade(const QString &v);
    QStringList missions() const { return missions_; }
    QStringList types() const { return types_; }
    QStringList classes() const { return classes_; }
    QStringList sizes() const { return sizes_; }
    QStringList grades() const { return grades_; }
    bool showTags() const;
    void setShowTags(bool on);
    bool scanOtherChannels() const;
    void setScanOtherChannels(bool on);
    bool forceRescan() const { return forceRescan_; }
    void setForceRescan(bool on);
    bool ownedDirty() const { return ownedDirty_; }

    Q_INVOKABLE void own(const QStringList &names);
    Q_INVOKABLE void unown(const QStringList &names);
    Q_INVOKABLE void applyOwnedTags();
    Q_INVOKABLE void scanLogs();
    Q_INVOKABLE QString defaultExportPath(bool csv) const;
    Q_INVOKABLE void exportOwned(const QUrl &target);
    Q_INVOKABLE void importOwned(const QUrl &source);

signals:
    void listsChanged();
    void filtersChanged();
    void optionsChanged();

private:
    AppController &app() const;
    void rebuild();
    bool visible(const QString &name) const;
    QVariantMap row(const QString &name) const;
    void setFilter(QString &field, const QString &value);
    void scanNext(QStringList queue, QSet<QString> found);
    void finishScan(const QSet<QString> &found);

    QVariantList available_, owned_;
    QStringList missions_, types_, classes_, sizes_, grades_;
    QString search_, mission_, type_, cls_, size_, grade_;
    bool forceRescan_ = false;
    bool ownedDirty_ = false;
};
