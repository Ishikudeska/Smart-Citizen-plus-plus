#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// The guided tour's steps and progress. Steps come from :/tutorial.json
// (page, spotlight target, side, copy), with a language's
// tutorial.<id>.title / .description overriding the copy. QML draws the
// coach marks; this only sequences them. Ports TutorialTour and
// MainWindow's tutorial bookkeeping.
class TutorialController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(int index READ index NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(QVariantMap step READ step NOTIFY changed)

public:
    explicit TutorialController(QObject *parent = nullptr);

    bool running() const { return running_; }
    int index() const { return index_; }
    int count() const { return static_cast<int>(steps_.size()); }
    QVariantMap step() const;

    // On a version's first launch, unless disabled or in Simple mode.
    Q_INVOKABLE bool shouldAutoStart() const;
    Q_INVOKABLE void start();
    Q_INVOKABLE void next();
    Q_INVOKABLE void back();
    Q_INVOKABLE void skip();

signals:
    void changed();
    // Finish or Skip; either way the tour counts as seen for this version.
    void finished(bool completed);

private:
    void load();
    void finish(bool completed);

    QVariantList steps_;
    int index_ = 0;
    bool running_ = false;
};
