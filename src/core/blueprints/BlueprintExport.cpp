#include "core/blueprints/BlueprintExport.h"

#include "core/text/PyJson.h"
#include "core/text/PyText.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringDecoder>

#include <algorithm>
#include <utility>

namespace core::blueprints {

namespace {

constexpr int kFormatVersion = 1;

// sorted(owned, key=str.lower)
QStringList sortedCaseInsensitive(const QSet<QString> &names)
{
    QStringList sorted(names.cbegin(), names.cend());
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
        const QString la = a.toLower();
        const QString lb = b.toLower();
        return la != lb ? py::less(la, lb) : py::less(a, b);
    });
    return sorted;
}

QString displayName(const QString &name, const QMap<QString, BlueprintItem> &meta)
{
    const auto it = meta.constFind(name);
    return it != meta.cend() && !it->taggedName.isEmpty() ? it->taggedName : name;
}

// csv.writer's QUOTE_MINIMAL field.
QString csvField(const QString &s)
{
    if (!s.contains(u',') && !s.contains(u'"') && !s.contains(u'\n') && !s.contains(u'\r'))
        return s;
    QString quoted = s;
    quoted.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    return u'"' + quoted + u'"';
}

// csv.reader with the excel dialect: records end at CR, LF or CRLF outside
// quotes; a blank line is an empty record.
QList<QStringList> parseCsv(QStringView text)
{
    QList<QStringList> records;
    QStringList fields;
    QString field;
    enum { StartRecord, StartField, InField, InQuoted, QuoteInQuoted } state = StartRecord;

    const auto endRecord = [&] {
        records.append(fields);
        fields.clear();
        state = StartRecord;
    };
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        const bool newline = c == u'\r' || c == u'\n';
        if (newline && c == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n' && state != InQuoted)
            ++i; // CRLF is one terminator
        switch (state) {
        case StartRecord:
            if (newline) {
                endRecord();
                break;
            }
            [[fallthrough]];
        case StartField:
            if (newline) {
                fields << QString();
                endRecord();
            } else if (c == u'"') {
                state = InQuoted;
            } else if (c == u',') {
                fields << QString();
                state = StartField;
            } else {
                field += c;
                state = InField;
            }
            break;
        case InField:
            if (newline) {
                fields << std::exchange(field, {});
                endRecord();
            } else if (c == u',') {
                fields << std::exchange(field, {});
                state = StartField;
            } else {
                field += c;
            }
            break;
        case InQuoted:
            if (c == u'"')
                state = QuoteInQuoted;
            else
                field += c;
            break;
        case QuoteInQuoted:
            if (c == u'"') {
                field += c;
                state = InQuoted;
            } else if (c == u',') {
                fields << std::exchange(field, {});
                state = StartField;
            } else if (newline) {
                fields << std::exchange(field, {});
                endRecord();
            } else {
                field += c; // not strict: text after the closing quote is kept
                state = InField;
            }
            break;
        }
    }
    if (state == StartField || state == InField || state == InQuoted || state == QuoteInQuoted) {
        if (state != StartField || !fields.isEmpty())
            fields << field;
        records.append(fields);
    }
    return records;
}

// str(value) for a JSON scalar, the way the Python reads a "name".
QString pyStr(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::String:
        return v.toString();
    case QJsonValue::Bool:
        return v.toBool() ? QStringLiteral("True") : QString();
    case QJsonValue::Double: {
        const QVariant var = v.toVariant();
        if (var.typeId() == QMetaType::LongLong || var.typeId() == QMetaType::Int)
            return var.toLongLong() == 0 ? QString() : QString::number(var.toLongLong());
        const double d = v.toDouble();
        return d == 0 ? QString() : QString::number(d, 'g', QLocale::FloatingPointShortest);
    }
    case QJsonValue::Array:
    case QJsonValue::Object:
        // Never seen in an export; the name would not normalize to an item.
        return QString();
    default:
        return QString();
    }
}

std::expected<QSet<QString>, QString> parseJsonNames(const QString &path, const Enclosings &enclosings)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(QStringLiteral("Could not read JSON file: %1").arg(file.errorString()));
    const QByteArray bytes = file.readAll();
    // json.load rejects a BOM; Qt would skip it.
    if (bytes.startsWith("\xEF\xBB\xBF"))
        return std::unexpected(QStringLiteral("Could not read JSON file: Unexpected UTF-8 BOM"));
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError)
        return std::unexpected(QStringLiteral("Could not read JSON file: %1").arg(error.errorString()));
    if (!doc.isObject() || !doc.object().value(QStringLiteral("blueprints")).isArray())
        return std::unexpected(QStringLiteral("JSON file has no \"blueprints\" array"));

    QSet<QString> names;
    for (const QJsonValue &entry : doc.object().value(QStringLiteral("blueprints")).toArray()) {
        if (!entry.isObject())
            continue;
        const QString name = normalizeItemName(pyStr(entry.toObject().value(QStringLiteral("name"))), enclosings);
        if (!name.isEmpty())
            names.insert(name);
    }
    return names;
}

std::expected<QSet<QString>, QString> parseCsvNames(const QString &path, const Enclosings &enclosings)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(QStringLiteral("Could not read CSV file: %1").arg(file.errorString()));
    // Skips one leading BOM, like utf-8-sig; stateless so a truncated final
    // sequence counts as an error.
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = decoder.decode(file.readAll());
    if (decoder.hasError())
        return std::unexpected(QStringLiteral("Could not read CSV file: not valid UTF-8"));

    const QList<QStringList> records = parseCsv(text);
    if (records.isEmpty() || !records.front().contains(QStringLiteral("name")))
        return std::unexpected(QStringLiteral("CSV file has no \"name\" column"));
    const QStringList &header = records.front();

    QSet<QString> names;
    for (qsizetype r = 1; r < records.size(); ++r) {
        const QStringList &row = records[r];
        if (row.isEmpty())
            continue; // DictReader skips blank lines
        // dict(zip(fieldnames, row)): the last "name" column wins, and one
        // past the end of a short row is None.
        QString value;
        for (qsizetype i = 0; i < header.size(); ++i)
            if (header[i] == u"name")
                value = i < row.size() ? row[i] : QString();
        const QString name = normalizeItemName(value, enclosings);
        if (!name.isEmpty())
            names.insert(name);
    }
    return names;
}

} // namespace

QString exportOwnedBlueprintsJson(const QSet<QString> &owned, const QMap<QString, BlueprintItem> &meta,
                                  const QDateTime &now)
{
    QString out = QStringLiteral("{\n  \"version\": %1,\n  \"exportedAt\": %2,\n  \"missions\": [],\n")
                      .arg(kFormatVersion)
                      .arg(py::jsonString(now.toUTC().toString(Qt::ISODateWithMs), false));
    const QStringList names = sortedCaseInsensitive(owned);
    if (names.isEmpty()) {
        out += QStringLiteral("  \"blueprints\": []\n}");
        return out;
    }
    out += QStringLiteral("  \"blueprints\": [\n");
    for (qsizetype i = 0; i < names.size(); ++i) {
        out += QStringLiteral("    {\n      \"name\": %1,\n      \"completed\": true,\n      \"favorite\": false\n    }")
                   .arg(py::jsonString(displayName(names[i], meta), false));
        out += i + 1 < names.size() ? QStringLiteral(",\n") : QStringLiteral("\n");
    }
    out += QStringLiteral("  ]\n}");
    return out;
}

QString exportOwnedBlueprintsCsv(const QSet<QString> &owned, const QMap<QString, BlueprintItem> &meta)
{
    QString out = QStringLiteral("name,type\n");
    for (const QString &name : sortedCaseInsensitive(owned)) {
        const auto it = meta.constFind(name);
        out += csvField(displayName(name, meta)) + u',' + csvField(it != meta.cend() ? it->type : QString()) + u'\n';
    }
    return out;
}

std::expected<QSet<QString>, QString> parseImportNames(const QString &path, const Enclosings &enclosings)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == u"json")
        return parseJsonNames(path, enclosings);
    if (suffix == u"csv")
        return parseCsvNames(path, enclosings);
    return std::unexpected(
        QStringLiteral("Unsupported file type: %1").arg(suffix.isEmpty() ? QStringLiteral("(none)") : u'.' + suffix));
}

ImportMatch matchImportNames(const QSet<QString> &imported, const QSet<QString> &known, const QSet<QString> &catalogue,
                             const Enclosings &enclosings)
{
    // Normalize the known side too, so the comparison stays symmetric.
    QStringList knownSorted(known.cbegin(), known.cend());
    std::sort(knownSorted.begin(), knownSorted.end(), [](const QString &a, const QString &b) { return py::less(a, b); });
    QHash<QString, QString> knownByNormalized;
    for (const QString &name : knownSorted) {
        const QString n = normalizeItemName(name, enclosings);
        if (!knownByNormalized.contains(n))
            knownByNormalized.insert(n, name);
    }

    ImportMatch result;
    for (const QString &name : imported) {
        auto hit = knownByNormalized.constFind(name);
        if (hit == knownByNormalized.cend() && !catalogue.isEmpty()) {
            if (const std::optional<QString> real = resolveAgainstCatalogue(name, catalogue))
                hit = knownByNormalized.constFind(*real);
        }
        if (hit != knownByNormalized.cend())
            result.matched.insert(*hit);
        else
            result.unmatched.insert(name);
    }
    return result;
}

} // namespace core::blueprints
