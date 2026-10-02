#include "core/enhancements/Common.h"

#include "core/text/PyFormat.h"
#include "core/text/PyText.h"

#include <QFileInfo>
#include <QSet>

#include <array>
#include <cmath>

namespace core::enh {

namespace {

constexpr std::array kSentinelKeys = {
    std::string_view("LOC_BADSTRING"), std::string_view("LOC_BADTOKEN"),      std::string_view("LOC_DEBUG"),
    std::string_view("LOC_EMPTY"),     std::string_view("LOC_INVALID"),       std::string_view("LOC_NOINNERTHOUGHT"),
    std::string_view("LOC_PLACEHOLDER"), std::string_view("LOC_UNINITIALIZED"),
};

std::string_view lstripAt(std::string_view s)
{
    while (!s.empty() && s.front() == '@')
        s.remove_prefix(1);
    return s;
}

QString keyFromRef(std::string_view ref)
{
    return qs(lstripAt(ref));
}

// The first Localization element whose `attr` holds a usable "@key".
QString localizationKey(Node root, const char *attr)
{
    QString key;
    forEachElement(root, [&](Node el) {
        if (tag(el) != "Localization")
            return true;
        const std::string_view v = getOr(el, attr);
        if (v.starts_with('@') && !isSentinelLocRef(v)) {
            key = keyFromRef(v);
            return false;
        }
        return true;
    });
    return key;
}

} // namespace

QString humanizeKey(QStringView key)
{
    return py::strip(key.toString().replace(u'_', u' '));
}

QString appendEnhancements(const QString &existing, const QString &block, const QString &separator, bool prepend)
{
    if (block.isEmpty())
        return existing;
    QString base = existing;
    static const std::array markers = {
        QStringLiteral("\\n\\n--- STATS ---"),
        QStringLiteral("\\n\\n<EM3>STATS</EM3>"),
        QStringLiteral("\\n\\n<EM3>MISSION DETAILS</EM3>"),
        QStringLiteral("\\n\\n<EM3>== Stats ==</EM3>"),
        QStringLiteral("\\n\\n<EM3>== Mission Details ==</EM3>"),
        QStringLiteral("\\n\\n== Stats =="),
        QStringLiteral("\\n\\n== Mission Details =="),
    };
    for (const QString &marker : markers) {
        if (const qsizetype at = base.indexOf(marker); at >= 0) {
            base.truncate(at);
            break;
        }
    }
    if (prepend)
        return block + kStatsPrependSeparator + base;
    return base + separator + block;
}

std::optional<double> toFloat(std::string_view value)
{
    return py::toFloat(qs(value));
}

std::optional<double> toFloat(const std::optional<std::string_view> &value)
{
    return value ? toFloat(*value) : std::nullopt;
}

QString fmt(std::optional<double> value, QStringView unit, int decimals)
{
    if (!value)
        return QStringLiteral("?");
    const double v = *value;
    if (decimals)
        return py::fixed(v, decimals, true) + unit;
    if (std::isnan(v))
        return py::repr(v); // int(round(nan)) raises; str(value) is what shows
    const double r = py::round(v);
    if (std::fabs(r) < 9.0e18)
        return py::integer(qint64(r), true) + unit;
    return py::fixed(r, 0, true) + unit;
}

QString fmt(const std::optional<std::string_view> &value, QStringView unit, int decimals)
{
    if (!value)
        return QStringLiteral("?");
    const std::optional<double> v = toFloat(*value);
    if (!v)
        return qs(*value); // not a number: shown as is, without the unit
    if (!decimals && std::isnan(*v))
        return qs(*value);
    return fmt(v, unit, decimals);
}

bool isSentinelLocRef(std::string_view ref)
{
    if (ref.empty())
        return true;
    const std::string_view key = lstripAt(ref);
    return std::find(kSentinelKeys.begin(), kSentinelKeys.end(), key) != kSentinelKeys.end();
}

bool isSentinelKey(QStringView key)
{
    const QByteArray utf8 = key.toUtf8();
    const std::string_view k(utf8.constData(), std::size_t(utf8.size()));
    return std::find(kSentinelKeys.begin(), kSentinelKeys.end(), k) != kSentinelKeys.end();
}

bool isPlaceholderText(QStringView s)
{
    static const QSet<QString> texts = {
        QStringLiteral("<= PLACEHOLDER =>"), QStringLiteral("<= UNINITIALIZED =>"), QStringLiteral("<= BADSTRING =>"),
        QStringLiteral("<= BADTOKEN =>"),    QStringLiteral("<= DEBUG =>"),         QStringLiteral("<= EMPTY =>"),
        QStringLiteral("<= INVALID =>"),     QStringLiteral("<= NOINNERTHOUGHT =>"),
    };
    return texts.contains(py::strip(s));
}

QString locKey(Node root)
{
    return localizationKey(root, "Description");
}

QString locNameKey(Node root)
{
    return localizationKey(root, "Name");
}

QString missionLocKey(Node root)
{
    const std::string_view desc = getOr(root, "description");
    if (desc.starts_with('@') && !isSentinelLocRef(desc))
        return keyFromRef(desc);
    return QString();
}

QString synthesizeDescription(Node root, const QString &xmlFile, const QString &key)
{
    QStringList parts;
    if (const QString name = locNameKey(root); !name.isEmpty()) {
        parts << name;
    } else if (const QString stem = humanizeKey(fileStem(xmlFile)); !stem.isEmpty()) {
        parts << stem;
    }

    if (const Node vpc = findDescendant(root, "VehicleComponentParams")) {
        QStringList attrs;
        if (const std::string_view career = getOr(vpc, "vehicleCareer"); career.starts_with('@'))
            attrs << QStringLiteral("Class: ") + keyFromRef(career);
        if (const std::string_view role = getOr(vpc, "vehicleRole"); role.starts_with('@'))
            attrs << QStringLiteral("Role: ") + keyFromRef(role);
        if (const std::string_view crew = getOr(vpc, "crewSize"); !crew.empty())
            attrs << QStringLiteral("Crew: ") + qs(crew);
        if (const Node bbox = findDescendant(root, "maxBoundingBoxSize"))
            if (const std::string_view length = getOr(bbox, "y"); !length.empty())
                attrs << QStringLiteral("Length: %1m").arg(qs(length));
        if (!attrs.isEmpty())
            parts << attrs.join(QStringLiteral(" | "));
    }
    if (const Node icp = findDescendant(root, "ItemComponentParams"))
        if (const std::string_view type = getOr(icp, "itemType"); !type.empty())
            parts << QStringLiteral("Item Type: ") + qs(type);
    if (const Node tp = findDescendant(root, "targetingParams"))
        if (const std::string_view sig = getOr(tp, "trackingSignalType"); !sig.empty())
            parts << QStringLiteral("Tracking: ") + qs(sig);

    // A real newline: discovered entries are the one place the Python joins
    // with "\n" rather than the literal "\\n".
    return parts.isEmpty() ? QString(key).replace(u'_', u' ') : parts.join(u'\n');
}

QString extractItemSize(QStringView cls)
{
    static const QRegularExpression re = py::re(QStringLiteral(R"(_S0*(\d+)_)"));
    const QRegularExpressionMatch m = re.matchView(cls);
    if (!m.hasMatch())
        return QString();
    return QStringLiteral("S") + QString::number(m.capturedView(1).toLongLong());
}

std::string_view polyType(Node el)
{
    const std::string_view poly = getOr(el, "__polymorphicType");
    return poly.empty() ? tag(el) : poly;
}

QString recordClassName(Node root, const QString &fallback)
{
    const std::string_view t = tag(root);
    const std::size_t dot = t.find('.');
    return dot == std::string_view::npos ? fallback : qs(t.substr(dot + 1));
}

QString lastDotPart(std::string_view t)
{
    const std::size_t dot = t.rfind('.');
    return qs(dot == std::string_view::npos ? t : t.substr(dot + 1));
}

QString fileStem(const QString &path)
{
    return QFileInfo(path).completeBaseName();
}

} // namespace core::enh
