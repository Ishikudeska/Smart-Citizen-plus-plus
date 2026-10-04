#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <deque>
#include <functional>

// Message boxes for C++ flows. A flow calls ask() with a callback and
// carries on there; QML's PromptHost shows `current` and calls answer().
// Prompts queue, so two flows never stack dialogs on each other.
class PromptService : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by App")

    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(bool active READ active NOTIFY currentChanged)

public:
    enum class Kind { Info, Warning, Error, Question };

    struct Prompt
    {
        Kind kind = Kind::Info;
        QString title;
        QString text;
        QString detail;      // optional monospace block (paths, error text)
        QStringList buttons; // empty: a single OK
        int defaultButton = 0;
        QString checkbox;    // optional "don't ask again" style checkbox
        QStringList choices; // optional list to pick from (answered as `choice`)
    };

    // button: the index pressed (-1 when dismissed); checked: the checkbox;
    // choice: index into `choices`, or -1.
    using Answer = std::function<void(int button, bool checked, int choice)>;

    explicit PromptService(QObject *parent = nullptr);

    void ask(Prompt prompt, Answer answer = {});
    // Shorthands.
    void info(const QString &title, const QString &text, const QString &detail = {});
    void warning(const QString &title, const QString &text, const QString &detail = {});
    void error(const QString &title, const QString &text, const QString &detail = {});
    // Yes / No; `yes` runs on Yes only.
    void confirm(const QString &title, const QString &text, std::function<void()> yes,
                 Kind kind = Kind::Question, std::function<void()> no = {});

    QVariantMap current() const { return current_; }
    bool active() const { return !current_.isEmpty(); }

    Q_INVOKABLE void answer(int button, bool checked = false, int choice = -1);

signals:
    void currentChanged();

private:
    void showNext();

    std::deque<std::pair<Prompt, Answer>> queue_;
    Answer pending_;
    QVariantMap current_;
};
