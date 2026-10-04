#include "engine/gamedata/DotNet.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace engine::gamedata::dotnet {

namespace {

bool isNumberWhite(char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

std::string_view trimNumberWhite(std::string_view s)
{
    while (!s.empty() && isNumberWhite(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isNumberWhite(s.back()))
        s.remove_suffix(1);
    return s;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return (x | 0x20) == (y | 0x20); });
}

// Decodes one UTF-8 code point at `s[i]`, advancing `i`; malformed bytes
// decode as themselves.
char32_t decodeAt(std::string_view s, std::size_t &i)
{
    const auto b = static_cast<unsigned char>(s[i]);
    int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : 0;
    if (extra && i + extra >= s.size())
        extra = 0;
    char32_t c = extra == 0 ? b : b & (0x3F >> extra);
    for (int k = 1; k <= extra; ++k)
        c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += 1 + extra;
    return c;
}

// UTF-8 to UTF-16 code units.
std::u16string toUtf16(std::string_view s)
{
    std::u16string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const char32_t c = decodeAt(s, i);
        if (c >= 0x10000) {
            out.push_back(static_cast<char16_t>(0xD800 + ((c - 0x10000) >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + ((c - 0x10000) & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(c));
        }
    }
    return out;
}

// icu.dll (Windows 10 1903+), the library .NET's globalization uses.
struct Icu
{
    using Open = void *(*)(const char *, int *);
    using StrColl = int (*)(const void *, const char16_t *, std::int32_t, const char16_t *, std::int32_t);
    StrColl strcoll = nullptr;
    void *collator = nullptr;
    std::mutex mutex; // a UCollator isn't safe for concurrent use

    Icu()
    {
#ifdef _WIN32
        HMODULE lib = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!lib)
            return;
        const auto open = reinterpret_cast<Open>(reinterpret_cast<void *>(GetProcAddress(lib, "ucol_open")));
        strcoll = reinterpret_cast<StrColl>(reinterpret_cast<void *>(GetProcAddress(lib, "ucol_strcoll")));
        if (!open || !strcoll)
            return;
        int status = 0;
        collator = open("", &status);
        if (status > 0)
            collator = nullptr;
#endif
    }
};

Icu &icu()
{
    static Icu instance;
    return instance;
}

} // namespace

std::optional<double> parseDouble(std::string_view s)
{
    s = trimNumberWhite(s);
    if (s.empty())
        return std::nullopt;
    bool negative = false;
    std::string_view body = s;
    if (body.front() == '+' || body.front() == '-') {
        negative = body.front() == '-';
        body.remove_prefix(1);
    }
    if (equalsIgnoreCase(s, "NaN"))
        return std::nan("");
    if (equalsIgnoreCase(body, "Infinity") || body == "\xE2\x88\x9E")
        return negative ? -HUGE_VAL : HUGE_VAL;
    // digits [. digits] [e [sign] digits], at least one mantissa digit
    std::size_t i = 0, mantissa = 0;
    while (i < body.size() && body[i] >= '0' && body[i] <= '9')
        ++i, ++mantissa;
    if (i < body.size() && body[i] == '.') {
        ++i;
        while (i < body.size() && body[i] >= '0' && body[i] <= '9')
            ++i, ++mantissa;
    }
    if (mantissa == 0)
        return std::nullopt;
    if (i < body.size() && (body[i] == 'e' || body[i] == 'E')) {
        std::size_t j = i + 1;
        if (j < body.size() && (body[j] == '+' || body[j] == '-'))
            ++j;
        const std::size_t digits = j;
        while (j < body.size() && body[j] >= '0' && body[j] <= '9')
            ++j;
        if (j == digits)
            return std::nullopt;
        i = j;
    }
    if (i != body.size())
        return std::nullopt;
    std::string text(body);
    if (text.front() == '.')
        text.insert(text.begin(), '0');
    double value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ptr != text.data() + text.size() || (ec != std::errc() && ec != std::errc::result_out_of_range))
        return std::nullopt;
    if (ec == std::errc::result_out_of_range) // .NET Core 3.0+: overflow to infinity, underflow to zero
        value = std::strtod(text.c_str(), nullptr);
    return negative ? -value : value;
}

std::optional<std::int32_t> parseInt32(std::string_view s)
{
    s = trimNumberWhite(s);
    bool negative = false;
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) {
        negative = s.front() == '-';
        s.remove_prefix(1);
    }
    if (s.empty())
        return std::nullopt;
    std::int64_t value = 0;
    for (const char c : s) {
        if (c < '0' || c > '9')
            return std::nullopt;
        value = value * 10 + (c - '0');
        if (value > 2147483648LL)
            return std::nullopt;
    }
    if (negative)
        value = -value;
    if (value > 2147483647LL)
        return std::nullopt;
    return static_cast<std::int32_t>(value);
}

double round(double value, int decimals)
{
    static constexpr double kPower10[] = {1e0, 1e1, 1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                          1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15};
    if (std::abs(value) < 1e16) {
        const double p = kPower10[decimals];
        value *= p;
        value = std::nearbyint(value); // the default rounding mode: half to even
        value /= p;
    }
    return value;
}

bool isWhiteSpace(char32_t c)
{
    return c == ' ' || (c >= 0x09 && c <= 0x0D) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
           c == 0x3000;
}

std::string_view trimEnd(std::string_view s)
{
    while (!s.empty()) {
        // Step back to the start of the last code point.
        std::size_t start = s.size() - 1;
        while (start > 0 && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80)
            --start;
        std::size_t i = start;
        if (!isWhiteSpace(decodeAt(s, i)))
            break;
        s = s.substr(0, start);
    }
    return s;
}

std::string_view trim(std::string_view s)
{
    while (!s.empty()) {
        std::size_t i = 0;
        if (!isWhiteSpace(decodeAt(s, i)))
            break;
        s.remove_prefix(i);
    }
    return trimEnd(s);
}

int compareOrdinal(std::string_view a, std::string_view b)
{
    const std::u16string ua = toUtf16(a), ub = toUtf16(b);
    const int c = ua.compare(ub);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

int compareCulture(std::string_view a, std::string_view b)
{
    Icu &lib = icu();
    const std::u16string ua = toUtf16(a), ub = toUtf16(b);
    if (lib.collator) {
        std::lock_guard lock(lib.mutex);
        return lib.strcoll(lib.collator, ua.data(), static_cast<std::int32_t>(ua.size()), ub.data(),
                           static_cast<std::int32_t>(ub.size()));
    }
    std::u16string la = ua, lb = ub;
    for (auto *s : {&la, &lb})
        for (char16_t &c : *s)
            if (c >= u'A' && c <= u'Z')
                c = static_cast<char16_t>(c + 32);
    if (const int c = la.compare(lb))
        return c < 0 ? -1 : 1;
    const int c = ub.compare(ua); // lower case first, as ICU's tertiary level
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

std::string utcNowRoundTrip()
{
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto secs = floor<seconds>(now);
    const auto ticks = duration_cast<duration<std::int64_t, std::ratio<1, 10000000>>>(now - secs).count();
    const std::time_t t = system_clock::to_time_t(secs);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%07lldZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<long long>(ticks));
    return buf;
}

} // namespace engine::gamedata::dotnet
