#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <utility>

namespace core {

// Key/value pairs in insertion order with hashed lookup: Python dict
// semantics, which the merge depends on (re-assigning a key keeps its
// position; "first maximal" tie-breaks follow insertion order).
class IniMap
{
public:
    using Entry = std::pair<QString, QString>;

    void insert(const QString &key, const QString &value);
    bool remove(const QString &key);
    void clear();
    void reserve(qsizetype n);

    bool contains(const QString &key) const { return index_.contains(key); }
    const QString *find(const QString &key) const;
    QString value(const QString &key, const QString &fallback = {}) const;

    qsizetype size() const { return entries_.size(); }
    bool isEmpty() const { return entries_.isEmpty(); }

    // Insertion order.
    const QList<Entry> &entries() const { return entries_; }
    auto begin() const { return entries_.cbegin(); }
    auto end() const { return entries_.cend(); }

private:
    QList<Entry> entries_;
    QHash<QString, qsizetype> index_;
};

// Smart Citizen's parse_ini_file over already-decoded text: one key=value
// per line, split on the first '='; blank lines, ';' comments and lines
// without '=' are skipped. Keys are stripped and lose any ",metadata" suffix
// ("key,P" -> "key"). Values are stripped too, unless `stripValues` is false:
// user.ini must round-trip verbatim because a single-space favourite prefix
// is meaningful there (#100). Lines split on '\n' only (not U+2028 etc.).
IniMap parseIni(const QString &text, bool stripValues = true);

// Reads `path` (encoding-tolerant, see IniText) and parses it. A missing or
// unreadable file yields an empty map.
IniMap loadIni(const QString &path, bool stripValues = true);

// The "key=value" text for `map`, one line each, '\n' line ends.
QString formatIni(const IniMap &map);

// Writes UTF-8 without BOM and CRLF line ends, which is what Smart Citizen's
// Windows text-mode writes produce for user.ini and the enhancement INIs.
bool writeIniFile(const QString &path, const IniMap &map);

// Encodes text for a Windows text-mode write: '\n' becomes "\r\n".
QByteArray toCrlfUtf8(const QString &text);

} // namespace core
