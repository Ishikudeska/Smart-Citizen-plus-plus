#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::gamedata::json {

// System.Text.Json's Utf8JsonWriter with WriteIndented, UnsafeRelaxedJsonEscaping
// and Environment.NewLine (CRLF): two-space indent, "key": value, numbers
// as .NET formats them. Callers write keys in the order the C# properties
// are declared and skip the ones that are null.
class Writer
{
public:
    void beginObject();
    void endObject();
    void beginArray();
    void endArray();

    void key(std::string_view name);
    void value(std::string_view utf8);
    void value(const char *utf8) { value(std::string_view(utf8)); }
    void value(double v);
    void value(std::int32_t v);
    void null();

    // Shorthands for "key": value.
    template <class T>
    void field(std::string_view name, const T &v)
    {
        key(name);
        value(v);
    }
    template <class T>
    void field(std::string_view name, const std::optional<T> &v)
    {
        if (v) {
            key(name);
            value(*v);
        }
    }

    const std::string &text() const { return out_; }

private:
    void beforeValue();
    void newLine();

    std::string out_;
    std::vector<bool> hasItems_; // per open container
    bool afterKey_ = false;
};

// A parsed JSON value, enough for reading an overlay game_data.json.
struct Value
{
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string numberText; // as written, for Int32 checks
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object; // in document order

    const Value *find(std::string_view name) const; // the last property with that name
};

std::optional<Value> parse(std::string_view text);

} // namespace engine::gamedata::json
