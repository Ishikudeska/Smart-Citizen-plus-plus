#include "core/model/StringEntry.h"

#include <QHash>
#include <QMutex>
#include <QRegularExpression>

#include <array>

namespace core {

namespace {

constexpr std::array kFpsWeaponWords = {u"_rifle_", u"_pistol_", u"_smg_",  u"_shotgun_", u"_sniper_",  u"_launcher_",
                                        u"_lmg_",   u"_hmg_",    u"_knife_", u"_multi_",  u"_crossbow_"};
constexpr std::array kArmorGearWords = {u"armor", u"helmet", u"suit", u"vest", u"glasses", u"_optics_", u"_barrel_"};
constexpr std::array kComponentCodes = {u"shld", u"powr", u"cool", u"qdrv", u"jump", u"misl", u"gmisl", u"bomb"};

// Mission-source prefixes, compared against the lower-cased key.
constexpr std::array kMissionPrefixes = {
    u"adagio_", u"assassin", u"basesweep_", u"bbt_", u"bhg_", u"bitzero", u"blackbox", u"blacjac",
    u"blockaderunner", u"bounty_", u"cdf_", u"cfp", u"civilian_", u"claimsweep_", u"cleanair_", u"clovis_",
    u"combatassist_", u"commarray", u"confirmkill_", u"constantine_", u"contract", u"covalex", u"criminal_",
    u"crusader_", u"crus_", u"dataheis", u"deadsaints_", u"deploypiggyback_", u"deployprobe_", u"destroyblade_",
    u"destroydebris_", u"destroyitem_", u"destroyprobe", u"destroystash_", u"distraction", u"dusters_", u"eckhart",
    u"ecn_", u"escort_", u"fffinale_", u"firesale_", u"forcedepletion", u"foxwell_", u"fps_bounty", u"ftl_",
    u"genlocal_", u"gobling_", u"groupbounty_", u"hack_", u"haulcargo_", u"hdactivist_", u"headhunters_",
    u"hexpenetrator_", u"hh_", u"highpoint_", u"hockcrow_", u"hockrow_", u"hurston_", u"intersec_", u"jt_",
    u"kaboos_", u"kareahsweep_", u"killship_", u"lingfamily_", u"localdelivery_", u"locationrush_", u"me_blackbox",
    u"me_bounty", u"me_planetcollect", u"meet_", u"mg_", u"mgclovus_", u"miningclaim", u"mission", u"mtps_",
    u"murderspree_", u"ninetails_", u"northrock_", u"ntlockdown_", u"outpost_repair", u"outlawsweep_",
    u"p_showdown", u"p_protect", u"planetcollect_", u"preventdata_", u"prisonerbreak_", u"protlife_", u"rain_",
    u"recovery_", u"recoverstash_", u"recoverstolen_", u"redwind_", u"repairoxygenkiosk_", u"retakelocation_",
    u"retrieveconsignment_", u"retrievedatapad_", u"roughready_", u"ruto_", u"scramblerace_", u"searchbody",
    u"searchcrew_", u"sectorsweep_", u"securitypatrol_", u"servicebeacon_", u"shubin_", u"singleidrisfight_",
    u"spacecargo_", u"spacecollect", u"spacesteal_", u"stealevidence_", u"stealitem_", u"tarpits_", u"test_title_",
    u"thecollector_", u"timesensitive_", u"tutorial", u"udm_", u"uwc_", u"vaughn_", u"vendingmachine_",
    u"wantedlevel", u"wstr_", u"xenothreat_",
};

template <std::size_t N>
bool startsWithAny(QStringView s, const std::array<const char16_t *, N> &prefixes)
{
    for (const char16_t *p : prefixes)
        if (s.startsWith(QStringView(p)))
            return true;
    return false;
}

template <std::size_t N>
bool containsAny(QStringView s, const std::array<const char16_t *, N> &words)
{
    for (const char16_t *w : words)
        if (s.contains(QStringView(w)))
            return true;
    return false;
}

// item_name / item_desc, optionally "_", then a ship component code and "_".
bool hasComponentPrefix(QStringView lower)
{
    for (QStringView field : {QStringView(u"item_name"), QStringView(u"item_desc")}) {
        if (!lower.startsWith(field))
            continue;
        for (QStringView rest : {lower.sliced(field.size()),
                                 lower.size() > field.size() && lower[field.size()] == u'_'
                                     ? lower.sliced(field.size() + 1)
                                     : QStringView()}) {
            for (const char16_t *code : kComponentCodes) {
                const QStringView c(code);
                if (rest.size() > c.size() && rest.startsWith(c) && rest[c.size()] == u'_')
                    return true;
            }
        }
    }
    return false;
}

QString computeCategory(const QString &key)
{
    if (key.isEmpty())
        return category::kOther;
    const QString lower = key.toLower();
    const QStringView l(lower);

    if (l.startsWith(u"vehicle_name") || l.startsWith(u"vehicle_desc"))
        return category::kShips;
    // Wikelo ship mods (TheCollector_ShipMod_*_VehicleName and friends).
    if (l.endsWith(u"_vehiclename") || l.endsWith(u"_vehicledesc") || l.endsWith(u"_vehiclenameshort"))
        return category::kShips;

    if (l.startsWith(u"item_name") || l.startsWith(u"item_desc")) {
        if (l.startsWith(u"item_name_turret") || l.startsWith(u"item_desc_turret"))
            return category::kShipItems;
        if (hasComponentPrefix(l))
            return category::kShipItems;
        if (containsAny(l, kFpsWeaponWords) || containsAny(l, kArmorGearWords))
            return category::kGear;
        // The case of what follows item_Name/item_Desc tells the domain:
        // uppercase is a ship item (item_NameBEHR_...), lowercase FPS gear.
        const QStringView after = QStringView(key).sliced(9);
        if (!after.isEmpty() && after[0] != u'_')
            return after[0].isUpper() ? category::kShipItems : category::kGear;
        static const QRegularExpression shipWeaponSize(QStringLiteral(R"(_S\d+|_X{1,2}L(-\d+)?|_[LMS]-\d+)"),
                                                       QRegularExpression::CaseInsensitiveOption);
        if (shipWeaponSize.match(key).hasMatch())
            return category::kShipItems;
        if (l.startsWith(u"item_name_") || l.startsWith(u"item_desc_"))
            return category::kGear;
    }

    if (l.startsWith(u"item_mining_gadget_"))
        return category::kGear;
    if (l.startsWith(u"item_mining_"))
        return category::kShipItems;
    if (l.startsWith(u"items_commodities_"))
        return category::kCommodities;
    if (l.contains(u"journal"))
        return category::kJournal;
    if (startsWithAny(l, kMissionPrefixes))
        return category::kMissions;
    return category::kOther;
}

} // namespace

QString statusName(EntryStatus status)
{
    switch (status) {
    case EntryStatus::Unmodified: return QStringLiteral("Unmodified");
    case EntryStatus::Modified: return QStringLiteral("Modified");
    case EntryStatus::Enhanced: return QStringLiteral("Enhanced");
    case EntryStatus::New: return QStringLiteral("New");
    }
    return {};
}

QString extractCategory(const QString &key)
{
    static QMutex mutex;
    static QHash<QString, QString> cache;
    {
        QMutexLocker lock(&mutex);
        if (const auto it = cache.constFind(key); it != cache.cend())
            return *it;
    }
    QString result = computeCategory(key);
    QMutexLocker lock(&mutex);
    cache.insert(key, result);
    return result;
}

bool isShipNameKey(QStringView key)
{
    if (key.isEmpty())
        return false;
    const QString lower = key.toString().toLower();
    return lower.startsWith(u"vehicle_name") || lower.endsWith(u"_vehiclename") ||
           lower.endsWith(u"_vehiclenameshort");
}

} // namespace core
