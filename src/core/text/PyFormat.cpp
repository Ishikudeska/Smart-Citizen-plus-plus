#include "core/text/PyFormat.h"

#include "core/text/PyText.h"

#include <charconv>
#include <cmath>
#include <string>

namespace core::py {

namespace {

bool isDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

// digitpart: digit (["_"] digit)*. Appends the digits to `out` and
// advances `i`; false when there is no digit at `i`.
bool digitPart(QStringView s, qsizetype &i, std::string &out)
{
    if (i >= s.size() || !isDigit(s[i]))
        return false;
    while (i < s.size()) {
        if (isDigit(s[i])) {
            out += char(s[i].unicode());
            ++i;
        } else if (s[i] == u'_' && i + 1 < s.size() && isDigit(s[i + 1])) {
            ++i;
        } else {
            break;
        }
    }
    return true;
}

QString fromAscii(const char *begin, const char *end)
{
    return QString::fromLatin1(begin, end - begin);
}

QString nonFinite(double v, bool plus)
{
    if (std::isnan(v))
        return plus ? QStringLiteral("+nan") : QStringLiteral("nan");
    if (v < 0)
        return QStringLiteral("-inf");
    return plus ? QStringLiteral("+inf") : QStringLiteral("inf");
}

} // namespace

std::optional<double> toFloat(QStringView s)
{
    const QString t = strip(s);
    qsizetype i = 0;
    std::string text;
    if (i < t.size() && (t[i] == u'+' || t[i] == u'-'))
        text += char(t[i++].unicode());
    const QString rest = t.sliced(i).toLower();
    if (rest == u"inf" || rest == u"infinity")
        return text == "-" ? -HUGE_VAL : HUGE_VAL;
    if (rest == u"nan")
        return std::nan("");

    const bool intPart = digitPart(t, i, text);
    bool fracPart = false;
    if (i < t.size() && t[i] == u'.') {
        text += '.';
        ++i;
        fracPart = digitPart(t, i, text);
    }
    if (!intPart && !fracPart)
        return std::nullopt;
    if (i < t.size() && (t[i] == u'e' || t[i] == u'E')) {
        text += 'e';
        ++i;
        if (i < t.size() && (t[i] == u'+' || t[i] == u'-'))
            text += char(t[i++].unicode());
        if (!digitPart(t, i, text))
            return std::nullopt;
    }
    if (i != t.size())
        return std::nullopt;

    double v = 0;
    const char *begin = text.data() + (text.starts_with('+') ? 1 : 0);
    const auto [end, ec] = std::from_chars(begin, text.data() + text.size(), v);
    if (end != text.data() + text.size())
        return std::nullopt;
    if (ec == std::errc::result_out_of_range) // Python rounds to inf / 0
        return std::strtod(begin, nullptr);
    return v;
}

std::optional<qint64> toInt(QStringView s)
{
    const QString t = strip(s);
    qsizetype i = 0;
    std::string text;
    if (i < t.size() && (t[i] == u'+' || t[i] == u'-')) {
        if (t[i] == u'-')
            text += '-';
        ++i;
    }
    if (!digitPart(t, i, text) || i != t.size())
        return std::nullopt;
    qint64 v = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v);
    if (ec != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return v;
}

QString repr(double v)
{
    if (!std::isfinite(v))
        return nonFinite(v, false);
    // Shortest digits in scientific form: "-d.ddde+XX".
    char buf[64];
    const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::scientific);
    std::string sci(buf, end);
    const bool negative = sci.starts_with('-');
    if (negative)
        sci.erase(0, 1);
    const std::size_t e = sci.find('e');
    std::string digits = sci.substr(0, e);
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
    const int exponent = std::stoi(sci.substr(e + 1));
    const int decpt = exponent + 1; // value = 0.DIGITS x 10^decpt

    std::string out = negative ? "-" : "";
    if (decpt > -4 && decpt <= 16) {
        if (decpt <= 0) {
            out += "0." + std::string(std::size_t(-decpt), '0') + digits;
        } else if (std::size_t(decpt) >= digits.size()) {
            out += digits + std::string(std::size_t(decpt) - digits.size(), '0') + ".0";
        } else {
            out += digits.substr(0, std::size_t(decpt)) + '.' + digits.substr(std::size_t(decpt));
        }
    } else {
        out += digits.substr(0, 1);
        if (digits.size() > 1)
            out += '.' + digits.substr(1);
        const int x = decpt - 1;
        char ebuf[16];
        std::snprintf(ebuf, sizeof ebuf, "e%c%02d", x < 0 ? '-' : '+', std::abs(x));
        out += ebuf;
    }
    return QString::fromLatin1(out);
}

double round(double v)
{
    if (!std::isfinite(v))
        return v;
    const double floor = std::floor(v);
    const double diff = v - floor;
    if (diff < 0.5)
        return floor;
    if (diff > 0.5)
        return floor + 1;
    return std::fmod(floor, 2.0) == 0 ? floor : floor + 1; // ties to even
}

QString groupThousands(const QString &number)
{
    qsizetype start = 0;
    if (start < number.size() && (number[start] == u'-' || number[start] == u'+'))
        ++start;
    qsizetype end = number.indexOf(u'.', start);
    if (end < 0)
        end = number.indexOf(u'e', start);
    if (end < 0)
        end = number.size();
    QString out = number;
    for (qsizetype i = end - 3; i > start; i -= 3)
        out.insert(i, u',');
    return out;
}

QString fixed(double v, int decimals, bool thousands, bool plus)
{
    if (!std::isfinite(v))
        return nonFinite(v, plus);
    char buf[512];
    const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed, decimals);
    QString out = fromAscii(buf, end);
    if (thousands)
        out = groupThousands(out);
    if (plus && !out.startsWith(u'-'))
        out.prepend(u'+');
    return out;
}

QString integer(qint64 n, bool thousands)
{
    const QString out = QString::number(n);
    return thousands ? groupThousands(out) : out;
}

QString groupedRepr(double v)
{
    return groupThousands(repr(v));
}

} // namespace core::py
