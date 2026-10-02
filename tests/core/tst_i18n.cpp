// UI text lookup. Ports test_i18n.py and test_available_languages.py, and
// loads every shipped ui.json.

#include "core/i18n/Translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace core::i18n;

namespace {

QString s(const char *utf8)
{
    return QString::fromUtf8(utf8);
}

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(text).object();
}

void writeLang(const QTemporaryDir &root, const char *lang, const QByteArray &text)
{
    QDir().mkpath(root.filePath(s(lang)));
    QFile f(root.filePath(s(lang) + s("/ui.json")));
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot write");
    f.write(text);
}

const char *const kEnglish = R"js({
  "toolbar": {"apply_btn": "Apply to Game", "more_btn": "More"},
  "status": {"ready": "Ready"},
  "dialogs": {"export_complete": "Exported to {path} ({size} bytes)"},
  "strings_tab": {"copied_key": "Copied: {key}"}
})js";

QString shipped()
{
    return QStringLiteral(SC_SOURCE_DIR "/resources/languages");
}

} // namespace

class TestI18n : public QObject
{
    Q_OBJECT

private slots:
    void lookups()
    {
        const Catalog en = Catalog::fromTrees(kDefaultLanguage, json(kEnglish));
        QCOMPARE(en.lookup(s("toolbar.apply_btn")), s("Apply to Game"));
        QVERIFY(en.lookup(s("toolbar.nope")).isNull());
        QVERIFY(en.lookup(s("toolbar.apply_btn.deeper")).isNull());
        QVERIFY(en.lookup(s("toolbar")).isNull()); // a section is not text
    }

    void overlayFallsBackToEnglish()
    {
        const Catalog fr = Catalog::fromTrees(s("french"), json(kEnglish),
                                              json(R"js({"toolbar": {"apply_btn": "Appliquer au jeu"}})js"));
        QCOMPARE(fr.lookup(s("toolbar.apply_btn")), s("Appliquer au jeu"));
        QCOMPARE(fr.lookup(s("toolbar.more_btn")), s("More"));
        QCOMPARE(fr.lookup(s("status.ready")), s("Ready"));
    }

    void humanAndAiLeaves()
    {
        const QJsonObject en = json(R"js({"toolbar": {"apply_btn": {"ht": "Apply to Game", "at": ""},
            "more_btn": {"ht": "More", "at": ""}, "help_btn": {"ht": "Help", "at": ""}}})js");
        const Catalog fr = Catalog::fromTrees(s("french"), en, json(R"js({
            "toolbar": {"apply_btn": {"ht": "Appliquer au jeu", "at": ""}, "more_btn": {"ht": "", "at": "Plus"},
                        "help_btn": {"ht": "", "at": ""}},
            "tutorial": {"step1": {"ht": "Étape un", "at": ""}}})js"));
        QCOMPARE(fr.lookup(s("toolbar.apply_btn")), s("Appliquer au jeu"));
        QCOMPARE(fr.lookup(s("toolbar.more_btn")), s("Plus"));
        QCOMPARE(fr.lookup(s("toolbar.help_btn")), s("Help"));
        QCOMPARE(fr.lookup(s("tutorial.step1")), s("Étape un"));
    }

    void deepMergeRules()
    {
        QJsonObject base = json(R"js({"a": {"x": "1", "y": "2"}, "b": "3"})js");
        deepMerge(base, json(R"js({"a": {"x": "10"}})js"));
        QCOMPARE(base, json(R"js({"a": {"x": "10", "y": "2"}, "b": "3"})js"));

        base = json(R"js({"a": {"x": "1"}})js");
        deepMerge(base, json(R"js({"a": "flat"})js"));
        QCOMPARE(base, json(R"js({"a": "flat"})js"));

        base = json(R"js({"k": {"ht": "A", "at": ""}})js");
        deepMerge(base, json(R"js({"k": {"ht": "", "at": "B"}})js"));
        QCOMPARE(base, json(R"js({"k": {"ht": "", "at": "B"}})js"));

        base = json(R"js({"k": {"ht": "A", "at": ""}})js");
        deepMerge(base, json(R"js({"k": {"ht": "", "at": ""}})js"));
        QCOMPARE(base, json(R"js({"k": {"ht": "A", "at": ""}})js"));
    }

    void loadFromDisk()
    {
        QTemporaryDir root;
        writeLang(root, "english", kEnglish);
        writeLang(root, "french", R"js({"toolbar": {"apply_btn": "Appliquer au jeu"}})js");
        writeLang(root, "german", "{not json");
        QCOMPARE(Catalog::load(root.path(), s("french")).lookup(s("toolbar.apply_btn")), s("Appliquer au jeu"));
        QCOMPARE(Catalog::load(root.path(), s("german")).lookup(s("toolbar.apply_btn")), s("Apply to Game"));
        QCOMPARE(Catalog::load(root.path(), s("klingon")).lookup(s("toolbar.apply_btn")), s("Apply to Game"));
        QCOMPARE(Catalog::load(root.path(), s("klingon")).language(), s("klingon"));
    }

    void formatting()
    {
        const QString t = s("Exported to {path} ({size} bytes)");
        QCOMPARE(format(t, {{s("path"), s("C:/out.zip")}, {s("size"), 42}}), s("Exported to C:/out.zip (42 bytes)"));
        QCOMPARE(format(t, {{s("wrong_kwarg"), 1}}), t);
        QCOMPARE(format(s("Copied: {key}"), {{s("key"), s("item_Name_Foo")}}), s("Copied: item_Name_Foo"));
        QCOMPARE(format(s("{count:,} items"), {{s("count"), 1234567}}), s("1,234,567 items"));
        QCOMPARE(format(s("{count:,}"), {{s("count"), -1234}}), s("-1,234"));
        QCOMPARE(format(s("{count:,}"), {{s("count"), s("x")}}), s("{count:,}"));
        QCOMPARE(format(s("{{literal}} {a}"), {{s("a"), true}}), s("{literal} True"));
        QCOMPARE(format(s("bad } brace {a}"), {{s("a"), 1}}), s("bad } brace {a}"));
        QCOMPARE(format(s("open {a"), {{s("a"), 1}}), s("open {a"));
    }

    void translatorServesQCoreApplication()
    {
        JsonTranslator translator;
        translator.setCatalog(std::make_shared<const Catalog>(Catalog::fromTrees(kDefaultLanguage, json(kEnglish))));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QCOMPARE(core::i18n::tr("toolbar.apply_btn"), s("Apply to Game"));
        QCOMPARE(core::i18n::tr("toolbar.nope"), s("toolbar.nope"));
        QCOMPARE(core::i18n::tr("status.ready", {{s("key"), s("some_loc_key")}}), s("Ready"));
        QCOMPARE(core::i18n::tr("dialogs.export_complete", {{s("path"), s("C:/out.zip")}, {s("size"), 42}}),
                 s("Exported to C:/out.zip (42 bytes)"));
        // Qt's own English source strings pass through.
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Cancel"), s("Cancel"));
        translator.setCatalog(nullptr);
        QCOMPARE(core::i18n::tr("toolbar.apply_btn"), s("toolbar.apply_btn"));
        QCoreApplication::removeTranslator(&translator);
    }

    void availableLanguagesHidesStubs()
    {
        QTemporaryDir root;
        writeLang(root, "english", kEnglish);
        writeLang(root, "french", R"js({"toolbar": {"apply_btn": "Appliquer"}})js");
        writeLang(root, "spanish", R"js({"_comment": "stub"})js");
        writeLang(root, "italian", R"js({"_comment": "x", "toolbar": {"_comment": "y"}})js");
        QDir().mkpath(root.filePath(s("german")));
        QCOMPARE(availableLanguages(root.path()), (QStringList{s("english"), s("french")}));

        QTemporaryDir stub;
        writeLang(stub, "english", R"js({"_comment": "only"})js");
        QCOMPARE(availableLanguages(stub.path()), QStringList{s("english")});
        QCOMPARE(availableLanguages(stub.filePath(s("missing"))), QStringList{s("english")});
    }

    // Every shipped language parses and resolves.
    void shippedLanguages()
    {
        const QStringList langs = availableLanguages(shipped());
        QVERIFY(langs.contains(s("english")));
        QVERIFY(langs.size() >= 7);
        const Catalog en = Catalog::load(shipped(), kDefaultLanguage);
        QCOMPARE(en.lookup(s("branding.title")), s("SMART CITIZEN"));
        for (const QString &lang : langs) {
            QFile f(shipped() + u'/' + lang + s("/ui.json"));
            QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(lang));
            QJsonParseError error;
            QJsonDocument::fromJson(f.readAll(), &error);
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(lang + u' ' + error.errorString()));
            QVERIFY(!Catalog::load(shipped(), lang).isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(TestI18n)
#include "tst_i18n.moc"
