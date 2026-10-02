#include "engine/p4k/TreeIndex.h"

#include <QTest>

#include <atomic>
#include <string>
#include <vector>

using engine::p4k::TreeIndex;

class TestTreeIndex : public QObject
{
    Q_OBJECT

    static QStringList childNames(const TreeIndex &t, TreeIndex::NodeId folder)
    {
        QStringList out;
        for (std::uint32_t r = 0; r < t.node(folder).childCount; ++r)
            out << QString::fromStdString(std::string(t.node(t.child(folder, r)).name));
        return out;
    }

private slots:
    void buildsSortedTree()
    {
        const std::vector<std::string> paths = {"Data/b.txt", "Data/Libs/x.xml", "Data/a.TXT", "Data/libs/y.xml",
                                                "Root.ini", "Data/Objects/", "Data/C/z.dds"};
        std::vector<TreeIndex::Item> items;
        for (std::size_t i = 0; i < paths.size(); ++i)
            items.push_back({paths[i], (i + 1) * 10, i + 1});
        const auto t = TreeIndex::build(items);
        QVERIFY(t);

        QCOMPARE(childNames(*t, t->root()), (QStringList{"Data", "Root.ini"}));
        const auto data = t->find("data");
        QVERIFY(data);
        // Folders first ("Libs" and "libs" merge into the first spelling), then files.
        QCOMPARE(childNames(*t, *data), (QStringList{"C", "Libs", "Objects", "a.TXT", "b.txt"}));
        const auto libs = t->find("DATA\\LIBS");
        QVERIFY(libs);
        QCOMPARE(childNames(*t, *libs), (QStringList{"x.xml", "y.xml"}));

        // Rows and parents line up.
        for (std::uint32_t r = 0; r < t->node(*data).childCount; ++r) {
            const auto c = t->child(*data, r);
            QCOMPARE(t->node(c).row, r);
            QCOMPARE(t->node(c).parent, *data);
        }

        // Totals roll up; the folder entry adds nothing.
        QCOMPARE(t->node(t->root()).files, 6u);
        QCOMPARE(t->node(*libs).size, std::uint64_t(20 + 40));
        QCOMPARE(t->node(*libs).packed, std::uint64_t(2 + 4));
        QCOMPARE(t->node(t->root()).size, std::uint64_t(10 + 20 + 30 + 40 + 50 + 70));

        QCOMPARE(QString::fromStdString(t->path(t->nodeOfItem(3))), QStringLiteral("Data/Libs/y.xml"));
        QCOMPARE(t->nodeOfItem(5), *t->find("Data/Objects"));
        QVERIFY(t->isFolder(*t->find("Data/Objects")));
        QVERIFY(!t->find("Data/nope"));
        QCOMPARE(*t->find(""), t->root());

        std::vector<std::uint32_t> under;
        t->collectItems(*data, under);
        QCOMPARE(under, (std::vector<std::uint32_t>{6, 1, 3, 2, 0}));
    }

    void fileAndFolderShareName()
    {
        const std::vector<std::string> paths = {"a/b", "a/b/c"};
        std::vector<TreeIndex::Item> items;
        for (const auto &p : paths)
            items.push_back({p, 1, 1});
        const auto t = TreeIndex::build(items);
        QVERIFY(t);
        QVERIFY(!t->isFolder(*t->find("a/b")));
        QCOMPARE(QString::fromStdString(t->path(*t->find("a/b/c"))), QStringLiteral("a/b/c"));
    }

    void cancels()
    {
        const std::string p = "x/y";
        const std::vector<TreeIndex::Item> items{{p, 1, 1}};
        std::atomic<bool> cancel{true};
        QVERIFY(!TreeIndex::build(items, &cancel));
    }
};

QTEST_APPLESS_MAIN(TestTreeIndex)
#include "tst_treeindex.moc"
