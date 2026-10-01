// Ports test_ini_encoding_fallback.py (#251) and the INI parsing cases from
// test_core.py / test_favorite_prefix_whitespace.py.

#include "core/text/IniFile.h"
#include "core/text/IniText.h"
#include "core/text/PyText.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace core;

namespace {

// Valid UTF-8 with plenty of multi-byte characters and ONE bare corrupt
// byte, with keys after it that a truncating parser would lose.
QByteArray corruptedUtf8()
{
    return QStringLiteral("vehicle_NameHunter=Drake Cutlass Black\n"
                          "item_NameSHLD_Aspirum=Aspirum™ Shield — Mk. II\n"
                          "npc_name=Sebastián Muñoz\n")
               .toUtf8() +
           QByteArray("corrupt_line=before\xA0" "after\n") +
           QStringLiteral("key_after_bad_byte=must survive\nlast=entrée\n").toUtf8();
}

// A whole file in Windows-1252: every high byte is invalid UTF-8 on its own.
QByteArray cp1252File()
{
    return QByteArray("vehicle_NameHunter=Drake Cutlass Black\n"
                      "mission_prompt=Continuer\xA0?\n"
                      "item_NameSHLD_Aspirum=Bouclier \xAB\xA0" "Aspirum\xA0\xBB\n"
                      "key_after_bad_byte=must survive\n");
}

QString writeTemp(const QTemporaryDir &dir, const QString &name, const QByteArray &bytes)
{
    QFile f(dir.filePath(name));
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot open %s", qPrintable(f.fileName()));
    f.write(bytes);
    return f.fileName();
}

} // namespace

class TestText : public QObject
{
    Q_OBJECT

private slots:
    void pythonStrip()
    {
        QCOMPARE(py::strip(u"  \t a b \r\n"), QStringLiteral("a b"));
        QCOMPARE(py::strip(QStringView(u"\x1c" "x\x1f")), QStringLiteral("x")); // Python counts these as whitespace
        QCOMPARE(py::strip(QString(QChar(0xA0)) + QStringLiteral("x")), QStringLiteral("x"));
        QCOMPARE(py::rstrip(QStringView(u"a=b\r\r"), u'\r').toString(), QStringLiteral("a=b"));
    }

    void cleanUtf8()
    {
        const IniText t = decodeIniText(QStringLiteral("clé=révisé — ✓\n").toUtf8());
        QCOMPARE(t.encoding, IniEncoding::Utf8);
        QCOMPARE(t.text, QStringLiteral("clé=révisé — ✓\n"));
    }

    void bomIsDropped()
    {
        const IniText t = decodeIniText(QByteArray("\xEF\xBB\xBFk=v\n"));
        QCOMPARE(t.text, QStringLiteral("k=v\n"));
    }

    void corruptedUtf8KeepsMultibyte()
    {
        const IniText t = decodeIniText(corruptedUtf8());
        QCOMPARE(t.encoding, IniEncoding::Utf8Repaired);
        const IniMap m = parseIni(t.text);
        QCOMPARE(m.size(), 6);
        QCOMPARE(m.value(QStringLiteral("key_after_bad_byte")), QStringLiteral("must survive"));
        QCOMPARE(m.value(QStringLiteral("item_NameSHLD_Aspirum")), QStringLiteral("Aspirum™ Shield — Mk. II"));
        QCOMPARE(m.value(QStringLiteral("npc_name")), QStringLiteral("Sebastián Muñoz"));
        QCOMPARE(m.value(QStringLiteral("corrupt_line")), QStringLiteral("before") + QChar(0xFFFD) + QStringLiteral("after"));
    }

    void windows1252FileParsesCompletely()
    {
        const IniText t = decodeIniText(cp1252File());
        QCOMPARE(t.encoding, IniEncoding::Windows1252);
        const IniMap m = parseIni(t.text);
        QCOMPARE(m.size(), 4);
        QCOMPARE(m.value(QStringLiteral("mission_prompt")), QStringLiteral("Continuer") + QChar(0xA0) + u'?');
        QCOMPARE(m.value(QStringLiteral("item_NameSHLD_Aspirum")),
                 QStringLiteral("Bouclier «") + QChar(0xA0) + QStringLiteral("Aspirum") + QChar(0xA0) + u'»');
    }

    void undefinedCp1252ByteDoesNotCrash()
    {
        const IniMap m = parseIni(decodeIniText(QByteArray("good_key=ok\nweird=\x81\nlater_key=still here\n")).text);
        QCOMPARE(m.value(QStringLiteral("good_key")), QStringLiteral("ok"));
        QCOMPARE(m.value(QStringLiteral("later_key")), QStringLiteral("still here"));
    }

    void bomThenCp1252Body()
    {
        const IniMap m = parseIni(decodeIniText(QByteArray("\xEF\xBB\xBFk=v\xA0!\n")).text);
        QCOMPARE(m.size(), 1);
        QCOMPARE(m.value(QStringLiteral("k")), QStringLiteral("v") + QChar(0xA0) + u'!');
    }

    void parseRules()
    {
        const IniMap m = parseIni(QStringLiteral("; comment\n"
                                                 "\n"
                                                 "   \n"
                                                 "no equals sign\n"
                                                 "  spaced key  =  spaced value  \r\n"
                                                 "key,P=metadata stripped\n"
                                                 "k=a=b\n"
                                                 "=no key\n"
                                                 ",P=empty clean key\n"
                                                 "dup=first\n"
                                                 "dup=second\n"));
        QCOMPARE(m.size(), 4);
        QCOMPARE(m.value(QStringLiteral("spaced key")), QStringLiteral("spaced value"));
        QCOMPARE(m.value(QStringLiteral("key")), QStringLiteral("metadata stripped"));
        QCOMPARE(m.value(QStringLiteral("k")), QStringLiteral("a=b"));
        QCOMPARE(m.value(QStringLiteral("dup")), QStringLiteral("second"));
        // Re-assigning keeps the original position, like a Python dict.
        QCOMPARE(m.entries().last().first, QStringLiteral("dup"));
        QCOMPARE(m.entries().first().first, QStringLiteral("spaced key"));
    }

    // #100: a single-space favourite prefix must survive user.ini.
    void unstrippedValuesKeepSpaces()
    {
        const IniMap m = parseIni(QStringLiteral("vehicle_NameX= Cutlass\n"), /*stripValues=*/false);
        QCOMPARE(m.value(QStringLiteral("vehicle_NameX")), QStringLiteral(" Cutlass"));
    }

    void doesNotSplitOnUnicodeLineSeparators()
    {
        const QString text = QStringLiteral("a=x") + QChar(0x2028) + QStringLiteral("y\nb=z\n");
        const IniMap m = parseIni(text);
        QCOMPARE(m.size(), 2);
        QCOMPARE(m.value(QStringLiteral("a")), QStringLiteral("x") + QChar(0x2028) + u'y');
    }

    void writesCrlfWithoutBom()
    {
        QTemporaryDir dir;
        IniMap m;
        m.insert(QStringLiteral("b"), QStringLiteral("2"));
        m.insert(QStringLiteral("a"), QStringLiteral("é"));
        const QString path = dir.filePath(QStringLiteral("sub/out.ini"));
        QVERIFY(writeIniFile(path, m));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QStringLiteral("b=2\r\na=é\r\n").toUtf8());
    }

    void loadsFiles()
    {
        QTemporaryDir dir;
        const QString path = writeTemp(dir, QStringLiteral("x.ini"), corruptedUtf8());
        QCOMPARE(loadIni(path).size(), 6);
        QVERIFY(loadIni(dir.filePath(QStringLiteral("missing.ini"))).isEmpty());
    }

    void mapRemoveKeepsOrder()
    {
        IniMap m;
        for (const char *k : {"a", "b", "c", "d"})
            m.insert(QString::fromLatin1(k), QStringLiteral("v"));
        QVERIFY(m.remove(QStringLiteral("b")));
        QVERIFY(!m.remove(QStringLiteral("b")));
        QCOMPARE(m.size(), 3);
        QCOMPARE(m.entries()[1].first, QStringLiteral("c"));
        m.insert(QStringLiteral("c"), QStringLiteral("new"));
        QCOMPARE(m.value(QStringLiteral("c")), QStringLiteral("new"));
        QCOMPARE(m.value(QStringLiteral("d")), QStringLiteral("v"));
    }
};

QTEST_GUILESS_MAIN(TestText)
#include "tst_text.moc"
