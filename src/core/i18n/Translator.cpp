#include "core/i18n/Translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocale>
#include <QLoggingCategory>

#include <optional>

Q_LOGGING_CATEGORY(lcI18n, "scx.i18n")

namespace core::i18n {

namespace {

constexpr QLatin1StringView kHuman("ht");
constexpr QLatin1StringView kAi("at");

QJsonObject loadTree(const QString &path)
{
    QFile file(path);
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcI18n) << "could not read" << path << file.errorString();
        return {};
    }
    const QByteArray bytes = file.readAll();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    // json.load rejects a BOM; so does this, rather than half-loading.
    if (bytes.startsWith("\xEF\xBB\xBF") || error.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcI18n) << "could not load" << path << error.errorString();
        return {};
    }
    return doc.object();
}

bool isLeaf(const QJsonValue &v)
{
    return v.isObject() && (v.toObject().contains(kHuman) || v.toObject().contains(kAi));
}

// "ht" if set, else "at"; a plain string is itself; anything else is blank.
QString leafValue(const QJsonValue &v)
{
    if (v.isString())
        return v.toString();
    if (!isLeaf(v))
        return QString();
    const QJsonObject leaf = v.toObject();
    const QString human = leaf.value(kHuman).toString();
    return !human.isEmpty() ? human : leaf.value(kAi).toString();
}

void flatten(const QJsonObject &node, const QString &prefix, QHash<QString, QString> &out)
{
    for (auto it = node.begin(); it != node.end(); ++it) {
        // A dotted name cannot be addressed by a dot path.
        if (it.key().contains(u'.'))
            continue;
        const QString path = prefix + it.key();
        const QJsonValue v = it.value();
        if (v.isObject() && !isLeaf(v)) {
            flatten(v.toObject(), path + u'.', out);
        } else {
            const QString text = leafValue(v);
            if (!text.isEmpty())
                out.insert(path, text);
        }
    }
}

int leafCount(const QJsonValue &node)
{
    if (!node.isObject())
        return 1;
    int n = 0;
    const QJsonObject o = node.toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
        if (it.key() != u"_comment")
            n += leafCount(it.value());
    return n;
}

// Python's str() of a format argument.
QString pyStr(const QVariant &v)
{
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    return v.toString();
}

bool isIntegral(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
        return true;
    default:
        return false;
    }
}

// format(value, ",") for an integer: "1,234,567".
QString groupThousands(const QVariant &v)
{
    const bool negative = v.toLongLong() < 0;
    QString digits = v.typeId() == QMetaType::ULongLong ? QString::number(v.toULongLong())
                                                        : QString::number(qAbs(v.toLongLong()));
    for (qsizetype i = digits.size() - 3; i > 0; i -= 3)
        digits.insert(i, u',');
    return negative ? u'-' + digits : digits;
}

std::optional<QString> formatField(const QString &field, const QVariantHash &args)
{
    const qsizetype colon = field.indexOf(u':');
    const QString name = colon >= 0 ? field.first(colon) : field;
    const QString spec = colon >= 0 ? field.sliced(colon + 1) : QString();
    if (name.isEmpty() || name.contains(u'!') || name.contains(u'.') || name.contains(u'['))
        return std::nullopt;
    const auto it = args.constFind(name);
    if (it == args.cend())
        return std::nullopt; // KeyError
    if (spec.isEmpty())
        return pyStr(*it);
    if (spec == u"," && isIntegral(*it))
        return groupThousands(*it);
    return std::nullopt; // a spec the UI text never uses
}

} // namespace

Catalog Catalog::load(const QString &languagesDir, const QString &language)
{
    const QDir dir(languagesDir);
    const QString file = QStringLiteral("/ui.json");
    const QJsonObject english = loadTree(dir.filePath(kDefaultLanguage) + file);
    const QJsonObject overlay = language != kDefaultLanguage ? loadTree(dir.filePath(language) + file) : QJsonObject{};
    return fromTrees(language, english, overlay);
}

Catalog Catalog::fromTrees(const QString &language, const QJsonObject &english, const QJsonObject &overlay)
{
    QJsonObject merged = english;
    deepMerge(merged, overlay);
    Catalog c;
    c.language_ = language;
    flatten(merged, QString(), c.strings_);
    return c;
}

void deepMerge(QJsonObject &base, const QJsonObject &overlay)
{
    for (auto it = overlay.begin(); it != overlay.end(); ++it) {
        const QString &k = it.key();
        const QJsonValue v = it.value();
        if (isLeaf(v)) {
            // A blank leaf keeps the English one as the floor.
            if (!leafValue(v).isEmpty() || !base.contains(k))
                base.insert(k, v);
        } else if (v.isObject() && base.value(k).isObject() && !isLeaf(base.value(k))) {
            QJsonObject section = base.value(k).toObject();
            deepMerge(section, v.toObject());
            base.insert(k, section);
        } else {
            if (base.value(k).isObject() && !v.isObject())
                qCWarning(lcI18n) << "key" << k << "is a scalar in the overlay but a section in the base; replacing";
            base.insert(k, v);
        }
    }
}

JsonTranslator::JsonTranslator(QObject *parent)
    : QTranslator(parent)
    , catalog_(std::make_shared<const Catalog>())
{
}

void JsonTranslator::setCatalog(std::shared_ptr<const Catalog> catalog)
{
    catalog_.store(catalog ? std::move(catalog) : std::make_shared<const Catalog>());
}

std::shared_ptr<const Catalog> JsonTranslator::catalog() const
{
    return catalog_.load();
}

QString JsonTranslator::translate(const char *, const char *sourceText, const char *, int) const
{
    if (!sourceText)
        return QString();
    return catalog_.load()->lookup(QString::fromUtf8(sourceText));
}

bool JsonTranslator::isEmpty() const
{
    return catalog_.load()->isEmpty();
}

QString format(const QString &text, const QVariantHash &args)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == u'{') {
            if (i + 1 < text.size() && text[i + 1] == u'{') {
                out += u'{';
                ++i;
                continue;
            }
            // The field runs to the matching brace (a spec may nest one).
            qsizetype depth = 1;
            qsizetype j = i + 1;
            for (; j < text.size() && depth > 0; ++j)
                depth += text[j] == u'{' ? 1 : text[j] == u'}' ? -1 : 0;
            if (depth != 0)
                return text;
            const std::optional<QString> value = formatField(text.sliced(i + 1, j - i - 2), args);
            if (!value)
                return text;
            out += *value;
            i = j - 1;
        } else if (c == u'}') {
            if (i + 1 < text.size() && text[i + 1] == u'}') {
                out += u'}';
                ++i;
                continue;
            }
            return text; // "Single '}' encountered"
        } else {
            out += c;
        }
    }
    return out;
}

QString tr(const char *key, const QVariantHash &args)
{
    const QString text = QCoreApplication::translate(nullptr, key);
    // The Python only formats when arguments are given.
    return args.isEmpty() ? text : format(text, args);
}

QStringList availableLanguages(const QString &languagesDir)
{
    QStringList names;
    const QDir dir(languagesDir);
    for (const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
         const QString &name : entries) {
        if (name == kDefaultLanguage || leafCount(loadTree(dir.filePath(name + QStringLiteral("/ui.json")))) > 0)
            names << name;
    }
    if (!names.contains(kDefaultLanguage))
        names << kDefaultLanguage;
    names.sort();
    return names;
}

} // namespace core::i18n
