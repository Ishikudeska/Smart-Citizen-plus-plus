// game_data.json export (port of sc.gamedata): the .NET behaviours it leans
// on, the JSON writer's exact layout, and the passes end to end over a small
// set of synthetic records.

#include "engine/gamedata/DotNet.h"
#include "engine/gamedata/GameData.h"
#include "engine/gamedata/Json.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <pugixml.hpp>

#include <cmath>

using namespace engine;
using namespace engine::gamedata;

namespace {

// Records as (path, XML) pairs; built through pugixml into the XmlTree the
// extractor walks.
RecordSource records(std::vector<std::pair<std::string, std::string>> items)
{
    RecordSource source;
    source.dcbVersion = 8;
    auto xmls = std::make_shared<std::vector<std::string>>();
    for (auto &[path, text] : items) {
        source.paths.push_back(path);
        xmls->push_back(text);
    }
    source.build = [xmls](std::size_t i, xml::XmlTree &tree) {
        tree.clear();
        pugi::xml_document doc;
        if (!doc.load_string((*xmls)[i].c_str()))
            qFatal("bad test XML");
        return tree.copyFrom(doc.document_element());
    };
    return source;
}

const char *kRecords = "libs/foundry/records/";

std::string path(const char *rest)
{
    return std::string(kRecords) + rest;
}

} // namespace

class TestGameData : public QObject
{
    Q_OBJECT

private slots:
    void parsesDoublesLikeDotNet()
    {
        QCOMPARE(dotnet::parseDouble("1.5").value(), 1.5);
        QCOMPARE(dotnet::parseDouble(" -2E-05 ").value(), -2e-05);
        QCOMPARE(dotnet::parseDouble("+.5").value(), 0.5);
        QCOMPARE(dotnet::parseDouble("5.").value(), 5.0);
        QVERIFY(std::isinf(dotnet::parseDouble("-Infinity").value()));
        QVERIFY(!dotnet::parseDouble("1,5"));
        QVERIFY(!dotnet::parseDouble("0x10"));
        QVERIFY(!dotnet::parseDouble(""));
        QVERIFY(!dotnet::parseDouble("e5"));
        QCOMPARE(dotnet::parseInt32(" 42 ").value(), 42);
        QCOMPARE(dotnet::parseInt32("-2147483648").value(), std::numeric_limits<std::int32_t>::min());
        QVERIFY(!dotnet::parseInt32("2147483648"));
        QVERIFY(!dotnet::parseInt32("1.0"));
    }

    void roundsHalfToEven()
    {
        QCOMPARE(dotnet::round(2.5, 0), 2.0);
        QCOMPARE(dotnet::round(3.5, 0), 4.0);
        QCOMPARE(dotnet::round(0.125, 2), 0.12);
        QCOMPARE(dotnet::round(291.66666666666669, 1), 291.7);
        QCOMPARE(dotnet::round(-1.25, 1), -1.2);
    }

    void trimsDotNetWhiteSpace()
    {
        QCOMPARE(dotnet::trim("\t a b\xC2\xA0 "), std::string_view("a b"));
        QCOMPARE(dotnet::trimEnd(" x \xE3\x80\x80"), std::string_view(" x"));
    }

    void ordersLikeTheInvariantCulture()
    {
        QVERIFY(dotnet::compareCulture("apple", "Banana") < 0); // case-insensitive first
        QVERIFY(dotnet::compareCulture("a", "A") < 0);          // then lower case first
        QVERIFY(dotnet::compareCulture("'Arrow'", "9-Series") < 0);
        QVERIFY(dotnet::compareCulture("Gladius", "Gladius") == 0);
        QVERIFY(dotnet::compareOrdinal("B", "a") < 0);
#ifdef _WIN32
        // CLDR root puts '_' before '-' (ASCII has them the other way round):
        // proof the system ICU is in use, as it is for .NET.
        QVERIFY(dotnet::compareCulture("_a", "-a") < 0);
#endif
    }

    void introSortMatchesInsertionSortWhenSmall()
    {
        // Up to 16 items List.Sort is an insertion sort, so stable.
        std::vector<std::pair<int, int>> items = {{2, 0}, {1, 1}, {2, 2}, {1, 3}, {0, 4}, {2, 5}};
        dotnet::introSort(items, [](const auto &a, const auto &b) { return a.first - b.first; });
        const std::vector<std::pair<int, int>> expected = {{0, 4}, {1, 1}, {1, 3}, {2, 0}, {2, 2}, {2, 5}};
        QVERIFY(items == expected);

        std::vector<int> many;
        for (int i = 0; i < 1000; ++i)
            many.push_back((i * 7919) % 101);
        dotnet::introSort(many, [](int a, int b) { return a < b ? -1 : a > b ? 1 : 0; });
        QVERIFY(std::is_sorted(many.begin(), many.end()));
    }

    void writesSystemTextJsonLayout()
    {
        json::Writer w;
        w.beginObject();
        w.field("a", std::int32_t{1});
        w.field("b", 0.1);
        w.field("none", std::optional<double>());
        w.key("list");
        w.beginArray();
        w.value("x\"\\\n\x01<&>");
        w.null();
        w.endArray();
        w.key("empty");
        w.beginArray();
        w.endArray();
        w.key("o");
        w.beginObject();
        w.field("big", 1e17);
        w.field("small", 1e-05);
        w.field("u", "caf\xC3\xA9 \xF0\x9F\x98\x80");
        w.endObject();
        w.endObject();
        const std::string expected = "{\r\n"
                                     "  \"a\": 1,\r\n"
                                     "  \"b\": 0.1,\r\n"
                                     "  \"list\": [\r\n"
                                     "    \"x\\\"\\\\\\n\\u0001<&>\",\r\n"
                                     "    null\r\n"
                                     "  ],\r\n"
                                     "  \"empty\": [],\r\n"
                                     "  \"o\": {\r\n"
                                     "    \"big\": 1E+17,\r\n"
                                     "    \"small\": 1E-05,\r\n"
                                     "    \"u\": \"caf\xC3\xA9 \\uD83D\\uDE00\"\r\n"
                                     "  }\r\n"
                                     "}";
        QCOMPARE(QString::fromStdString(w.text()), QString::fromStdString(expected));

        const auto parsed = json::parse(w.text());
        QVERIFY(parsed);
        QCOMPARE(parsed->find("o")->find("u")->string, std::string("caf\xC3\xA9 \xF0\x9F\x98\x80"));
        QCOMPARE(parsed->find("list")->array.front().string, std::string("x\"\\\n\x01<&>"));
    }

    void extractsEndToEnd()
    {
        const std::string ammoGuid = "aaaaaaaa-0000-0000-0000-000000000001";
        const std::string rackGuid = "cccccccc-0000-0000-0000-000000000003";
        const RecordSource source = records({
            {path("ammoparams/vehicle/ammo_laser.xml"),
             R"(<AmmoParams.Laser speed="1200" lifetime="1.67" __ref=")" + ammoGuid +
                 R"("><projectileParams><BulletProjectileParams><damage><DamageInfo DamageEnergy="50"/></damage>
                 <penetrationParams basePenetrationDistance="0.175"/></BulletProjectileParams></projectileParams>
                 </AmmoParams.Laser>)"},
            {path("entities/scitem/ships/weapons/test_laser_s1.xml"),
             R"(<EntityClassDefinition.TEST_LaserCannon_S1 __ref="w1"><Components>
                 <SAttachableComponentParams><AttachDef Size="1"><Localization Name="@item_NameTEST_LaserCannon_S1"/></AttachDef></SAttachableComponentParams>
                 <SAmmoContainerComponentParams ammoParamsRecord=")" +
                 ammoGuid.substr(0, 8) + "-0000-0000-0000-000000000001" + R"(" maxAmmoCount="0"/>
                 <SCItemWeaponComponentParams><fireActions><SWeaponActionFireSingleParams fireRate="350" heatPerShot="0.5"/></fireActions></SCItemWeaponComponentParams>
                 <SWeaponSimplifiedHeatParams overheatTemperature="10" coolingPerSecond="2.5" timeTillCoolingStarts="0.25" overheatFixTime="3"/>
                 </Components></EntityClassDefinition.TEST_LaserCannon_S1>)"},
            {path("entities/scitem/ships/weapons/parts/not_a_weapon.xml"), "<EntityClassDefinition.Part/>"},
            {path("entities/scitem/ships/weapons/missiles/misl_s02_ir_test.xml"),
             R"(<EntityClassDefinition.MISL_S02_IR_TEST_Spark __ref="m1"><Components>
                 <SAttachableComponentParams><AttachDef Size="2" SubType="Missile"><Localization Name="@nope"/></AttachDef></SAttachableComponentParams>
                 <SCItemMissileParams armTime="0.5" maxLifetime="15"><targetingParams lockTime="2.25" lockRangeMin="500" lockRangeMax="8000"/>
                 <GCSParams linearSpeed="1400.4"/><explosionParams><damage><DamageInfo DamagePhysical="1200"/></damage></explosionParams></SCItemMissileParams>
                 <SHealthComponentParams Health="25"/></Components></EntityClassDefinition.MISL_S02_IR_TEST_Spark>)"},
            {path("entities/scitem/ships/armor/armr_test_ship.xml"),
             R"(<EntityClassDefinition.ARMR_TEST_Ship __ref="a1"><Components>
                 <SAttachableComponentParams><AttachDef><Localization Name="@item_NameARMR_TEST_Ship"/></AttachDef></SAttachableComponentParams>
                 <SHealthComponentParams Health="3300"><DamageResistance><PhysicalResistance Multiplier="0.8"/></DamageResistance></SHealthComponentParams>
                 <SCItemVehicleArmorParams><armorDeflection><deflectionValue DamagePhysical="11"/></armorDeflection>
                 <damageMultiplier><DamageInfo DamagePhysical="0.75" DamageEnergy="1"/></damageMultiplier></SCItemVehicleArmorParams>
                 </Components></EntityClassDefinition.ARMR_TEST_Ship>)"},
            {path("entities/scitem/ships/controller/controller_flight_test.xml"),
             R"(<EntityClassDefinition.Controller_Flight_TEST_Ship><Components>
                 <SAttachableComponentParams><AttachDef Mass="100"/></SAttachableComponentParams>
                 <IFCSParams scmSpeed="200" boostSpeedForward="500" maxSpeed="1200"><speedProfile><angularVelocity x="60" y="50" z="100"/></speedProfile>
                 <afterburnerNew><afterburnAngVelocityMultiplier x="1.5" y="1" z="1"/><afterburnAccelMultiplierPositive y="2"/></afterburnerNew></IFCSParams>
                 </Components></EntityClassDefinition.Controller_Flight_TEST_Ship>)"},
            {path("entities/scitem/ships/thrusters/thrm_test.xml"),
             R"(<EntityClassDefinition.THRM_TEST><Components><SAttachableComponentParams><AttachDef Mass="50"/></SAttachableComponentParams>
                 <SCItemThrusterParams thrustCapacity="1000000" thrusterType="Main"/></Components></EntityClassDefinition.THRM_TEST>)"},
            {path("entities/scitem/ships/thrusters/thrv_test.xml"),
             R"(<EntityClassDefinition.THRV_TEST><Components>
                 <SCItemThrusterParams thrustCapacity="250000" thrusterType="Maneuver" onlyActiveInVTOL="1"/></Components></EntityClassDefinition.THRV_TEST>)"},
            {path("entities/scitem/ships/missile_racks/mrck_s02_test_dual.xml"),
             R"(<EntityClassDefinition.MRCK_S02_TEST_Dual __ref=")" + rackGuid + R"("><Components>
                 <SAttachableComponentParams><AttachDef Type="MissileLauncher" SubType="MissileRack" Size="2"/></SAttachableComponentParams>
                 <SItemPortContainerComponentParams><Ports>
                 <SItemPortDef MinSize="2" MaxSize="2"><Types><SItemPortDefTypes Type="Missile"/></Types></SItemPortDef>
                 <SItemPortDef MinSize="2" MaxSize="2"><Types><SItemPortDefTypes Type="Missile"/></Types></SItemPortDef>
                 </Ports></SItemPortContainerComponentParams></Components></EntityClassDefinition.MRCK_S02_TEST_Dual>)"},
            {path("entities/spaceships/test_ship.xml"),
             R"(<EntityClassDefinition.TEST_Ship><Components>
                 <SAttachableComponentParams><AttachDef Size="2"><Localization Name="@vehicle_NameTEST_Ship"/></AttachDef></SAttachableComponentParams>
                 <VehicleComponentParams crewSize="1" vehicleCareer="@career" vehicleRole="@role"><maxBoundingBoxSize x="17.5" y="21.004" z="5.5"/></VehicleComponentParams>
                 <SEntityComponentDefaultLoadoutParams><loadout><SItemPortLoadoutManualParams><entries>
                   <SItemPortLoadoutEntryParams itemPortName="hardpoint_armor" entityClassName="ARMR_TEST_Ship"/>
                   <SItemPortLoadoutEntryParams itemPortName="hardpoint_controller_flight" entityClassName="Controller_Flight_TEST_Ship"/>
                   <SItemPortLoadoutEntryParams itemPortName="hardpoint_thruster_main" entityClassName="THRM_TEST"/>
                   <SItemPortLoadoutEntryParams itemPortName="hardpoint_thruster_vtol" entityClassName="THRV_TEST"/>
                   <SItemPortLoadoutEntryParams itemPortName="hardpoint_weapon_gun_class1_nose" entityClassReference="w1"/>
                   <SItemPortLoadoutEntryParams itemPortName="hardpoint_missilerack_left" entityClassReference=")" +
                 rackGuid + R"(">
                     <loadout><SItemPortLoadoutManualParams><entries>
                       <SItemPortLoadoutEntryParams itemPortName="missile_01_attach" entityClassName="MISL_S02_IR_TEST_Spark"/>
                       <SItemPortLoadoutEntryParams itemPortName="missile_02_attach" entityClassName="MISL_S02_IR_TEST_Spark"/>
                     </entries></SItemPortLoadoutManualParams></loadout>
                   </SItemPortLoadoutEntryParams>
                 </entries></SItemPortLoadoutManualParams></loadout></SEntityComponentDefaultLoadoutParams>
                 <Vehicle><Parts><Part mass="1000"><Parts><Part mass="250"/></Parts></Part></Parts></Vehicle>
                 </Components></EntityClassDefinition.TEST_Ship>)"},
            {path("entities/spaceships/test_ship_pu_pirate.xml"),
             "<EntityClassDefinition.TEST_Ship_PU_Pirate/>"},
        });

        QTemporaryDir dir;
        const QString ini = dir.filePath(QStringLiteral("base.ini"));
        {
            QFile f(ini);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("\xEF\xBB\xBFitem_NameTEST_LaserCannon_S1=Test Repeater\r\n"
                    "item_NameARMR_TEST_Ship=Test Ship Armor\r\n"
                    "vehicle_NameTEST_Ship=Testco Test Ship\r\n"
                    "career=Combat\nrole=Light Fighter\n");
        }
        Options options;
        options.baseIniPath = ini.toStdString();
        options.generatedAt = "2026-01-01T00:00:00.0000000Z";
        options.channel = "PTU";
        std::vector<std::string> log;
        const auto text = buildGameDataJson(source, options, [&](const std::string &l) { log.push_back(l); });
        QVERIFY(text);
        QVERIFY(text->ends_with("}\r\n"));
        QVERIFY(std::find(log.begin(), log.end(), "Weapons: 2 (1 guns + 1 missiles)") != log.end());

        const QJsonObject root = QJsonDocument::fromJson(QByteArray::fromStdString(*text)).object();
        QCOMPARE(root.keys().size(), 8);
        QCOMPARE(root[u"channel"].toString(), QStringLiteral("PTU"));
        QCOMPARE(root[u"dcb_version"].toInt(), 8);

        const QJsonArray weapons = root[u"weapons"].toArray();
        QCOMPARE(weapons.size(), 2);
        const QJsonObject gun = weapons[0].toObject();
        QCOMPARE(gun[u"name"].toString(), QStringLiteral("Test Repeater"));
        QCOMPARE(gun[u"range"].toDouble(), 2004.0);
        QCOMPARE(gun[u"dps_burst"].toDouble(), 291.7);
        QCOMPARE(gun[u"base_penetration_distance"].toDouble(), 0.18); // 0.175 * 100 is exactly 17.5: to even
        QCOMPARE(gun[u"shots_to_overheat"].toInt(), 20);
        QCOMPARE(gun[u"time_to_overheat"].toDouble(), 3.429);
        QVERIFY(!gun.contains(u"magazine_capacity"));
        QVERIFY(!gun.contains(u"pellet_count"));
        const QJsonObject missile = weapons[1].toObject();
        QCOMPARE(missile[u"name"].toString(), QStringLiteral("S02 IR TEST Spark"));
        QCOMPARE(missile[u"range"].toDouble(), 21006.0);
        QCOMPARE(missile[u"projectile_velocity"].toDouble(), 1400.0);
        QCOMPARE(missile[u"lock_time"].toDouble(), 2.25);
        QCOMPARE(missile[u"health"].toDouble(), 25.0);

        const QJsonObject armor = root[u"ships"].toArray()[0].toObject();
        QCOMPARE(armor[u"name"].toString(), QStringLiteral("Test"));
        QCOMPARE(armor[u"resistance"].toObject()[u"phys"].toDouble(), 0.8);
        QCOMPARE(armor[u"resistance"].toObject()[u"energy"].toDouble(), 1.0);

        const QJsonArray vehicles = root[u"vehicles"].toArray();
        QCOMPARE(vehicles.size(), 1); // the _PU_Pirate variant is dropped
        const QJsonObject ship = vehicles[0].toObject();
        QCOMPARE(ship[u"name"].toString(), QStringLiteral("Test Ship")); // "Testco" isn't in the id
        QCOMPARE(ship[u"armor_id"].toString(), QStringLiteral("ARMR_TEST_Ship"));
        QCOMPARE(ship[u"career"].toString(), QStringLiteral("Combat"));
        QCOMPARE(ship[u"length"].toDouble(), 21.0);
        QCOMPARE(ship[u"size_class"].toInt(), 2);
        QCOMPARE(ship[u"mass"].toDouble(), 1250.0);
        QCOMPARE(ship[u"mass_loadout"].toDouble(), 150.0);
        QCOMPARE(ship[u"mass_total"].toDouble(), 1400.0);
        QCOMPARE(ship[u"hull_hp"].toDouble(), 3300.0);
        QCOMPARE(ship[u"agility"].toObject()[u"pitch_boosted"].toDouble(), 90.0);
        QCOMPARE(ship[u"thrust_capacity"].toObject()[u"vtol"].toDouble(), 250000.0);
        QCOMPARE(ship[u"acceleration"].toObject()[u"main_boosted"].toDouble(), 1000000.0 / 1400 * 2);

        const QJsonArray slotList = ship[u"slots"].toArray();
        QCOMPARE(slotList.size(), 2);
        const QJsonObject rack = slotList[0].toObject(); // size 2 sorts first
        QCOMPARE(rack[u"kind"].toString(), QStringLiteral("missile_rack"));
        QCOMPARE(rack[u"label"].toString(), QStringLiteral("missilerack left"));
        QCOMPARE(rack[u"stock_rack_id"].toString(), QStringLiteral("MRCK_S02_TEST_Dual")); // resolved by guid
        QCOMPARE(rack[u"stock_missile_ids"].toArray().size(), 2);
        const QJsonObject gunSlot = slotList[1].toObject();
        QCOMPARE(gunSlot[u"label"].toString(), QStringLiteral("nose"));
        QCOMPARE(gunSlot[u"size"].toInt(), 1);
        QCOMPARE(gunSlot[u"stock_weapon_id"].toString(), QStringLiteral("TEST_LaserCannon_S1"));

        const QJsonObject rackRecord = root[u"racks"].toArray()[0].toObject();
        QCOMPARE(rackRecord[u"missile_count"].toInt(), 2);
        QCOMPARE(rackRecord[u"manufacturer"].toString(), QStringLiteral("TEST"));

        // An overlay only fills what extraction left null.
        const QString overlay = dir.filePath(QStringLiteral("overlay.json"));
        {
            QFile f(overlay);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"vehicles": [{"id": "TEST_Ship", "mass": 1, "scm_speed": 999, "nav_speed": null}]})");
        }
        options.overlayPath = overlay.toStdString();
        const auto again = buildGameDataJson(source, options);
        QVERIFY(again);
        const QJsonObject ship2 = QJsonDocument::fromJson(QByteArray::fromStdString(*again))
                                      .object()[u"vehicles"]
                                      .toArray()[0]
                                      .toObject();
        QCOMPARE(ship2[u"mass"].toDouble(), 1250.0);
        QCOMPARE(ship2[u"scm_speed"].toDouble(), 200.0);
    }
};

QTEST_GUILESS_MAIN(TestGameData)
#include "tst_gamedata.moc"
