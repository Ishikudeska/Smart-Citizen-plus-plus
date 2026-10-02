#include "engine/gamedata/Json.h"

#include "engine/forge/DotNetFormat.h"

#include <cmath>
#include <cstdio>

namespace engine::gamedata::json {

namespace {

constexpr std::string_view kNewLine = "\r\n";

void appendHexEscape(std::string &out, unsigned unit)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "\\u%04X", unit);
    out += buf;
}

// JavaScriptEncoder.UnsafeRelaxedJsonEscaping: everything but controls,
// quote, backslash, line/paragraph separators, private use, lone
// surrogates and non-BMP characters (always escaped as a surrogate pair)
// is written as is.
void appendEscaped(std::string &out, std::string_view s)
{
    out += '"';
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            switch (b) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (b < 0x20 || b == 0x7F)
                    appendHexEscape(out, b);
                else
                    out += static_cast<char>(b);
            }
            ++i;
            continue;
        }
        const int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : 0;
        if (extra == 0 || i + extra >= s.size()) {
            appendHexEscape(out, 0xFFFD); // malformed
            ++i;
            continue;
        }
        char32_t c = b & (0x3F >> extra);
        for (int k = 1; k <= extra; ++k)
            c = (c << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        if (c >= 0x10000) {
            appendHexEscape(out, 0xD800 + ((c - 0x10000) >> 10));
            appendHexEscape(out, 0xDC00 + ((c - 0x10000) & 0x3FF));
        } else if ((c >= 0x80 && c <= 0x9F) || c == 0x2028 || c == 0x2029 || (c >= 0xD800 && c <= 0xDFFF) ||
                   (c >= 0xE000 && c <= 0xF8FF) || c == 0xFFFE || c == 0xFFFF) {
            appendHexEscape(out, c);
        } else {
            out.append(s.substr(i, 1 + extra));
        }
        i += 1 + extra;
    }
    out += '"';
}

class Parser
{
public:
    explicit Parser(std::string_view text) : s_(text) {}

    std::optional<Value> document()
    {
        if (s_.substr(0, 3) == "\xEF\xBB\xBF")
            pos_ = 3;
        Value v;
        if (!parseValue(v, 0))
            return std::nullopt;
        skipWhite();
        if (pos_ != s_.size())
            return std::nullopt;
        return v;
    }

private:
    void skipWhite()
    {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    bool literal(std::string_view word)
    {
        if (s_.substr(pos_, word.size()) != word)
            return false;
        pos_ += word.size();
        return true;
    }

    static void appendUtf8(std::string &out, char32_t c)
    {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }

    bool hex4(unsigned &unit)
    {
        if (pos_ + 4 > s_.size())
            return false;
        unit = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[pos_++];
            unit <<= 4;
            if (c >= '0' && c <= '9')
                unit |= c - '0';
            else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
                unit |= (c | 0x20) - 'a' + 10;
            else
                return false;
        }
        return true;
    }

    bool parseString(std::string &out)
    {
        if (pos_ >= s_.size() || s_[pos_] != '"')
            return false;
        ++pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return false;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size())
                return false;
            switch (s_[pos_++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned unit = 0;
                if (!hex4(unit))
                    return false;
                char32_t cp = unit;
                if (unit >= 0xD800 && unit <= 0xDBFF && s_.substr(pos_, 2) == "\\u") {
                    const std::size_t save = pos_;
                    pos_ += 2;
                    unsigned low = 0;
                    if (hex4(low) && low >= 0xDC00 && low <= 0xDFFF)
                        cp = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                    else
                        pos_ = save;
                }
                appendUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool parseValue(Value &v, int depth)
    {
        if (depth > 64)
            return false;
        skipWhite();
        if (pos_ >= s_.size())
            return false;
        const char c = s_[pos_];
        if (c == '{') {
            ++pos_;
            v.type = Value::Type::Object;
            skipWhite();
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            while (true) {
                skipWhite();
                std::pair<std::string, Value> member;
                if (!parseString(member.first))
                    return false;
                skipWhite();
                if (pos_ >= s_.size() || s_[pos_++] != ':')
                    return false;
                if (!parseValue(member.second, depth + 1))
                    return false;
                v.object.push_back(std::move(member));
                skipWhite();
                if (pos_ < s_.size() && s_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < s_.size() && s_[pos_] == '}') {
                    ++pos_;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            ++pos_;
            v.type = Value::Type::Array;
            skipWhite();
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            while (true) {
                Value item;
                if (!parseValue(item, depth + 1))
                    return false;
                v.array.push_back(std::move(item));
                skipWhite();
                if (pos_ < s_.size() && s_[pos_] == ',') {
                    ++pos_;
                    continue;
                }
                if (pos_ < s_.size() && s_[pos_] == ']') {
                    ++pos_;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            v.type = Value::Type::String;
            return parseString(v.string);
        }
        if (literal("true")) {
            v.type = Value::Type::Bool;
            v.boolean = true;
            return true;
        }
        if (literal("false")) {
            v.type = Value::Type::Bool;
            return true;
        }
        if (literal("null"))
            return true;
        const std::size_t start = pos_;
        if (s_[pos_] == '-')
            ++pos_;
        auto digits = [&] {
            const std::size_t from = pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9')
                ++pos_;
            return pos_ > from;
        };
        if (!digits())
            return false;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            if (!digits())
                return false;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-'))
                ++pos_;
            if (!digits())
                return false;
        }
        v.type = Value::Type::Number;
        v.numberText = std::string(s_.substr(start, pos_ - start));
        v.number = std::strtod(v.numberText.c_str(), nullptr);
        return true;
    }

    std::string_view s_;
    std::size_t pos_ = 0;
};

} // namespace

void Writer::newLine()
{
    out_ += kNewLine;
    out_.append(hasItems_.size() * 2, ' ');
}

void Writer::beforeValue()
{
    if (afterKey_) {
        afterKey_ = false;
        return;
    }
    if (!hasItems_.empty()) {
        if (hasItems_.back())
            out_ += ',';
        hasItems_.back() = true;
        newLine();
    }
}

void Writer::beginObject()
{
    beforeValue();
    out_ += '{';
    hasItems_.push_back(false);
}

void Writer::endObject()
{
    const bool items = hasItems_.back();
    hasItems_.pop_back();
    if (items)
        newLine();
    out_ += '}';
}

void Writer::beginArray()
{
    beforeValue();
    out_ += '[';
    hasItems_.push_back(false);
}

void Writer::endArray()
{
    endObject();
    out_.back() = ']';
}

void Writer::key(std::string_view name)
{
    beforeValue();
    appendEscaped(out_, name);
    out_ += ": ";
    afterKey_ = true;
}

void Writer::value(std::string_view utf8)
{
    beforeValue();
    appendEscaped(out_, utf8);
}

void Writer::value(double v)
{
    beforeValue();
    forge::appendDouble(out_, v);
}

void Writer::value(std::int32_t v)
{
    beforeValue();
    out_ += std::to_string(v);
}

void Writer::null()
{
    beforeValue();
    out_ += "null";
}

const Value *Value::find(std::string_view name) const
{
    for (auto it = object.rbegin(); it != object.rend(); ++it)
        if (it->first == name)
            return &it->second;
    return nullptr;
}

std::optional<Value> parse(std::string_view text)
{
    return Parser(text).document();
}

} // namespace engine::gamedata::json
