#include "core/merge/SourceLoader.h"

#include "core/model/Enhancements.h"

#include <QDir>
#include <QFileInfo>

namespace core {

LoadedSources loadSources(const SourceFiles &files)
{
    LoadedSources out;

    if (QFileInfo::exists(files.baseIni)) {
        IniMap base = loadIni(files.baseIni);
        if (base.isEmpty())
            out.problems << QStringLiteral("%1 is empty or unreadable").arg(files.baseIni);
        else
            out.sources[kSourceGlobal] = std::move(base);
    } else {
        out.problems << QStringLiteral("%1 not found").arg(files.baseIni);
    }
    out.hierarchy << kSourceGlobal;

    IniMap combined;
    for (const auto &file : enhancements::files()) {
        const QString id = QString::fromLatin1(file.id);
        if (!files.enhancementFileIds.contains(id))
            continue;
        const QString path = QDir(files.enhancementsDir).filePath(QString::fromLatin1(file.fileName));
        if (!QFileInfo::exists(path))
            continue;
        const IniMap data = loadIni(path);
        const QString label = enhancements::categoryLabelForFile(id);
        for (const auto &[key, value] : data) {
            if (!label.isEmpty())
                out.enhancementCategories.insert(key, label);
            combined.insert(key, value);
        }
    }
    if (!combined.isEmpty()) {
        out.sources[kSourceEnhancements] = std::move(combined);
        out.hierarchy << kSourceEnhancements;
    }

    if (!files.userIni.isEmpty() && QFileInfo::exists(files.userIni)) {
        IniMap user =
            loadIni(files.userIni, /*stripValues=*/false); // a space favourite prefix must survive (#100)
        if (!user.isEmpty())
            out.sources[kSourceUser] = std::move(user);
    }
    out.hierarchy << kSourceUser;
    return out;
}

EntryStatus statusFromSource(const QString &source, const QString &baseSource, bool keyInBase,
                             bool inGlobalSource)
{
    if (!keyInBase)
        return EntryStatus::New; // only the user has it
    if (!inGlobalSource)
        return EntryStatus::New; // only the enhancements have it (found in DataForge)
    if (source == kSourceUser)
        return EntryStatus::Modified;
    if (source == kSourceEnhancements)
        return EntryStatus::Enhanced;
    if (source == baseSource)
        return EntryStatus::Unmodified;
    return EntryStatus::Modified; // some other higher-priority source
}

QList<StringEntry> buildEntries(const LoadedSources &loaded, const IniMap *userOverrides)
{
    const IniMap empty;
    const IniMap *user = &empty;
    if (userOverrides && !userOverrides->isEmpty())
        user = userOverrides;
    else if (const auto it = loaded.sources.find(kSourceUser); it != loaded.sources.end())
        user = &it->second;

    QStringList baseHierarchy;
    for (const QString &name : loaded.hierarchy)
        if (name != kSourceUser && loaded.sources.contains(name))
            baseHierarchy << name;
    const QString baseSource = baseHierarchy.isEmpty() ? kSourceGlobal : baseHierarchy.first();

    // Merge without the user layer, so originalValue is the pre-edit baseline.
    const IniMap baseMerged = mergeSourcesByHierarchy(loaded.sources, baseHierarchy, nullptr);

    // The last base source holding each key.
    QHash<QString, QString> origin;
    origin.reserve(baseMerged.size());
    for (const QString &name : std::as_const(baseHierarchy))
        for (const auto &[key, value] : loaded.sources.at(name))
            origin.insert(key, name);

    const auto globalIt = loaded.sources.find(baseSource);
    const IniMap *globalSource = globalIt == loaded.sources.end() ? &empty : &globalIt->second;

    QList<StringEntry> entries;
    entries.reserve(baseMerged.size() + user->size());
    auto add = [&](const QString &key) {
        // Short ship names (vehicle_Name*_short) are not editable rows.
        if (key.startsWith(u"vehicle_name", Qt::CaseInsensitive) && key.contains(u"_short"))
            return;

        StringEntry e;
        e.key = key;
        e.originalValue = baseMerged.value(key);
        e.customValue = user->value(key);
        const bool keyInBase = baseMerged.contains(key);
        if (!e.customValue.isEmpty() && keyInBase)
            e.status = EntryStatus::Modified;
        else
            e.status = statusFromSource(origin.value(key, baseSource), baseSource, keyInBase,
                                        globalSource->contains(key));
        e.sourceFile = origin.value(key, keyInBase ? baseSource : kSourceUser);

        if (key.contains(u"journal", Qt::CaseInsensitive))
            e.category = category::kJournal;
        else if (const auto it = loaded.enhancementCategories.constFind(key);
                 it != loaded.enhancementCategories.cend())
            e.category = *it;
        else
            e.category = extractCategory(key);
        entries.append(std::move(e));
    };

    for (const auto &[key, value] : baseMerged)
        add(key);
    for (const auto &[key, value] : *user)
        if (!baseMerged.contains(key))
            add(key);
    return entries;
}

} // namespace core
