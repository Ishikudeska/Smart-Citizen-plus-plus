#include "WindowLayout.h"

#include "AppController.h"
#include "PromptService.h"

#include "core/Settings.h"
#include "core/i18n/Translator.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace {

constexpr QLatin1StringView kWindowGeometryKey("window_geometry");
constexpr QLatin1StringView kColumnWidthsKey("string_column_widths");

QString text(const char *key)
{
    return core::i18n::tr(key);
}

} // namespace

WindowLayout::WindowLayout(QObject *parent) : QObject(parent) {}

AppController &WindowLayout::app() const
{
    return *AppController::instance();
}

QVariantMap WindowLayout::windowGeometry() const
{
    // "x,y,width,height,maximized"; anything else (e.g. Smart Citizen's
    // saveGeometry() bytes) reads as never saved.
    const QStringList parts = app().settings().value(kWindowGeometryKey).toString().split(u',');
    if (parts.size() != 5)
        return {};
    int v[4];
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        v[i] = parts[i].toInt(&ok);
        if (!ok)
            return {};
    }
    if (v[2] < 200 || v[3] < 150)
        return {};
    return {{QStringLiteral("x"), v[0]}, {QStringLiteral("y"), v[1]}, {QStringLiteral("width"), v[2]},
            {QStringLiteral("height"), v[3]}, {QStringLiteral("maximized"), parts[4] == u"1"}};
}

void WindowLayout::saveWindowGeometry(const QVariantMap &g)
{
    app().settings().setValue(kWindowGeometryKey, QStringLiteral("%1,%2,%3,%4,%5")
                                                      .arg(g.value(QStringLiteral("x")).toInt())
                                                      .arg(g.value(QStringLiteral("y")).toInt())
                                                      .arg(g.value(QStringLiteral("width")).toInt())
                                                      .arg(g.value(QStringLiteral("height")).toInt())
                                                      .arg(g.value(QStringLiteral("maximized")).toBool() ? 1 : 0));
}

QVariantList WindowLayout::columnWidths() const
{
    // A JSON list, as Smart Citizen stores it; malformed reads as never set.
    const QJsonDocument doc = QJsonDocument::fromJson(app().settings().value(kColumnWidthsKey).toString().toUtf8());
    QVariantList out;
    for (const auto values = doc.array(); const QJsonValue &v : values) {
        if (!v.isDouble())
            return {};
        out << v.toInt();
    }
    return out;
}

void WindowLayout::saveColumnWidths(const QVariantList &widths)
{
    QJsonArray array;
    for (const QVariant &w : widths)
        array.append(w.toInt());
    app().settings().setValue(kColumnWidthsKey, QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void WindowLayout::resetWindowProportions()
{
    app().prompts()->confirm(text("dialogs.reset_proportions_title"), text("dialogs.reset_proportions_body"), [this] {
        core::Settings &settings = app().settings();
        settings.remove(kWindowGeometryKey);
        settings.remove(QStringLiteral("window_state"));
        settings.remove(kColumnWidthsKey);
        settings.sync();
        emit windowProportionsReset();
    });
}
