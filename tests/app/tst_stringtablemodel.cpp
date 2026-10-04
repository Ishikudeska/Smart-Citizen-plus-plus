#include "StringTableModel.h"

#include <QSignalSpy>
#include <QTest>

using namespace core;
using namespace core::table;

// The String Editor's model over the core filter/sort functions: rows,
// search, edits and their counts, and sorting.
class TestStringTableModel : public QObject
{
    Q_OBJECT

    static StringEntry entry(const char *key, const char *value)
    {
        StringEntry e;
        e.key = QString::fromLatin1(key);
        e.sourceFile = QStringLiteral("global");
        e.category = extractCategory(e.key);
        e.originalValue = QString::fromLatin1(value);
        return e;
    }

    static QStringList keys(const StringTableModel &m)
    {
        QStringList out;
        for (int r = 0; r < m.rowCount(); ++r)
            out << m.key(r);
        return out;
    }

    static void load(StringTableModel &m)
    {
        IniMap defaults;
        defaults.insert(QStringLiteral("ui_Zeta"), QStringLiteral("Last"));
        defaults.insert(QStringLiteral("ui_alpha"), QStringLiteral("First Thing"));
        defaults.insert(QStringLiteral("ui_Mid"), QStringLiteral("Middle avenger"));
        m.setEntries({entry("ui_Zeta", "Last"), entry("ui_alpha", "First Thing"), entry("ui_Mid", "Middle avenger")},
                     defaults);
    }

private slots:
    void loadsAndSortsByKey()
    {
        StringTableModel m;
        QSignalSpy reset(&m, &StringTableModel::dataReset);
        load(m);
        QCOMPARE(reset.size(), 1);
        QCOMPARE(m.totalCount(), 3);
        QCOMPARE(m.visibleCount(), 3);
        QCOMPARE(keys(m), (QStringList{QStringLiteral("ui_alpha"), QStringLiteral("ui_Mid"), QStringLiteral("ui_Zeta")}));
        QCOMPARE(m.data(m.index(0, ColCurrent)).toString(), QStringLiteral("First Thing"));

        m.sortBy(ColKey); // same column again: descending
        QVERIFY(m.sortDescending());
        QCOMPARE(keys(m), (QStringList{QStringLiteral("ui_Zeta"), QStringLiteral("ui_Mid"), QStringLiteral("ui_alpha")}));
        m.sortBy(ColCurrent); // a new column starts ascending
        QVERIFY(!m.sortDescending());
        QCOMPARE(m.key(0), QStringLiteral("ui_alpha"));
    }

    void searchesCaseInsensitively()
    {
        StringTableModel m;
        load(m);
        QSignalSpy changed(&m, &StringTableModel::filtersChanged);
        m.setSearchText(QStringLiteral("AVENGER"));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(keys(m), QStringList{QStringLiteral("ui_Mid")});
        QCOMPARE(m.rowForKey(QStringLiteral("ui_Mid")), 0);
        QCOMPARE(m.rowForKey(QStringLiteral("ui_alpha")), -1);
        m.setSearchText(QStringLiteral("avenger")); // the same filter: no refilter, no signal
        QCOMPARE(changed.size(), 1);

        m.setColumnFilter(ColKey, QStringLiteral("zeta"));
        QCOMPARE(m.visibleCount(), 0); // both filters apply
        m.clearFilters();
        QCOMPARE(m.searchText(), QString());
        QCOMPARE(m.visibleCount(), 3);
    }

    void editsUpdateStatusAndCounts()
    {
        StringTableModel m;
        load(m);
        QSignalSpy edited(&m, &StringTableModel::edited);
        const int row = m.rowForKey(QStringLiteral("ui_Zeta"));
        m.setCustom(row, QStringLiteral("Changed"));
        QCOMPARE(edited.size(), 1);
        QCOMPARE(m.modifiedCount(), 1);
        QCOMPARE(m.customValue(row), QStringLiteral("Changed"));
        QCOMPARE(m.effectiveValue(row), QStringLiteral("Changed"));

        m.setHideUnmodified(true);
        QCOMPARE(keys(m), QStringList{QStringLiteral("ui_Zeta")});

        m.resetRow(0);
        QCOMPARE(edited.size(), 2);
        QCOMPARE(m.modifiedCount(), 0);
        QCOMPARE(m.customValue(0), QString());
        QCOMPARE(m.effectiveValue(0), QStringLiteral("Last"));
    }
};

QTEST_GUILESS_MAIN(TestStringTableModel)
#include "tst_stringtablemodel.moc"
