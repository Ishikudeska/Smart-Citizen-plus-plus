#include "core/model/Enhancements.h"

#include "core/model/StringEntry.h"

#include <array>

namespace core::enhancements {

namespace {

constexpr std::array<File, 9> kFiles = {{
    {"ship_descs", "ships_desc_enhancements.ini"},
    {"component_descs", "components_desc_enhancements.ini"},
    {"ship_weapon_descs", "ship_weapons_desc_enhancements.ini"},
    {"fps_weapon_descs", "fps_weapons_desc_enhancements.ini"},
    {"mission_rewards", "mission_rewards_enhancements.ini"},
    {"commodity_crafting", "commodity_crafting_enhancements.ini"},
    {"journal", "journal_enhancements.ini"},
    {"missile_enhancements", "missile_enhancements.ini"},
    {"medical_consumables", "medical_consumables_enhancements.ini"},
}};

const std::array<Category, 7> &categoryTable()
{
    static const std::array<Category, 7> table = {{
        {"ships", category::kShips, {QStringLiteral("ship_descs")}},
        {"ship_items",
         category::kShipItems,
         {QStringLiteral("component_descs"), QStringLiteral("ship_weapon_descs"),
          QStringLiteral("missile_enhancements")}},
        {"gear", category::kGear, {QStringLiteral("fps_weapon_descs")}},
        {"missions", category::kMissions, {QStringLiteral("mission_rewards")}},
        {"commodities", category::kCommodities, {QStringLiteral("commodity_crafting")}},
        {"journal", category::kJournal, {QStringLiteral("journal")}},
        {"medical_consumables", category::kMedicalConsumables, {QStringLiteral("medical_consumables")}},
    }};
    return table;
}

} // namespace

std::span<const File> files()
{
    return kFiles;
}

std::span<const Category> categories()
{
    return categoryTable();
}

QString categoryLabelForFile(const QString &fileId)
{
    for (const Category &c : categoryTable())
        if (c.fileIds.contains(fileId))
            return c.label;
    return {};
}

QString fileNameFor(const QString &fileId)
{
    for (const File &f : kFiles)
        if (fileId == QLatin1StringView(f.id))
            return QString::fromLatin1(f.fileName);
    return {};
}

} // namespace core::enhancements
