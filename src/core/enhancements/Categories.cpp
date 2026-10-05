#include "core/enhancements/Categories.h"

#include "core/text/PyText.h"

#include <QFileInfo>
#include <QLoggingCategory>

#include <algorithm>

Q_LOGGING_CATEGORY(lcEnh, "scx.enhancements")

namespace core::enh {

const tags::TagConfig &Context::config(const QString &category) const
{
    const auto it = tagConfigs.constFind(category);
    return it != tagConfigs.cend() ? *it : defaultTagConfig(category);
}

QString Context::missionHeader(const QString &key) const
{
    static const QHash<QString, QString> defaults = {
        {QStringLiteral("details"), QStringLiteral("MISSION DETAILS")},
        {QStringLiteral("blueprints"), QStringLiteral("POTENTIAL BLUEPRINTS")},
        {QStringLiteral("items"), QStringLiteral("ITEM REWARDS")},
        {QStringLiteral("blueprint_data"), QStringLiteral("BLUEPRINT DATA")},
    };
    return missionHeaders.value(key, defaults.value(key));
}

Loc scanEntityDir(const RecordStore &store, const QString &relDir, const EnhancementFn &fn, const Loc &loc,
                  const ScanOptions &options)
{
    Loc out;
    int matched = 0, missed = 0, skipped = 0, discovered = 0;
    const auto keyFn = options.locKeyFn ? options.locKeyFn : locKey;
    for (const auto files = store.indexRglob(relDir); const QString &file : files) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const Node root = doc.root();
        const QString key = keyFn(root);
        if (key.isEmpty()) {
            ++skipped;
            continue;
        }
        const QString *stock = loc.find(key);
        const bool isDiscovered = stock == nullptr;
        const QString base = isDiscovered ? synthesizeDescription(root, file, key) : *stock;
        discovered += isDiscovered ? 1 : 0;

        QString block;
        try {
            block = fn(root);
        } catch (const PyError &e) {
            qCWarning(lcEnh) << "Enhancements failed for" << QFileInfo(file).fileName() << e.what();
            continue;
        }
        if (!block.isEmpty()) {
            out.insert(key, appendEnhancements(base, block, options.separator, options.prepend));
            ++matched;
        } else if (options.captureAll || isDiscovered) {
            if (!out.contains(key))
                out.insert(key, base);
            ++matched;
        } else {
            ++missed;
        }

        // The item_Name tag ([MIL-S1-A], [IR-S2], ...), and its _short.
        if (options.generateNameTags && !loc.isEmpty()) {
            const QString nameKey = locNameKey(root);
            if (nameKey.isEmpty())
                continue;
            const QString *nameStock = loc.find(nameKey);
            const QString name = nameStock ? *nameStock : humanizeKey(fileStem(file));
            if (name.isEmpty())
                continue;
            QString source = options.tagLoc ? options.tagLoc->value(key) : QString();
            if (source.isEmpty())
                source = base;
            const QString tag =
                options.nameTagger ? options.nameTagger(source, root) : componentNameTag(source, root);
            if (tag.isEmpty())
                continue;
            out.insert(nameKey, tags::joinTag(name, tag, options.nameTagPlacement));
            const QString shortKey = nameKey + QStringLiteral("_short");
            if (const QString shortValue = loc.value(shortKey); !shortValue.isEmpty())
                out.insert(shortKey, tags::joinTag(shortValue, tag, options.nameTagPlacement));
        }
    }
    qCInfo(lcEnh).noquote() << QStringLiteral(
                                   "%1: %2 matched, %3 discovered, %4 no enhancements, %5 no loc key")
                                   .arg(relDir.section(u'/', -1))
                                   .arg(matched)
                                   .arg(discovered)
                                   .arg(missed)
                                   .arg(skipped);
    return out;
}

namespace {

// The stats part of `value` grafted onto `base`, or `value` when it has none.
QString graftStats(const QString &base, const QString &value)
{
    const qsizetype at = value.indexOf(kEnhancementSeparator);
    return at >= 0 ? base + value.sliced(at) : value;
}

// "Name [MIL-S1-A]" -> "<base> [MIL-S1-A]", or `value` without a trailing tag.
QString graftTag(const QString &base, const QString &value)
{
    static const QRegularExpression re = py::re(QStringLiteral(R"(\s(\[[A-Z0-9\-]+\])\s*$)"));
    const QRegularExpressionMatch m = re.match(value);
    return m.hasMatch() ? base + u' ' + m.captured(1) : value;
}

constexpr std::array kCompTypes = {"COOL", "SHLD", "POWR", "QDRV", "RADR"};

} // namespace

std::pair<int, int> mirrorScitemSiblings(Loc &out, const Loc &loc)
{
    int siblings = 0;
    const QList<Loc::Entry> snapshot = out.entries();
    for (const auto &[key, value] : snapshot) {
        if (!key.endsWith(QStringLiteral("_SCItem")))
            continue;
        const QString baseKey = key.chopped(7);
        QStringList targets = {baseKey};
        if (baseKey.startsWith(QStringLiteral("item_name")))
            targets << QStringLiteral("item_Name") + baseKey.sliced(9);
        else if (baseKey.startsWith(QStringLiteral("item_desc")))
            targets << QStringLiteral("item_Desc") + baseKey.sliced(9);
        for (const QString &target : std::as_const(targets)) {
            const QString *stock = loc.find(target);
            if (!stock || out.contains(target))
                continue;
            if (target.startsWith(QStringLiteral("item_Desc")))
                out.insert(target, graftStats(*stock, value));
            else if (target.startsWith(QStringLiteral("item_Name")))
                out.insert(target, graftTag(*stock, value));
            else
                out.insert(target, value);
            ++siblings;
        }
        for (const char *ct : kCompTypes) {
            const QString code = QString::fromLatin1(ct);
            const QString descPrefix = QStringLiteral("item_Desc%1_").arg(code);
            if (baseKey.startsWith(descPrefix)) {
                const QString sibling =
                    QStringLiteral("item_Desc_%1_%2").arg(code, baseKey.sliced(descPrefix.size()));
                if (!out.contains(sibling))
                    if (const QString *stock = loc.find(sibling)) {
                        out.insert(sibling, graftStats(*stock, value));
                        ++siblings;
                    }
                break;
            }
            const QString namePrefix = QStringLiteral("item_name%1_").arg(code);
            if (baseKey.startsWith(namePrefix)) {
                const QString sibling =
                    QStringLiteral("item_Name_%1_%2").arg(code, baseKey.sliced(namePrefix.size()));
                if (!out.contains(sibling) && loc.contains(sibling)) {
                    out.insert(sibling, value);
                    ++siblings;
                }
                break;
            }
        }
    }

    int legacy = 0;
    const QList<Loc::Entry> snapshot2 = out.entries();
    for (const auto &[key, value] : snapshot2) {
        for (const auto &[with, without] :
             {std::pair{"item_Desc_", "item_Desc"}, {"item_Name_", "item_Name"}}) {
            const QString prefixWith = QString::fromLatin1(with);
            if (!key.startsWith(prefixWith))
                continue;
            const QString rest = key.sliced(prefixWith.size());
            if (rest.isEmpty() || !rest.contains(u'_'))
                continue;
            const QString head = rest.section(u'_', 0, 0);
            if (std::none_of(kCompTypes.begin(), kCompTypes.end(),
                             [&](const char *c) { return head == QLatin1StringView(c); }))
                continue;
            const QString sibling = QString::fromLatin1(without) + rest;
            const QString *stock = loc.find(sibling);
            if (out.contains(sibling) || !stock)
                continue;
            out.insert(sibling,
                       prefixWith == u"item_Desc_" ? graftStats(*stock, value) : graftTag(*stock, value));
            ++legacy;
            break;
        }
    }
    return {siblings, legacy};
}

Loc generateComponents(const Context &ctx)
{
    const tags::TagConfig &cfg = ctx.config(QStringLiteral("components"));
    const auto tagger = [&cfg](const QString &type) -> NameTagger {
        return
            [&cfg, type](const QString &desc, Node root) { return componentNameTag(desc, root, &cfg, type); };
    };
    ScanOptions options;
    options.generateNameTags = true;
    options.nameTagPlacement = cfg.placement;
    options.tagLoc = ctx.tagLoc;
    options.prepend = ctx.statsPrepend;

    Loc out;
    const QString ships = QStringLiteral("entities/scitem/ships/");
    const std::array<std::tuple<const char *, const char *, QString (*)(Node)>, 5> passes = {{
        {"shieldgenerator", "Shield Generator", enhancementsShield},
        {"cooler", "Cooler", enhancementsCooler},
        {"powerplant", "Power Plant", enhancementsPowerplant},
        {"quantumdrive", "Quantum Drive", enhancementsQuantumDrive},
        {"bombcompartments", "", enhancementsBombRack},
    }};
    for (const auto &[subdir, type, fn] : passes) {
        options.nameTagger = tagger(QString::fromLatin1(type));
        for (const auto &[k, v] :
             scanEntityDir(*ctx.store, ships + QString::fromLatin1(subdir), fn, *ctx.loc, options))
            out.insert(k, v);
    }
    if (ctx.store->dirExists(ships + QStringLiteral("radar"))) {
        options.nameTagger = tagger(QStringLiteral("Radar"));
        for (const auto &[k, v] :
             scanEntityDir(*ctx.store, ships + QStringLiteral("radar"), enhancementsRadar, *ctx.loc, options))
            out.insert(k, v);
    }
    const auto [siblings, legacy] = mirrorScitemSiblings(out, *ctx.loc);
    if (siblings || legacy)
        qCInfo(lcEnh) << "Propagated enhancements to" << siblings << "_SCItem siblings and" << legacy
                      << "legacy no-underscore siblings";
    for (const auto &[k, v] : bareTypeTags(*ctx.loc, &cfg))
        out.insert(k, v);
    return out;
}

Loc generateMissiles(const Context &ctx)
{
    const tags::TagConfig &cfg = ctx.config(QStringLiteral("missiles"));
    ScanOptions options;
    options.generateNameTags = true;
    options.nameTagger = [&cfg](const QString &desc, Node root) { return missileNameTag(desc, root, &cfg); };
    options.nameTagPlacement = cfg.placement;
    options.tagLoc = ctx.tagLoc;
    options.prepend = ctx.statsPrepend;
    Loc out;
    for (const char *dir :
         {"entities/scitem/ships/weapons/missiles", "entities/scitem/ships/weapons/rocket_pods"}) {
        const QString rel = QString::fromLatin1(dir);
        if (!ctx.store->dirExists(rel))
            continue;
        for (const auto &[k, v] : scanEntityDir(*ctx.store, rel, enhancementsMissile, *ctx.loc, options))
            out.insert(k, v);
    }
    return out;
}

Loc generateShipWeapons(const Context &ctx)
{
    const QString dir = QStringLiteral("entities/scitem/ships/weapons");
    if (!ctx.store->dirExists(dir))
        return {};
    const tags::TagConfig &cfg = ctx.config(QStringLiteral("ship_weapons"));
    ScanOptions options;
    options.generateNameTags = true;
    options.nameTagger =
        shipWeaponNameTagger(ctx.vehicleAmmo, &cfg, &ctx.config(QStringLiteral("components")));
    options.nameTagPlacement = cfg.placement;
    options.tagLoc = ctx.tagLoc;
    options.prepend = ctx.statsPrepend;
    // Mining lasers share the folder with combat weapons.
    const auto dispatch = [&ctx](Node root) {
        if (findDescendant(root, "SEntityComponentMiningLaserParams"))
            return enhancementsMiningLaser(root);
        return enhancementsWeapon(root, ctx.vehicleAmmo, ctx.loc, nullptr);
    };
    return scanEntityDir(*ctx.store, dir, dispatch, *ctx.loc, options);
}

Loc generateFpsWeapons(const Context &ctx)
{
    const QString dir = QStringLiteral("entities/scitem/weapons/fps_weapons");
    if (!ctx.store->dirExists(dir))
        return {};
    ScanOptions options;
    options.prepend = ctx.statsPrepend;
    // Handheld salvage tools share the folder with combat weapons.
    const auto dispatch = [&ctx](Node root) {
        if (findDescendant(root, "SWeaponActionFireSalvageRepairParams"))
            return enhancementsSalvageTool(root);
        return enhancementsWeapon(root, ctx.fpsAmmo, ctx.loc, &ctx.scitem.magazines);
    };
    return scanEntityDir(*ctx.store, dir, dispatch, *ctx.loc, options);
}

Loc generateShips(const Context &ctx)
{
    const QString dir = QStringLiteral("entities/spaceships");
    QStringList files = ctx.store->filesIn(dir);
    // sorted(Path(...)): Windows paths compare lower-cased.
    std::stable_sort(files.begin(), files.end(), [](const QString &a, const QString &b) {
        return py::less(QFileInfo(a).fileName().toLower(), QFileInfo(b).fileName().toLower());
    });
    Loc out;
    int matched = 0, missed = 0, skipped = 0, discovered = 0;
    for (const QString &file : std::as_const(files)) {
        const QString stem = fileStem(file).toLower();
        if (stem.contains(u"_pu_ai_") || stem.contains(u"_ai_template") || stem.contains(u"_unmanned_")) {
            ++skipped;
            continue;
        }
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const Node root = doc.root();
        const Node vpc = findDescendant(root, "VehicleComponentParams");
        if (!vpc) {
            ++skipped;
            continue;
        }
        const std::string_view descAttr = getOr(vpc, "vehicleDescription");
        if (!descAttr.starts_with('@') || isSentinelLocRef(descAttr)) {
            ++skipped;
            continue;
        }
        QString key = qs(descAttr);
        while (key.startsWith(u'@'))
            key.remove(0, 1);
        const QString *stock = ctx.loc->find(key);
        const bool isDiscovered = stock == nullptr;
        const QString base = isDiscovered ? synthesizeDescription(root, file, key) : *stock;
        discovered += isDiscovered ? 1 : 0;

        const QString shipClass = recordClassName(root, stem).toLower();
        const Node controller = ctx.controllers.value(shipClass);
        QString block;
        try {
            block = enhancementsShip(root, controller, ctx.loc, &ctx.armor);
        } catch (const PyError &e) {
            qCWarning(lcEnh) << "Ship enhancements failed for" << QFileInfo(file).fileName() << e.what();
            continue;
        }
        if (!block.isEmpty()) {
            if (!out.contains(key)) {
                out.insert(key, appendEnhancements(base, block, kEnhancementSeparator, ctx.statsPrepend));
                ++matched;
            }
        } else if (isDiscovered) {
            if (!out.contains(key))
                out.insert(key, base);
            ++matched;
        } else {
            ++missed;
        }
    }
    qCInfo(lcEnh) << "Spaceships:" << matched << "matched," << discovered << "discovered," << missed
                  << "no enhancements/key," << skipped << "skipped (AI/templates)";
    return out;
}

Loc generateMedicalConsumables(const Context &ctx)
{
    // CureLife pens: their descriptions are lore only.
    static const std::array<std::pair<const char *, const char *>, 7> effects = {{
        {"item_Desccrlf_consumable_adrenaline_01",
         "Reduces concussion symptoms, normalizes weapon handling and movement speed."},
        {"item_Desccrlf_consumable_steroids_01", "Reduces vision and hearing symptoms, normalizes stamina."},
        {"item_Desccrlf_consumable_radiation_01", "Reduces injuries from radiation."},
        {"item_Desccrlf_consumable_overdoseRevival_01",
         "Revives an overdosed person (if not incapacitated), doubles decay rate of Blood Drug Level."},
        {"item_Desccrlf_consumable_healing_01", "Restores health and stops bleeding. When used on another "
                                                "person recovers from incapacitated state."},
        {"item_Desccrlf_consumable_painkiller_01", "Reduces pain symptoms, normalizes movement ability."},
        {"item_Desccrlf_consumable_oxygen_01", "Recharges Oxygen reserves of a suit."},
    }};
    Loc out;
    for (const auto &[key, effect] : effects) {
        const QString k = QString::fromLatin1(key);
        if (const QString *stock = ctx.loc->find(k))
            out.insert(
                k, appendEnhancements(*stock, QString::fromUtf8(effect), kEffectSeparator, ctx.statsPrepend));
    }
    return out;
}

} // namespace core::enh
