#include "engine/forge/DotNetFormat.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <system_error>

namespace engine::forge {

namespace {

// Lays out shortest round-trip digits the way .NET's FormatGeneral does.
// `scientific` is std::to_chars output such as "1.093429e+09" or "5e-01".
void layoutGeneral(std::string &out, const char *scientific, std::size_t length, bool negative, int minPrecision)
{
    char digits[32];
    std::size_t digitCount = 0;
    const char *p = scientific;
    const char *end = scientific + length;
    while (p < end && *p != 'e') {
        if (*p >= '0' && *p <= '9')
            digits[digitCount++] = *p;
        ++p;
    }
    int exponent = 0;
    if (p < end) {
        ++p; // 'e'
        std::from_chars(*p == '+' ? p + 1 : p, end, exponent);
    }
    while (digitCount > 1 && digits[digitCount - 1] == '0')
        --digitCount;

    if (negative)
        out.push_back('-');

    // .NET's Number.Scale: value = 0.d1d2d3... x 10^scale.
    const int scale = exponent + 1;
    const int maxDigits = std::max(static_cast<int>(digitCount), minPrecision);

    if (scale > maxDigits || scale < -3) {
        out.push_back(digits[0]);
        if (digitCount > 1) {
            out.push_back('.');
            out.append(digits + 1, digitCount - 1);
        }
        const int e = scale - 1;
        out.push_back('E');
        out.push_back(e < 0 ? '-' : '+');
        const int magnitude = e < 0 ? -e : e;
        if (magnitude < 10)
            out.push_back('0');
        out += std::to_string(magnitude);
        return;
    }

    if (scale > 0) {
        for (int i = 0; i < scale; ++i)
            out.push_back(i < static_cast<int>(digitCount) ? digits[i] : '0');
        if (static_cast<int>(digitCount) > scale) {
            out.push_back('.');
            out.append(digits + scale, digitCount - static_cast<std::size_t>(scale));
        }
    } else {
        out.push_back('0');
        out.push_back('.');
        out.append(static_cast<std::size_t>(-scale), '0');
        out.append(digits, digitCount);
    }
}

template <class T>
void appendFloating(std::string &out, T value, int minPrecision)
{
    if (std::isnan(value)) {
        out += "NaN";
        return;
    }
    if (std::isinf(value)) {
        out += value < 0 ? "-Infinity" : "Infinity";
        return;
    }
    const bool negative = std::signbit(value);
    if (value == 0) {
        out += negative ? "-0" : "0";
        return;
    }
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, negative ? -value : value,
                                      std::chars_format::scientific);
    layoutGeneral(out, buffer, static_cast<std::size_t>(result.ptr - buffer), negative, minPrecision);
}

} // namespace

void appendSingle(std::string &out, float value)
{
    appendFloating(out, value, 9);
}

void appendDouble(std::string &out, double value)
{
    appendFloating(out, value, 17);
}

std::string formatSingle(float value)
{
    std::string out;
    appendSingle(out, value);
    return out;
}

std::string formatDouble(double value)
{
    std::string out;
    appendDouble(out, value);
    return out;
}

void appendGuid(std::string &out, const std::uint8_t *b)
{
    static constexpr char kHex[] = "0123456789abcdef";
    auto hex = [&out](std::uint8_t v) {
        out.push_back(kHex[v >> 4]);
        out.push_back(kHex[v & 15]);
    };
    // a = int32 at [4..7], b = int16 at [2..3], c = int16 at [0..1], all little-endian.
    hex(b[7]);
    hex(b[6]);
    hex(b[5]);
    hex(b[4]);
    out.push_back('-');
    hex(b[3]);
    hex(b[2]);
    out.push_back('-');
    hex(b[1]);
    hex(b[0]);
    out.push_back('-');
    hex(b[15]);
    hex(b[14]);
    out.push_back('-');
    for (int i = 13; i >= 8; --i)
        hex(b[i]);
}

std::string formatGuid(const std::uint8_t *bytes)
{
    std::string out;
    out.reserve(36);
    appendGuid(out, bytes);
    return out;
}

} // namespace engine::forge
