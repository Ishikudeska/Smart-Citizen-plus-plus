#include "core/pipeline/Patcher.h"

#include "engine/io/FileSystem.h"
#include "engine/io/RandomAccessFile.h"
#include "engine/xml/DotNetXmlWriter.h"
#include "engine/xml/XmlTree.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <pugixml.hpp>

#include <algorithm>

namespace core {

namespace {

QStringList patchFiles(const QString &patchRoot)
{
    QStringList files;
    for (QDirIterator it(patchRoot, {QStringLiteral("*.patch.json")}, QDir::Files, QDirIterator::Subdirectories);
         it.hasNext();)
        files << it.next();
    std::sort(files.begin(), files.end());
    return files;
}

std::optional<QJsonObject> readJson(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read %1").arg(path);
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        *error = QStringLiteral("%1 is not valid JSON: %2").arg(QFileInfo(path).fileName(), parseError.errorString());
        return std::nullopt;
    }
    return doc.object();
}

std::filesystem::path toFsPath(const QString &path)
{
    return std::filesystem::path(path.toStdU16String());
}

enum class EditOutcome { Applied, AlreadyApplied, Mismatch, NoMatch };

EditOutcome applyEdit(pugi::xml_node root, const QJsonObject &edit)
{
    const QString xpath = edit.value(QStringLiteral("xpath")).toString();
    const QString attribute = edit.value(QStringLiteral("attribute")).toString();
    const QJsonValue expected = edit.value(QStringLiteral("expected"));
    const QJsonValue set = edit.value(QStringLiteral("set"));
    if (xpath.isEmpty() || attribute.isEmpty() || !set.isString())
        return EditOutcome::NoMatch;

    pugi::xpath_node_set nodes;
    try {
        nodes = root.select_nodes(xpath.toUtf8().constData());
    } catch (const pugi::xpath_exception &) {
        return EditOutcome::NoMatch;
    }
    if (nodes.empty())
        return EditOutcome::NoMatch;

    const QByteArray attr = attribute.toUtf8();
    const QString newValue = set.toString();
    bool applied = false, already = false, mismatch = false;
    for (const pugi::xpath_node &n : nodes) {
        pugi::xml_node element = n.node();
        if (!element)
            continue;
        pugi::xml_attribute a = element.attribute(attr.constData());
        const QString current = a ? QString::fromUtf8(a.value()) : QString();
        if (a && current == newValue) {
            already = true;
            continue;
        }
        if (expected.isString() && (!a || current != expected.toString())) {
            mismatch = true;
            continue;
        }
        if (!a)
            a = element.append_attribute(attr.constData());
        a.set_value(newValue.toUtf8().constData());
        applied = true;
    }
    if (applied)
        return EditOutcome::Applied;
    if (already)
        return EditOutcome::AlreadyApplied;
    if (mismatch)
        return EditOutcome::Mismatch;
    return EditOutcome::NoMatch;
}

} // namespace

QString PatchReport::summary() const
{
    return QStringLiteral("patched %1 / already applied %2 / skipped (changed upstream) %3 / no match %4 "
                          "across %5/%6 patch files")
        .arg(editsApplied)
        .arg(editsAlreadyApplied)
        .arg(editsMismatched)
        .arg(editsUnmatched)
        .arg(filesRewritten)
        .arg(patchesSeen);
}

PatchReport applyPatches(const QString &patchRoot, const QString &recordsRoot)
{
    PatchReport report;
    for (const auto files = patchFiles(patchRoot); const QString &patchFile : files) {
        ++report.patchesSeen;
        const QString name = QFileInfo(patchFile).fileName();
        QString error;
        const auto patch = readJson(patchFile, &error);
        if (!patch) {
            report.errors << error;
            continue;
        }
        const QString target = patch->value(QStringLiteral("target")).toString();
        const QJsonArray edits = patch->value(QStringLiteral("edits")).toArray();
        if (target.isEmpty() || edits.isEmpty()) {
            report.errors << QStringLiteral("%1 has no target or edits").arg(name);
            continue;
        }

        const std::filesystem::path targetPath = toFsPath(QDir(recordsRoot).filePath(target));
        auto file = engine::io::RandomAccessFile::open(targetPath);
        if (!file) {
            report.errors << QStringLiteral("patch target missing: %1").arg(target);
            continue;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file->size()));
        if (!file->readAt(0, bytes)) {
            report.errors << QStringLiteral("cannot read %1").arg(target);
            continue;
        }
        file = engine::io::RandomAccessFile(); // close before rewriting

        pugi::xml_document doc;
        if (!doc.load_buffer(bytes.data(), bytes.size())) {
            report.errors << QStringLiteral("%1 is not well-formed XML").arg(target);
            continue;
        }

        bool changed = false;
        for (const QJsonValue &edit : edits) {
            switch (applyEdit(doc.document_element(), edit.toObject())) {
            case EditOutcome::Applied:
                ++report.editsApplied;
                changed = true;
                break;
            case EditOutcome::AlreadyApplied:
                ++report.editsAlreadyApplied;
                break;
            case EditOutcome::Mismatch:
                ++report.editsMismatched;
                break;
            case EditOutcome::NoMatch:
                ++report.editsUnmatched;
                break;
            }
        }
        if (!changed)
            continue;

        engine::xml::XmlTree tree;
        const auto root = tree.copyFrom(doc.document_element());
        std::string out;
        engine::xml::writeDotNet(tree, root, out);
        if (auto ok = engine::io::writeFile(targetPath, out); !ok) {
            report.errors << QString::fromStdString(ok.error().message);
            continue;
        }
        ++report.filesRewritten;
    }
    return report;
}

QList<LocstringWorkaround> loadLocstringWorkarounds(const QString &patchRoot)
{
    QList<LocstringWorkaround> out;
    for (const auto files = patchFiles(patchRoot); const QString &patchFile : files) {
        QString error;
        const auto patch = readJson(patchFile, &error);
        if (!patch)
            continue;
        for (const auto workarounds = patch->value(QStringLiteral("locstring_workarounds")).toArray();
             const QJsonValue &v : workarounds) {
            const QJsonObject o = v.toObject();
            LocstringWorkaround w;
            w.target = o.value(QStringLiteral("target")).toString();
            w.appendFrom = o.value(QStringLiteral("append_from")).toString();
            w.separator = o.value(QStringLiteral("separator")).toString();
            w.description = o.value(QStringLiteral("description")).toString();
            w.patchFile = QFileInfo(patchFile).fileName();
            if (!w.target.isEmpty() && !w.appendFrom.isEmpty())
                out << w;
        }
    }
    return out;
}

int applyLocstringWorkarounds(IniMap &entries, const QList<LocstringWorkaround> &workarounds)
{
    int applied = 0;
    for (const LocstringWorkaround &w : workarounds) {
        const QString *target = entries.find(w.target);
        const QString *from = entries.find(w.appendFrom);
        if (!target || !from)
            continue;
        const QString suffix = w.separator + *from;
        if (target->endsWith(suffix))
            continue;
        entries.insert(w.target, *target + suffix);
        ++applied;
    }
    return applied;
}

} // namespace core
