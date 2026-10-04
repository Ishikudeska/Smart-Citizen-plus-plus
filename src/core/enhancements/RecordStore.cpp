#include "core/enhancements/RecordStore.h"

#include "core/EnginePaths.h"
#include "core/text/PyText.h"
#include "engine/io/FileSystem.h"

#include <QRegularExpression>

#include <algorithm>

namespace core::enh {

namespace {

QString parentOf(const QString &rel)
{
    const qsizetype slash = rel.lastIndexOf(u'/');
    return slash < 0 ? QString() : rel.first(slash);
}

QString nameOf(const QString &rel)
{
    return rel.sliced(rel.lastIndexOf(u'/') + 1);
}

QString join(const QString &dir, const QString &name)
{
    return dir.isEmpty() ? name : dir + u'/' + name;
}

QString normalizedRel(QString rel)
{
    rel.replace(u'\\', u'/');
    while (rel.endsWith(u'/'))
        rel.chop(1);
    while (rel.startsWith(QStringLiteral("./")))
        rel.remove(0, 2);
    return rel == u"." ? QString() : rel;
}

} // namespace

bool RecordStore::ntfsLess(const QString &a, const QString &b)
{
    const qsizetype n = std::min(a.size(), b.size());
    for (qsizetype i = 0; i < n; ++i) {
        const char16_t x = a[i].toUpper().unicode();
        const char16_t y = b[i].toUpper().unicode();
        if (x != y)
            return x < y;
    }
    return a.size() < b.size();
}

std::shared_ptr<const RecordStore> RecordStore::scan(const QString &recordsDir)
{
    auto store = std::shared_ptr<RecordStore>(new RecordStore);
    store->root_ = QString(recordsDir).replace(u'\\', u'/');
    while (store->root_.endsWith(u'/'))
        store->root_.chop(1);
    if (!engine::io::exists(fsPath(store->root_)))
        return store;

    QHash<QString, Dir> &dirs = store->dirs_;
    dirs.insert(QString(), Dir{});
    engine::io::forEachFile(fsPath(store->root_), [&](std::string_view relative) {
        const QString rel = QString::fromUtf8(relative.data(), qsizetype(relative.size()));
        if (!rel.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive))
            return;
        QString dir = parentOf(rel);
        dirs[dir].files << nameOf(rel);
        ++store->fileCount_;
        // Register the directory chain up to the root.
        while (!dir.isEmpty()) {
            const QString parent = parentOf(dir);
            Dir &p = dirs[parent];
            const QString name = nameOf(dir);
            if (p.subdirs.contains(name))
                break;
            p.subdirs << name;
            dir = parent;
        }
    });
    for (Dir &d : dirs) {
        std::sort(d.subdirs.begin(), d.subdirs.end(), ntfsLess);
        std::sort(d.files.begin(), d.files.end(), ntfsLess);
    }
    for (const auto walked = store->walkOrder(QString()); const QString &dir : walked)
        if (!dirs.value(dir).files.isEmpty())
            store->indexOrder_ << dir;
    return store;
}

QString RecordStore::absolute(const QString &relative) const
{
    const QString rel = normalizedRel(relative);
    return rel.isEmpty() ? root_ : root_ + u'/' + rel;
}

bool RecordStore::dirExists(const QString &relative) const
{
    return dirs_.contains(normalizedRel(relative));
}

QStringList RecordStore::walkOrder(const QString &rel) const
{
    QStringList order;
    if (!dirs_.contains(rel))
        return order;
    order << rel;
    QStringList stack{rel};
    while (!stack.isEmpty()) {
        const QString path = stack.takeLast();
        for (const auto names = dirs_.value(path).subdirs; const QString &name : names) {
            const QString child = join(path, name);
            order << child;
            stack << child;
        }
    }
    return order;
}

QStringList RecordStore::indexRglob(const QString &relDir) const
{
    const QString prefix = normalizedRel(relDir);
    QStringList out;
    for (const QString &dir : indexOrder_) {
        if (dir != prefix && !(prefix.isEmpty() ? false : dir.startsWith(prefix + u'/')))
            continue;
        QStringList files = dirs_.value(dir).files;
        // The index sorts each directory's path strings.
        std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) { return py::less(a, b); });
        for (const QString &f : std::as_const(files))
            out << absolute(join(dir, f));
    }
    return out;
}

QStringList RecordStore::filesIn(const QString &relDir) const
{
    const QString dir = normalizedRel(relDir);
    QStringList files = dirs_.value(dir).files;
    std::sort(files.begin(), files.end(), [](const QString &a, const QString &b) { return py::less(a, b); });
    QStringList out;
    for (const QString &f : std::as_const(files))
        out << absolute(join(dir, f));
    return out;
}

QStringList RecordStore::rglob(const QString &relDir) const
{
    QStringList out;
    for (const auto dirs = walkOrder(normalizedRel(relDir)); const QString &dir : dirs)
        for (const auto files = dirs_.value(dir).files; const QString &f : files)
            out << absolute(join(dir, f));
    return out;
}

QStringList RecordStore::glob(const QString &relDir, const QString &pattern) const
{
    const QRegularExpression re(QRegularExpression::anchoredPattern(QRegularExpression::wildcardToRegularExpression(
                                    pattern, QRegularExpression::UnanchoredWildcardConversion)),
                                QRegularExpression::CaseInsensitiveOption);
    const QString dir = normalizedRel(relDir);
    QStringList out;
    for (const auto files = dirs_.value(dir).files; const QString &f : files)
        if (re.match(f).hasMatch())
            out << absolute(join(dir, f));
    return out;
}

} // namespace core::enh
