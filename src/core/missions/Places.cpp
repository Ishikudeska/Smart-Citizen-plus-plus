#include "core/missions/Places.h"

#include "core/enhancements/Common.h"

#include <QFile>
#include <QRegularExpression>

#include <algorithm>

namespace core::missions {

using enh::find;
using enh::findAll;
using enh::getOr;
using enh::Node;
using enh::qs;
using enh::XmlDoc;

// ── tags ───────────────────────────────────────────────────────────────

TagTable TagTable::load(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return parse(file.readAll());
}

TagTable TagTable::parse(const QByteArray &tsv)
{
    TagTable table;
    for (const QList<QByteArray> lines = tsv.split('\n'); const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 2 || fields[0].isEmpty())
            continue;
        const QString guid = QString::fromUtf8(fields[0]);
        const QString name = QString::fromUtf8(fields[1]);
        const QString parent = fields.size() > 2 ? QString::fromUtf8(fields[2].trimmed()) : QString();
        table.tags_.insert(guid, {name, parent});
        table.byName_[name] << guid;
        if (!parent.isEmpty())
            table.children_[parent] << guid;
    }
    return table;
}

QString TagTable::name(const QString &guid) const
{
    const auto it = tags_.constFind(guid);
    return it == tags_.cend() ? QString() : it->name;
}

QString TagTable::parent(const QString &guid) const
{
    const auto it = tags_.constFind(guid);
    return it == tags_.cend() ? QString() : it->parent;
}

QStringList TagTable::findByName(const QString &name) const
{
    return byName_.value(name);
}

QStringList TagTable::lineage(const QString &guid) const
{
    QStringList out;
    for (QString cur = guid; !cur.isEmpty() && !out.contains(cur); cur = parent(cur))
        out << cur;
    return out;
}

QStringList TagTable::children(const QString &guid) const
{
    return children_.value(guid);
}

// ── localization ───────────────────────────────────────────────────────

LocText::LocText(const IniMap &loc) : loc_(loc)
{
    lower_.reserve(loc.size());
    for (const auto &[key, value] : loc)
        lower_.insert(key.toLower(), &value);
}

QString LocText::operator()(QStringView ref) const
{
    while (ref.startsWith(u'@'))
        ref = ref.sliced(1);
    if (ref.isEmpty() || enh::isSentinelKey(ref))
        return {};
    const QString key = ref.toString();
    const QString *value = loc_.find(key);
    if (!value)
        value = lower_.value(key.toLower());
    // CIG's placeholders, "<= PLACEHOLDER =>" and "< / NOT AVAILABLE / >".
    static const QRegularExpression slashed(QStringLiteral(R"(^<\s*/.*/\s*>$)"));
    if (!value || enh::isPlaceholderText(*value) || slashed.match(*value).hasMatch())
        return {};
    return *value;
}

// ── searches ───────────────────────────────────────────────────────────

QString LocationSearch::key() const
{
    QStringList parts;
    for (const TagCondition &condition : conditions) {
        QStringList terms;
        for (const TagTerm &term : condition.terms)
            terms << term.positive.join(u',') + u'!' + term.negative.join(u',');
        parts << QString::number(int(condition.type)) + u':' + terms.join(u'|');
    }
    return parts.join(u'&');
}

QStringList LocationSearch::positiveTags() const
{
    QStringList out;
    for (const TagCondition &condition : conditions)
        if (condition.type == TagType::General)
            for (const TagTerm &term : condition.terms)
                for (const QString &tag : term.positive)
                    if (!out.contains(tag))
                        out << tag;
    return out;
}

namespace {

// Tag references under `node` at `path`, without null GUIDs.
QStringList refsAt(Node node, const char *path)
{
    QStringList out;
    for (const Node ref : findAll(node, path))
        if (const std::string_view guid = getOr(ref, "value"); !guid.empty() && guid != enh::kNullUuid)
            out << qs(guid);
    return out;
}

} // namespace

LocationSearch parseLocationSearch(Node value)
{
    LocationSearch search;
    for (const Node condition : findAll(value, "matchConditions/DataSetMatchCondition_TagSearch")) {
        const std::string_view type = getOr(condition, "tagType", "General");
        TagCondition c;
        if (type == "General")
            c.type = TagType::General;
        else if (type == "Produces")
            c.type = TagType::Produces;
        else if (type == "Consumes")
            c.type = TagType::Consumes;
        else
            continue;
        for (const Node term : findAll(condition, "tagSearch/TagSearchTerm")) {
            TagTerm t{refsAt(term, "positiveTags/Reference"), refsAt(term, "negativeTags/Reference")};
            if (!t.positive.isEmpty() || !t.negative.isEmpty())
                c.terms.push_back(std::move(t));
        }
        if (!c.terms.empty())
            search.conditions.push_back(std::move(c));
    }
    return search;
}

bool matchesGeneral(const LocationSearch &search, const QSet<QString> &tags)
{
    const auto has = [&tags](const QString &t) { return tags.contains(t); };
    return std::all_of(search.conditions.cbegin(), search.conditions.cend(), [&](const TagCondition &c) {
        return c.type != TagType::General ||
               std::any_of(c.terms.cbegin(), c.terms.cend(), [&](const TagTerm &term) {
                   return std::all_of(term.positive.cbegin(), term.positive.cend(), has) &&
                          std::none_of(term.negative.cbegin(), term.negative.cend(), has);
               });
    });
}

// ── places ─────────────────────────────────────────────────────────────

PlaceIndex PlaceIndex::build(const enh::RecordStore &store, const TagTable &tags, const LocText &loc)
{
    PlaceIndex index;
    const QString dir = QStringLiteral("missiondata/pu_locations");
    if (!store.dirExists(dir))
        return index;

    struct Row
    {
        Place place;
        std::array<QSet<QString>, kTagTypes> tags; // by TagType
        QString stem;                              // the record's file name, without .xml
        QStringList strings;                       // its name strings' keys
    };
    std::vector<Row> rows;
    for (const auto files = store.rglob(dir); const QString &file : files) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc || getOr(doc.root(), "__type") != "MissionLocationTemplate")
            continue;
        const Node data = find(doc.root(), "locationData");
        if (!data || getOr(data, "disabled") == "1")
            continue;
        Row row;
        row.place.id = qs(getOr(doc.root(), "__ref"));
        row.stem = enh::fileStem(file).toLower();
        const std::array<QStringList, kTagTypes> own = {refsAt(data, "generalTags/tags/Reference"),
                                                        refsAt(data, "producesTags/positiveTags/Reference"),
                                                        refsAt(data, "consumesTags/positiveTags/Reference")};
        for (std::size_t type = 0; type < own.size(); ++type)
            for (const QString &guid : own[type])
                for (const auto lineage = tags.lineage(guid); const QString &tag : lineage)
                    row.tags[type].insert(tag);
        QString first;
        for (const Node variant : findAll(data, "stringVariants/variants/MissionStringVariant")) {
            const QString key = qs(getOr(variant, "string"));
            row.strings << key.toLower();
            const QString text = loc(key);
            if (text.isEmpty())
                continue;
            const QString kind = tags.name(qs(getOr(variant, "tag")));
            if (kind == u"Name" && row.place.name.isEmpty())
                row.place.name = text;
            else if (kind == u"Address" && row.place.address.isEmpty())
                row.place.address = text;
            if (first.isEmpty())
                first = text;
        }
        if (row.place.name.isEmpty())
            row.place.name = !row.place.address.isEmpty() ? row.place.address : first;
        if (row.place.name.isEmpty())
            row.place.name = enh::humanizeKey(enh::lastDotPart(enh::tag(doc.root())));
        rows.push_back(std::move(row));
    }

    // Star systems are the children of a "System" tag; the tree has more than
    // one, so take the one the templates use most.
    QHash<QString, int> systemRoots;
    for (const Row &row : rows)
        for (const QString &tag : row.tags[0])
            if (const QString parent = tags.parent(tag); tags.name(parent) == u"System")
                ++systemRoots[parent];
    QString systemsRoot;
    for (auto it = systemRoots.cbegin(); it != systemRoots.cend(); ++it)
        if (systemsRoot.isEmpty() || it.value() > systemRoots.value(systemsRoot) ||
            (it.value() == systemRoots.value(systemsRoot) && it.key() < systemsRoot))
            systemsRoot = it.key();
    // That subtree's tags by lower-cased name ("stanton4a"), shallowest first.
    QHash<QString, QString> locationTags;
    for (QStringList level = tags.children(systemsRoot); !level.isEmpty();) {
        QStringList next;
        for (const QString &guid : std::as_const(level)) {
            locationTags.insert(tags.name(guid).toLower(), guid);
            next << tags.children(guid);
        }
        level = std::move(next);
    }
    const auto hasSystem = [&](const Row &row) {
        return std::any_of(row.tags[0].cbegin(), row.tags[0].cend(),
                           [&](const QString &t) { return tags.parent(t) == systemsRoot; });
    };
    const auto addLineage = [&](Row &row, const QString &name) {
        const QString guid = locationTags.value(name);
        for (const auto lineage = tags.lineage(guid); const QString &tag : lineage)
            row.tags[0].insert(tag);
        return !guid.isEmpty();
    };
    static const QRegularExpression byStem(
        QStringLiteral(R"((?:^|_)(stanton|pyro|nyx)_?(\d{1,2})?([a-z])?(?![a-z0-9]))"));
    static const QRegularExpression byKey(QStringLiteral(R"(_(stanton|pyro|nyx)_)"));
    for (Row &row : rows) {
        if (!systemsRoot.isEmpty() && !hasSystem(row)) {
            if (const QRegularExpressionMatch m = byStem.match(row.stem); m.hasMatch()) {
                const QString system = m.captured(1), planet = m.captured(2), moon = m.captured(3);
                const bool placed =
                    (!planet.isEmpty() && !moon.isEmpty() && addLineage(row, system + planet + moon)) ||
                    (!planet.isEmpty() && addLineage(row, system + planet));
                if (!placed)
                    addLineage(row, system);
            } else {
                for (const QString &key : std::as_const(row.strings))
                    if (const QRegularExpressionMatch k = byKey.match(key);
                        k.hasMatch() && addLineage(row, k.captured(1)))
                        break;
            }
        }
        QStringList systems;
        for (const QString &tag : std::as_const(row.tags[0]))
            if (!systemsRoot.isEmpty() && tags.parent(tag) == systemsRoot)
                systems << tags.name(tag);
        if (!systems.isEmpty())
            row.place.system = *std::min_element(systems.cbegin(), systems.cend());
    }

    // By name, ignoring leading quotes and the like ("Buckets" sorts under B).
    const auto sortName = [](const QString &name) {
        qsizetype i = 0;
        while (i < name.size() && !name[i].isLetterOrNumber())
            ++i;
        return QStringView(name).sliced(i);
    };
    std::sort(rows.begin(), rows.end(), [&sortName](const Row &a, const Row &b) {
        if (const int c = sortName(a.place.name).compare(sortName(b.place.name), Qt::CaseInsensitive); c != 0)
            return c < 0;
        if (const int c = a.place.name.compare(b.place.name, Qt::CaseInsensitive); c != 0)
            return c < 0;
        if (a.place.address != b.place.address)
            return a.place.address < b.place.address;
        return a.place.id < b.place.id;
    });
    index.places_.reserve(rows.size());
    for (Row &row : rows) {
        const int i = static_cast<int>(index.places_.size());
        for (std::size_t type = 0; type < row.tags.size(); ++type) {
            for (const QString &tag : std::as_const(row.tags[type]))
                index.byTag_[type][tag].push_back(i);
            index.tags_[type].push_back(std::move(row.tags[type]));
        }
        index.places_.push_back(std::move(row.place));
    }
    return index;
}

std::vector<int> PlaceIndex::match(const LocationSearch &search) const
{
    const int n = static_cast<int>(places_.size());
    if (search.isEmpty() || n == 0)
        return {};
    std::vector<char> keep(std::size_t(n), 1);
    for (const TagCondition &condition : search.conditions) {
        const auto &byTag = byTag_[std::size_t(condition.type)];
        const auto &placeTags = tags_[std::size_t(condition.type)];
        std::vector<char> any(std::size_t(n), 0);
        for (const TagTerm &term : condition.terms) {
            // Walk the rarest positive tag's places; every place without one.
            const std::vector<int> *candidates = nullptr;
            bool impossible = false;
            for (const QString &tag : term.positive) {
                const auto it = byTag.constFind(tag);
                if (it == byTag.cend()) {
                    impossible = true;
                    break;
                }
                if (!candidates || it->size() < candidates->size())
                    candidates = &*it;
            }
            if (impossible)
                continue;
            const auto consider = [&](int i) {
                const QSet<QString> &have = placeTags[std::size_t(i)];
                const bool ok = std::all_of(term.positive.cbegin(), term.positive.cend(),
                                            [&have](const QString &t) { return have.contains(t); }) &&
                                std::none_of(term.negative.cbegin(), term.negative.cend(),
                                             [&have](const QString &t) { return have.contains(t); });
                if (ok)
                    any[std::size_t(i)] = 1;
            };
            if (candidates)
                std::for_each(candidates->cbegin(), candidates->cend(), consider);
            else
                for (int i = 0; i < n; ++i)
                    consider(i);
        }
        for (std::size_t i = 0; i < keep.size(); ++i)
            keep[i] = char(keep[i] && any[i]);
    }
    std::vector<int> out;
    for (int i = 0; i < n; ++i) {
        if (!keep[std::size_t(i)])
            continue;
        const Place &p = places_[std::size_t(i)];
        if (!out.empty() && places_[std::size_t(out.back())].name == p.name &&
            places_[std::size_t(out.back())].address == p.address)
            continue;
        out.push_back(i);
    }
    return out;
}

QStringList PlaceIndex::systems() const
{
    QSet<QString> set;
    for (const Place &p : places_)
        if (!p.system.isEmpty())
            set.insert(p.system);
    QStringList out(set.cbegin(), set.cend());
    out.sort();
    return out;
}

} // namespace core::missions
