#include "core/text/IniText.h"

#include <QFile>
#include <QStringDecoder>

namespace core {

namespace {

// Windows-1252 0x80..0x9F. 0x81, 0x8D, 0x8F, 0x90 and 0x9D are undefined.
constexpr char16_t kCp1252High[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
};

QByteArrayView withoutBom(const QByteArray &bytes)
{
    if (bytes.startsWith("\xEF\xBB\xBF"))
        return QByteArrayView(bytes).sliced(3);
    return QByteArrayView(bytes);
}

} // namespace

QString decodeWindows1252(QByteArrayView bytes)
{
    QString out;
    out.resize(bytes.size());
    QChar *dst = out.data();
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        const auto b = static_cast<unsigned char>(bytes[i]);
        dst[i] = (b >= 0x80 && b <= 0x9F) ? QChar(kCp1252High[b - 0x80]) : QChar(b);
    }
    return out;
}

IniText decodeIniText(const QByteArray &bytes)
{
    const QByteArrayView body = withoutBom(bytes);
    {
        QStringDecoder strict(QStringConverter::Utf8,
                              QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
        QString text = strict.decode(body);
        if (!strict.hasError())
            return {std::move(text), IniEncoding::Utf8, 0};
    }

    QStringDecoder lenient(QStringConverter::Utf8,
                           QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
    QString repaired = lenient.decode(body);
    const int bad = static_cast<int>(repaired.count(QChar(0xFFFD)));
    qsizetype high = 0;
    for (const char c : body)
        high += static_cast<unsigned char>(c) >= 0x80 ? 1 : 0;

    if (bad * 2 <= high)
        return {std::move(repaired), IniEncoding::Utf8Repaired, bad};
    return {decodeWindows1252(body), IniEncoding::Windows1252, 0};
}

std::optional<IniText> readIniText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return decodeIniText(file.readAll());
}

} // namespace core
