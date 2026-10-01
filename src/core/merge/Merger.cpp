#include "core/merge/Merger.h"

#include <QHash>

#include <array>

namespace core {

namespace {

// Order matters: the replacements run in this sequence, as in the Python.
constexpr std::array kComponentCodes = {u"shld", u"powr", u"cool", u"qdrv", u"jump", u"misl", u"gmisl", u"bomb"};

bool isItemKey(const QString &key)
{
    return key.startsWith(u"item_name", Qt::CaseInsensitive) || key.startsWith(u"item_desc", Qt::CaseInsensitive);
}

// Python's len(): code points, not UTF-16 units.
qsizetype codePoints(const QString &s)
{
    qsizetype n = s.size();
    for (const QChar c : s)
        n -= c.isLowSurrogate() ? 1 : 0;
    return n;
}

} // namespace

QString canonicalItemKey(const QString &key)
{
    QString k = key;
    if (k.size() >= 7 && k.right(7).compare(u"_scitem", Qt::CaseInsensitive) == 0)
        k.chop(7);
    k = k.toLower();
    k.remove(u'_');

    bool hasCode = false;
    for (const char16_t *code : kComponentCodes)
        hasCode = hasCode || k.contains(QStringView(code));
    if (!hasCode)
        return k;

    for (const char16_t *code : kComponentCodes)
        k.replace(QString::fromUtf16(code), QString(u'_') + QString::fromUtf16(code));
    // Collapse runs of '_' and drop leading/trailing ones.
    const QStringList parts = k.split(u'_', Qt::SkipEmptyParts);
    return parts.join(u'_');
}

void syncKeyVariants(IniMap &merged, const QSet<QString> &userEditedKeys)
{
    // canonical form -> variants, both in first-seen order
    QHash<QString, qsizetype> groupIndex;
    QList<QList<QString>> groups;
    for (const auto &[key, value] : merged) {
        if (!isItemKey(key))
            continue;
        const QString canonical = canonicalItemKey(key);
        auto it = groupIndex.constFind(canonical);
        if (it == groupIndex.cend()) {
            it = groupIndex.insert(canonical, groups.size());
            groups.append(QList<QString>());
        }
        groups[*it].append(key);
    }

    for (const QList<QString> &variants : groups) {
        if (variants.size() < 2)
            continue;
        QList<QString> candidates;
        for (const QString &v : variants)
            if (userEditedKeys.contains(v))
                candidates.append(v);
        if (candidates.isEmpty())
            candidates = variants;

        // Python's max(): the first of the longest.
        const QString *best = nullptr;
        qsizetype bestLength = -1;
        for (const QString &c : candidates) {
            const QString *value = merged.find(c);
            const qsizetype length = codePoints(*value);
            if (length > bestLength) {
                best = value;
                bestLength = length;
            }
        }
        const QString synced = *best;
        for (const QString &v : variants)
            merged.insert(v, synced);
    }
}

IniMap mergeSourcesByHierarchy(const SourceMap &sources, const QStringList &hierarchy, const IniMap *userOverrides)
{
    IniMap result;
    for (const QString &name : hierarchy) {
        const auto it = sources.find(name);
        if (it == sources.end())
            continue;
        result.reserve(result.size() + it->second.size());
        for (const auto &[key, value] : it->second)
            result.insert(key, value);
    }

    QSet<QString> edited;
    if (userOverrides) {
        for (const auto &[key, value] : *userOverrides) {
            result.insert(key, value);
            edited.insert(key);
        }
    }
    syncKeyVariants(result, edited);
    return result;
}

} // namespace core
