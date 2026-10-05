#include "PromptService.h"

#include "core/i18n/Translator.h"

#include <QTimer>

using core::i18n::tr;

PromptService::PromptService(QObject *parent) : QObject(parent)
{}

void PromptService::ask(Prompt prompt, Answer answer)
{
    queue_.emplace_back(std::move(prompt), std::move(answer));
    if (current_.isEmpty())
        showNext();
}

void PromptService::info(const QString &title, const QString &text, const QString &detail)
{
    Prompt p;
    p.kind = Kind::Info;
    p.title = title;
    p.text = text;
    p.detail = detail;
    ask(std::move(p));
}

void PromptService::warning(const QString &title, const QString &text, const QString &detail)
{
    Prompt p;
    p.kind = Kind::Warning;
    p.title = title;
    p.text = text;
    p.detail = detail;
    ask(std::move(p));
}

void PromptService::error(const QString &title, const QString &text, const QString &detail)
{
    Prompt p;
    p.kind = Kind::Error;
    p.title = title;
    p.text = text;
    p.detail = detail;
    ask(std::move(p));
}

void PromptService::confirm(const QString &title, const QString &text, std::function<void()> yes, Kind kind,
                            std::function<void()> no)
{
    Prompt p;
    p.kind = kind;
    p.title = title;
    p.text = text;
    p.buttons = {tr("scx.yes"), tr("scx.no")};
    if (p.buttons[0] == u"scx.yes")
        p.buttons = {QStringLiteral("Yes"), QStringLiteral("No")};
    p.defaultButton = 1;
    ask(std::move(p), [yes = std::move(yes), no = std::move(no)](int button, bool, int) {
        if (button == 0) {
            if (yes)
                yes();
        } else if (no) {
            no();
        }
    });
}

void PromptService::answer(int button, bool checked, int choice)
{
    if (current_.isEmpty())
        return;
    Answer callback = std::move(pending_);
    pending_ = {};
    current_.clear();
    emit currentChanged();
    if (callback)
        callback(button, checked, choice);
    // Let QML close the dialog before the next one opens.
    QTimer::singleShot(0, this, [this] {
        if (current_.isEmpty())
            showNext();
    });
}

void PromptService::showNext()
{
    if (queue_.empty())
        return;
    auto [prompt, answer] = std::move(queue_.front());
    queue_.pop_front();
    pending_ = std::move(answer);
    static const char *const kinds[] = {"info", "warning", "error", "question"};
    QStringList buttons = prompt.buttons;
    if (buttons.isEmpty())
        buttons << QStringLiteral("OK");
    current_ = {
        {QStringLiteral("kind"), QString::fromLatin1(kinds[static_cast<int>(prompt.kind)])},
        {QStringLiteral("title"), prompt.title},
        {QStringLiteral("text"), prompt.text},
        {QStringLiteral("detail"), prompt.detail},
        {QStringLiteral("buttons"), buttons},
        {QStringLiteral("defaultButton"), prompt.defaultButton},
        {QStringLiteral("checkbox"), prompt.checkbox},
        {QStringLiteral("choices"), prompt.choices},
    };
    emit currentChanged();
}
