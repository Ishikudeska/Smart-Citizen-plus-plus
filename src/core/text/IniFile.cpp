#include "core/text/IniFile.h"

#include "core/text/IniText.h"
#include "core/text/PyText.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

namespace core {

void IniMap::insert(const QString &key, const QString &value)
{
    if (const auto it = index_.constFind(key); it != index_.cend()) {
        entries_[*it].second = value;
        return;
    }
    index_.insert(key, entries_.size());
    entries_.append({key, value});
}

bool IniMap::remove(const QString &key)
{
    const auto it = index_.constFind(key);
    if (it == index_.cend())
        return false;
    const qsizetype at = *it;
    entries_.removeAt(at);
    index_.erase(it);
    for (auto &pos : index_)
        if (pos > at)
            --pos;
    return true;
}

void IniMap::clear()
{
    entries_.clear();
    index_.clear();
}

void IniMap::reserve(qsizetype n)
{
    entries_.reserve(n);
    index_.reserve(n);
}

const QString *IniMap::find(const QString &key) const
{
    const auto it = index_.constFind(key);
    return it == index_.cend() ? nullptr : &entries_[*it].second;
}

QString IniMap::value(const QString &key, const QString &fallback) const
{
    const QString *v = find(key);
    return v ? *v : fallback;
}

IniMap parseIni(const QString &text, bool stripValues)
{
    IniMap result;
    result.reserve(text.size() / 64);
    for (QStringView line : QStringView(text).tokenize(u'\n')) {
        line = py::rstrip(line, u'\r');
        const QString stripped = py::strip(line);
        if (stripped.isEmpty() || stripped.startsWith(u';'))
            continue;
        const qsizetype eq = line.indexOf(u'=');
        if (eq < 0)
            continue;
        const QString key = py::strip(line.first(eq));
        if (key.isEmpty())
            continue;
        const qsizetype comma = key.indexOf(u',');
        const QString cleanKey = comma < 0 ? key : py::strip(QStringView(key).first(comma));
        if (cleanKey.isEmpty())
            continue;
        const QStringView value = line.sliced(eq + 1);
        result.insert(cleanKey, stripValues ? py::strip(value) : value.toString());
    }
    return result;
}

IniMap loadIni(const QString &path, bool stripValues)
{
    const auto text = readIniText(path);
    if (!text)
        return {};
    return parseIni(text->text, stripValues);
}

QString formatIni(const IniMap &map)
{
    QString out;
    for (const auto &[key, value] : map) {
        out += key;
        out += u'=';
        out += value;
        out += u'\n';
    }
    return out;
}

QByteArray toCrlfUtf8(const QString &text)
{
    QString crlf = text;
    crlf.replace(u'\n', QStringLiteral("\r\n"));
    return crlf.toUtf8();
}

bool writeIniFile(const QString &path, const IniMap &map)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(toCrlfUtf8(formatIni(map)));
    return file.commit();
}

} // namespace core
