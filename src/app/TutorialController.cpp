#include "TutorialController.h"

#include "AppController.h"

#include "core/i18n/Translator.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(lcApp)

TutorialController::TutorialController(QObject *parent) : QObject(parent) {}

void TutorialController::load()
{
    steps_.clear();
    QFile f(QStringLiteral(":/tutorial.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcApp) << "tutorial.json missing; tour disabled";
        return;
    }
    const QString app = QCoreApplication::applicationName();
    const auto translated = [](const QString &key, const QString &fallback) {
        const QString t = core::i18n::tr(key.toUtf8().constData());
        return t == key || t.isEmpty() ? fallback : t;
    };
    for (const auto steps = QJsonDocument::fromJson(f.readAll()).object().value(u"steps").toArray();
         const QJsonValue &v : steps) {
        const QJsonObject o = v.toObject();
        const QString id = o.value(u"id").toString();
        QString title = o.value(u"title").toString();
        QString description = o.value(u"description").toString();
        if (id.isEmpty() || title.isEmpty() || description.isEmpty())
            continue;
        title = translated(QStringLiteral("tutorial.%1.title").arg(id), title);
        description = translated(QStringLiteral("tutorial.%1.description").arg(id), description);
        title.replace(QStringLiteral("{app}"), app);
        description.replace(QStringLiteral("{app}"), app);
        steps_.push_back(QVariantMap{{QStringLiteral("id"), id},
                                     {QStringLiteral("page"), o.value(u"page").toString()},
                                     {QStringLiteral("target"), o.value(u"target").toString()},
                                     {QStringLiteral("side"), o.value(u"side").toString(QStringLiteral("auto"))},
                                     {QStringLiteral("title"), title},
                                     {QStringLiteral("description"), description}});
    }
}

QVariantMap TutorialController::step() const
{
    return running_ && index_ < steps_.size() ? steps_[index_].toMap() : QVariantMap{};
}

bool TutorialController::shouldAutoStart() const
{
    core::Settings &s = AppController::instance()->settings();
    if (s.uiMode() == u"simple" || s.tutorialDisabled())
        return false;
    return s.tutorialCompletedVersion() != QCoreApplication::applicationVersion();
}

void TutorialController::start()
{
    load(); // re-read, so a language switch shows in a replay
    if (steps_.isEmpty()) {
        emit finished(false);
        return;
    }
    index_ = 0;
    running_ = true;
    emit changed();
}

void TutorialController::next()
{
    if (!running_)
        return;
    if (index_ + 1 >= steps_.size()) {
        finish(true);
        return;
    }
    ++index_;
    emit changed();
}

void TutorialController::back()
{
    if (running_ && index_ > 0) {
        --index_;
        emit changed();
    }
}

void TutorialController::skip()
{
    if (running_)
        finish(false);
}

void TutorialController::finish(bool completed)
{
    running_ = false;
    AppController::instance()->settings().setTutorialCompletedVersion(QCoreApplication::applicationVersion());
    emit changed();
    emit finished(completed);
}
