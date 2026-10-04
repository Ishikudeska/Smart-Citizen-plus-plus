#include "core/blueprints/OwnedItems.h"

#include "core/text/PyText.h"

#include <QRegularExpression>

#include <algorithm>

namespace core::blueprints {

namespace {

constexpr auto kUcp = QRegularExpression::UseUnicodePropertiesOption;

constexpr QLatin1StringView kNl("\\n"); // the literal two-character separator
constexpr QLatin1StringView kOwnedTag(" <EM4>[Owned]</EM4>");

QString nfkc(const QString &s)
{
    return s.normalized(QString::NormalizationForm_KC);
}

// A pattern written with Python's \s. Substitute before inserting escaped
// text, which may itself hold a backslash followed by an "s".
QString pyPattern(const QString &pattern)
{
    QString p = pattern;
    p.replace(QStringLiteral("\\s"), py::kReSpace);
    return p;
}

QRegularExpression pyRe(const QString &pattern, QRegularExpression::PatternOptions extra = {})
{
    return QRegularExpression(pyPattern(pattern), kUcp | extra);
}

// "\n- <name>": a bullet runs to the next backslash.
const QRegularExpression &bulletRe()
{
    static const QRegularExpression re(QStringLiteral(R"(\\n- ([^\\]+))"), kUcp);
    return re;
}

const QRegularExpression &ownedStripRe()
{
    static const QRegularExpression re = pyRe(QStringLiteral(R"(\s*<EM4>\[Owned\]</EM4>)"));
    return re;
}

const QRegularExpression &wsRe()
{
    static const QRegularExpression re = pyRe(QStringLiteral(R"(\s+)"));
    return re;
}

// CIG's bullets annotate some items with a category the item's own name
// lacks ("Bendix (Fuel Nozzle)"). An allow-list, since some names end in a
// real parenthetical ("Artimex Arms (Modified)").
const QRegularExpression &trailingCategoryRe()
{
    static const QRegularExpression re = pyRe(QStringLiteral(
        R"(\s*\((Cooler|Fuel Nozzle|Mining Laser|Powerplant|Quantum Drive|Radar|Salvage Mod|Shield)\)\s*$)"));
    return re;
}

// Any <EM3>/<EM4> label: a section header unless it is a region label
// ("[Nyx]"), a reputation tier ("Awarded from ... variants") or a pool.
const QRegularExpression &sectionHeaderRe()
{
    static const QRegularExpression re(QStringLiteral(R"(<EM([34])>([^<]*)</EM\1>)"), kUcp);
    return re;
}

const QRegularExpression &awardedFromRe()
{
    static const QRegularExpression re(QStringLiteral(R"(^awarded from .+ variants$)"),
                                       kUcp | QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &poolLabelRe()
{
    static const QRegularExpression re =
        pyRe(QStringLiteral(R"(^pool \d+$)"), QRegularExpression::CaseInsensitiveOption);
    return re;
}

QString enclosingsCacheKey(const Enclosings &enclosings)
{
    QString key;
    for (const auto &[open, close] : enclosings)
        key += open + QChar(0x1F) + close + QChar(0x1E);
    return key;
}

struct TagPatterns
{
    QRegularExpression leading;
    QRegularExpression trailing;
    bool valid = false;
};

// Leading and trailing tag patterns for the delimited enclosings; none for
// a list without one (empty, or only the None style).
const TagPatterns &tagPatterns(const Enclosings &enclosings)
{
    thread_local QHash<QString, TagPatterns> cache;
    const QString key = enclosingsCacheKey(enclosings);
    if (const auto it = cache.constFind(key); it != cache.cend())
        return *it;

    QStringList alts;
    for (const auto &[open, close] : enclosings)
        if (!open.isEmpty() && !close.isEmpty())
            alts << QRegularExpression::escape(open) + QStringLiteral("[^") +
                        QRegularExpression::escape(close) + QStringLiteral("]*") +
                        QRegularExpression::escape(close);
    TagPatterns patterns;
    if (!alts.isEmpty()) {
        const QString alt = alts.join(u'|');
        patterns.leading = QRegularExpression(pyPattern(QStringLiteral(R"(^(?:%1)\s*)")).arg(alt), kUcp);
        patterns.trailing = QRegularExpression(pyPattern(QStringLiteral(R"(\s*(?:%1)$)")).arg(alt), kUcp);
        patterns.valid = true;
    }
    return *cache.insert(key, patterns);
}

const QRegularExpression &bpHeaderRe(const QString &customHeader)
{
    thread_local QHash<QString, QRegularExpression> cache;
    if (const auto it = cache.constFind(customHeader); it != cache.cend())
        return *it;

    QStringList parts = {kBpSectionHeader, kAltBpSectionHeader};
    const QString custom = py::strip(customHeader);
    if (!custom.isEmpty()) {
        const QString upper = custom.toUpper();
        if (std::none_of(parts.cbegin(), parts.cend(),
                         [&](const QString &p) { return p.toUpper() == upper; }))
            parts << custom;
    }
    QStringList escaped;
    for (const QString &p : std::as_const(parts))
        escaped << QRegularExpression::escape(p);
    // A trailing "(Repeat Only)"-style qualifier is part of the header.
    const QRegularExpression re(pyPattern(QStringLiteral(R"(<EM([34])>\s*(?:%1)(?:\s*\([^)]*\))?\s*</EM\1>)"))
                                    .arg(escaped.join(u'|')),
                                kUcp | QRegularExpression::CaseInsensitiveOption);
    return *cache.insert(customHeader, re);
}

// [start, end) of the blueprint section's bullets: from its header to the
// next real section header.
std::optional<std::pair<qsizetype, qsizetype>> bpSectionSpan(const QString &value, const QString &bpHeader)
{
    const QRegularExpressionMatch m = bpHeaderRe(bpHeader).match(value);
    if (!m.hasMatch())
        return std::nullopt;
    const qsizetype start = m.capturedEnd();
    qsizetype end = value.size();
    for (const QRegularExpressionMatch &hm : sectionHeaderRe().globalMatch(value, start)) {
        const QString text = py::strip(hm.capturedView(2));
        if (text.startsWith(u'[') || awardedFromRe().match(text).hasMatch() ||
            poolLabelRe().match(text).hasMatch())
            continue;
        end = hm.capturedStart();
        break;
    }
    return std::pair{start, end};
}

bool hasNoneStyle(const Enclosings &enclosings)
{
    return enclosings.contains(kNoneStyleEnclosing);
}

bool looksLikeNoneStyleTagWord(const QString &word)
{
    static const QString separators = QStringLiteral("-_./|");
    static const QString brackets = QStringLiteral("[](){}<>");
    if (word.isEmpty() ||
        std::none_of(word.cbegin(), word.cend(), [](QChar c) { return separators.contains(c); }))
        return false;
    if (brackets.contains(word.front()) || brackets.contains(word.back()))
        return false;
    static const QRegularExpression nonAlnum(QStringLiteral("[^A-Za-z0-9]+"));
    static const QRegularExpression size(QStringLiteral(R"(^S\d{1,2}$)"),
                                         kUcp | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression grade(QStringLiteral("^[A-F]$"),
                                          QRegularExpression::CaseInsensitiveOption);
    for (const auto tokens = word.split(nonAlnum, Qt::SkipEmptyParts); const QString &tok : tokens)
        if (size.match(tok).hasMatch() || grade.match(tok).hasMatch())
            return true;
    return false;
}

QString aliased(const QString &name)
{
    return bulletNameAliases().value(name, name);
}

QString collapseWhitespace(const QString &s)
{
    QString out = s;
    out.replace(wsRe(), QStringLiteral(" "));
    return py::strip(out);
}

} // namespace

const Enclosings &defaultEnclosings()
{
    static const Enclosings square = {{QStringLiteral("["), QStringLiteral("]")}};
    return square;
}

const QHash<QString, QString> &bulletNameAliases()
{
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("Arbor"), QStringLiteral("S0 Arbor")},
        {QStringLiteral("Helix"), QStringLiteral("S0 Helix")},
        {QStringLiteral("Hofstede"), QStringLiteral("S00 Hofstede")},
        {QStringLiteral("Klein"), QStringLiteral("Lawson Mining Laser")},
        // Fuel nozzles the key-slug fallback does not reach (#266).
        {QStringLiteral("Nozzle Fuelgiver Grin Nozzlefast"), QStringLiteral("Norfield")},
        {QStringLiteral("Nozzle Fuelgiver Grin Nozzleverysecure"), QStringLiteral("Harkin")},
        {QStringLiteral("Nozzle Fuelgiver Misc Nozzlestandard"), QStringLiteral("RN-7s")},
    };
    return aliases;
}

std::optional<QString> stripViaStockDiff(const QString &tagged, const QString &stock)
{
    const QString stockN = py::strip(nfkc(stock));
    if (stockN.isEmpty())
        return std::nullopt;
    const QString taggedN = nfkc(tagged);
    if (py::strip(taggedN) == stockN)
        return QString();
    if (taggedN.endsWith(stockN))
        return py::strip(QStringView(taggedN).chopped(stockN.size()));
    if (taggedN.startsWith(stockN))
        return py::strip(QStringView(taggedN).sliced(stockN.size()));
    return std::nullopt;
}

std::optional<std::pair<QString, QString>> findNoneStyleTagWord(const QString &s)
{
    if (const qsizetype i = s.indexOf(u' '); i >= 0) {
        const QString first = s.first(i);
        if (looksLikeNoneStyleTagWord(first))
            return std::pair{first, s.sliced(i + 1)};
    }
    if (const qsizetype i = s.lastIndexOf(u' '); i >= 0) {
        const QString last = s.sliced(i + 1);
        if (looksLikeNoneStyleTagWord(last))
            return std::pair{last, s.first(i)};
    }
    return std::nullopt;
}

QString stripNoneStyleTagHeuristic(const QString &s)
{
    const auto found = findNoneStyleTagWord(s);
    return found ? found->second : s;
}

Enclosings enclosingsFromTagConfigs(const QMap<QString, tags::TagConfig> &configs,
                                    const QStringList &categories)
{
    Enclosings pairs = defaultEnclosings();
    for (const QString &category : categories) {
        const auto it = configs.constFind(category);
        if (it == configs.cend())
            continue;
        EnclosingPair pair = defaultEnclosings().front();
        for (const tags::Enclosing &e : tags::enclosings())
            if (it->enclosing == QLatin1StringView(e.key))
                pair = {QString::fromUtf8(e.open), QString::fromUtf8(e.close)};
        if (!pairs.contains(pair))
            pairs << pair;
    }
    std::sort(pairs.begin(), pairs.end(), [](const EnclosingPair &a, const EnclosingPair &b) {
        return a.first != b.first ? py::less(a.first, b.first) : py::less(a.second, b.second);
    });
    return pairs;
}

bool hasBpSection(const QString &value, const QString &bpHeader)
{
    return bpHeaderRe(bpHeader).match(value).hasMatch();
}

QString normalizeItemName(const QString &name, const Enclosings &enclosings, const QString &stock)
{
    if (name.isEmpty())
        return QString();
    QString s = nfkc(name);
    s.replace(ownedStripRe(), QString());
    if (!stock.isEmpty() && stripViaStockDiff(s, stock))
        return aliased(collapseWhitespace(py::strip(nfkc(stock))));

    if (const TagPatterns &patterns = tagPatterns(enclosings); patterns.valid) {
        s.replace(patterns.leading, QString());
        s.replace(patterns.trailing, QString());
    }
    // Only for users who configured the None style: real names can look
    // like a None-style tag word ("F-4 Blaster").
    if (hasNoneStyle(enclosings))
        s = stripNoneStyleTagHeuristic(s);
    s.replace(trailingCategoryRe(), QString());
    return aliased(collapseWhitespace(s));
}

std::optional<QString> resolveAgainstCatalogue(const QString &name, const QSet<QString> &catalogue)
{
    // Characters that may separate a foreign tag from the real name.
    static const QString boundary = QStringLiteral(" ]/-_)}>.:");
    const QString n = normalizeItemName(name);
    if (n.isEmpty())
        return std::nullopt;
    std::optional<QString> best;
    qsizetype bestLen = -1;
    for (const QString &known : catalogue) {
        if (known.isEmpty() || known.size() > n.size() || known.size() <= bestLen)
            continue;
        if (!n.endsWith(known))
            continue;
        if (n.size() != known.size() && !boundary.contains(n[n.size() - known.size() - 1]))
            continue;
        best = known;
        bestLen = known.size();
    }
    return best;
}

OwnedRepair repairForeignOwnedNames(const QSet<QString> &owned, const QSet<QString> &catalogue)
{
    OwnedRepair result{owned, {}};
    if (catalogue.isEmpty())
        return result;
    QStringList unmatched;
    for (const QString &name : owned)
        if (!catalogue.contains(name))
            unmatched << name;
    std::sort(unmatched.begin(), unmatched.end(),
              [](const QString &a, const QString &b) { return py::less(a, b); });
    for (const QString &name : std::as_const(unmatched)) {
        const std::optional<QString> real = resolveAgainstCatalogue(name, catalogue);
        if (!real)
            continue;
        result.repaired.remove(name);
        if (result.repaired.contains(*real)) {
            result.renamed.insert(name, std::nullopt);
        } else {
            result.repaired.insert(*real);
            result.renamed.insert(name, *real);
        }
    }
    return result;
}

QSet<QString> extractBpItemNames(const QString &value, const Enclosings &enclosings, const QString &bpHeader)
{
    QSet<QString> names;
    if (value.isEmpty())
        return names;
    const auto span = bpSectionSpan(value, bpHeader);
    if (!span)
        return names;
    const QStringView section = QStringView(value).first(span->second);
    for (const QRegularExpressionMatch &m : bulletRe().globalMatchView(section, span->first)) {
        const QString name = normalizeItemName(m.captured(1), enclosings);
        if (!name.isEmpty())
            names.insert(name);
    }
    return names;
}

QString applyOwnedToValue(const QString &value, const QSet<QString> &owned, const Enclosings &enclosings,
                          const QString &bpHeader)
{
    if (value.isEmpty())
        return value;
    QString stripped = value;
    stripped.replace(ownedStripRe(), QString());
    if (owned.isEmpty())
        return stripped;
    const auto span = bpSectionSpan(stripped, bpHeader);
    if (!span)
        return stripped;
    const auto [start, end] = *span;

    QString out = stripped.first(start);
    qsizetype pos = start;
    const QStringView section = QStringView(stripped).first(end);
    for (const QRegularExpressionMatch &m : bulletRe().globalMatchView(section, start)) {
        const QString raw = m.captured(1);
        if (!owned.contains(normalizeItemName(raw, enclosings)))
            continue;
        out += QStringView(stripped).sliced(pos, m.capturedStart() - pos);
        out += kNl + QStringLiteral("- ") + raw + kOwnedTag;
        pos = m.capturedEnd();
    }
    out += QStringView(stripped).sliced(pos);
    return out;
}

} // namespace core::blueprints
