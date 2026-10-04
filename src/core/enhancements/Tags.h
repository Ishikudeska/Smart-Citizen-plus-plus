#pragma once

#include "core/enhancements/Common.h"
#include "core/tags/TagBuilder.h"

#include <QHash>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

// Name tags the generator puts on item names ("[MIL-S1-A] Bracer",
// "[IR-S2] Arrester", "[Energy-S3] ..."), read from the item's description
// text and record. An empty string stands for Python's None.
namespace core::enh {

// Records keyed by __ref (ammo), kept alive with their documents.
struct RecordLookup
{
    QHash<QString, Node> byId;
    std::vector<XmlDoc> docs;
    Node value(const QString &id) const { return byId.value(id); }
};

// (description, record) -> tag or "".
using NameTagger = std::function<QString(const QString &desc, Node root)>;

// _component_name_tag: the full Size/Grade/Class trio renders through the
// Tag Builder; leaner items fall back to [TYPE-Sx(-grade)] from Item Type.
QString componentNameTag(const QString &desc, Node root, const tags::TagConfig *config = nullptr,
                         const QString &componentType = {});
QString missileNameTag(const QString &desc, Node root, const tags::TagConfig *config = nullptr);

// Type(+Size) config for mining lasers from the components config; nothing
// when the Type element is off.
std::optional<tags::TagConfig> miningLaserTagConfig(const tags::TagConfig *componentsConfig);
QString miningLaserComponentTag(const QString &desc, Node root,
                                const std::optional<tags::TagConfig> &miningConfig);

// _ship_weapon_name_tag_factory: damage type of the weapon's ammo plus
// size; mining lasers get the component Type+Size shape instead.
NameTagger shipWeaponNameTagger(const RecordLookup &ammo, const tags::TagConfig *config,
                                const tags::TagConfig *miningLaserConfig);

const tags::ElementSpec *componentElement(const tags::TagConfig &config, const QString &kind);

// Type-only tag for size-less items whose "Item Type:" is Fuel Nozzle.
QString bareTypeTagFromDesc(const QString &desc, const tags::TagConfig *componentsConfig);
// Those tags on each *_Name whose *_Desc qualifies (and its _short).
Loc bareTypeTags(const Loc &loc, const tags::TagConfig *componentsConfig);
// Display name -> bare-type tag, for blueprint lists.
QHash<QString, QString> bareTypeNameTagLookup(const Loc &loc, const tags::TagConfig *componentsConfig);

// The default config for a Tag Builder category (process-wide).
const tags::TagConfig &defaultTagConfig(const QString &category);

} // namespace core::enh
