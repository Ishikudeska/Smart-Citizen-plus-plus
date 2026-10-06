// The ship loadout catalog over a small DataForge cache and vehicle XMLs
// written by the test: a ship with variants, the items it carries, and what
// the loadout adds up to.

#include "core/loadout/Catalog.h"
#include "core/loadout/Loadout.h"
#include "core/loadout/Performance.h"
#include "core/text/IniFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace core;
using namespace core::loadout;

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot open %s", qPrintable(path));
    f.write(bytes);
}

QByteArray guid(int n)
{
    return QStringLiteral("00000000-0000-0000-0000-%1").arg(n, 12, 10, QLatin1Char('0')).toUtf8();
}

const QByteArray kAcme = guid(900);

// An item record: AttachDef, then `components`.
QByteArray item(const char *id, int ref, const char *type, const char *subType, int size,
                const QByteArray &components, const char *attachExtra = "")
{
    return "<EntityClassDefinition." + QByteArray(id) + " __type=\"EntityClassDefinition\" __ref=\"" +
           guid(ref) + "\">\n  <Components>\n    <SAttachableComponentParams>\n      <AttachDef Type=\"" +
           type + "\" SubType=\"" + subType + "\" Size=\"" + QByteArray::number(size) +
           "\" Grade=\"3\" Manufacturer=\"" + kAcme + "\" " + attachExtra +
           ">\n        <Localization Name=\"@name_" + id +
           "\" ShortName=\"@LOC_EMPTY\" Description=\"@desc_" + id +
           "\" />\n      </AttachDef>\n    </SAttachableComponentParams>\n    "
           "<SEntityPhysicsControllerParams><PhysType><SEntityRigidPhysicsControllerParams Mass=\"100\" />"
           "</PhysType></SEntityPhysicsControllerParams>\n" +
           components + "\n  </Components>\n</EntityClassDefinition." + id + ">\n";
}

QByteArray amount(const char *resource, const char *unit, double value)
{
    static const QHash<QByteArray, QByteArray> attrs = {{"SPowerSegmentResourceUnit", "units"},
                                                        {"SStandardResourceUnit", "standardResourceUnits"},
                                                        {"SMicroResourceUnit", "microResourceUnits"}};
    return QByteArray("resource=\"") + resource + "\"><resourceAmountPerSecond><" + unit + ' ' +
           attrs.value(unit) + "=\"" + QByteArray::number(value) + "\" /></resourceAmountPerSecond>";
}

QByteArray consume(const char *resource, const char *unit, double value, double minFraction = 0)
{
    return "<ItemResourceDeltaConsumption minimumConsumptionFraction=\"" + QByteArray::number(minFraction) +
           "\"><consumption " + amount(resource, unit, value) +
           "</consumption></ItemResourceDeltaConsumption>";
}

QByteArray state(const char *name, const QByteArray &deltas, double em = 0, double ir = 0,
                 const QByteArray &ranges = {})
{
    return "<ItemResourceState name=\"" + QByteArray(name) + "\"><deltas>" + deltas +
           "</deltas><signatureParams><EMSignature nominalSignature=\"" + QByteArray::number(em) +
           "\" /><IRSignature nominalSignature=\"" + QByteArray::number(ir) +
           "\" /></signatureParams><powerRanges>" + ranges + "</powerRanges></ItemResourceState>";
}

QByteArray resources(const QByteArray &states)
{
    return "<ItemResourceComponentParams><states>" + states + "</states></ItemResourceComponentParams>";
}

QByteArray entries(const QByteArray &body)
{
    return "<loadout><SItemPortLoadoutManualParams><entries>" + body +
           "</entries></SItemPortLoadoutManualParams></loadout>";
}

QByteArray entry(const char *port, const char *name, const QByteArray &ref = {},
                 const QByteArray &children = {})
{
    QByteArray out = "<SItemPortLoadoutEntryParams itemPortName=\"" + QByteArray(port) +
                     "\" entityClassName=\"" + name + "\"";
    if (!ref.isEmpty())
        out += " entityClassReference=\"" + ref + "\"";
    return children.isEmpty() ? out + " />"
                              : out + ">" + entries(children) + "</SItemPortLoadoutEntryParams>";
}

QByteArray portDef(const char *name, int min, int max, const char *type, const char *subType = nullptr,
                   const char *flags = "editable")
{
    return "<SItemPortDef Name=\"" + QByteArray(name) + "\" DisplayName=\"@LOC_EMPTY\" Flags=\"" + flags +
           "\" MinSize=\"" + QByteArray::number(min) + "\" MaxSize=\"" + QByteArray::number(max) +
           "\"><Types><SItemPortDefTypes Type=\"" + type + "\">" +
           (subType ? "<SubTypes><Enum value=\"" + QByteArray(subType) + "\" /></SubTypes>" : QByteArray()) +
           "</SItemPortDefTypes></Types></SItemPortDef>";
}

QByteArray ports(const QByteArray &defs, const char *extraAttrs = "")
{
    return "<SItemPortContainerComponentParams " + QByteArray(extraAttrs) + "><Ports>" + defs +
           "</Ports></SItemPortContainerComponentParams>";
}

QByteArray ammo(const char *id, int ref, const char *damage)
{
    return "<AmmoParams." + QByteArray(id) + " speed=\"1000\" lifetime=\"2\" __type=\"AmmoParams\" __ref=\"" +
           guid(ref) + "\"><projectileParams><BulletProjectileParams><damage><DamageInfo " + damage +
           " /></damage><penetrationParams basePenetrationDistance=\"3\" /></BulletProjectileParams>"
           "</projectileParams></AmmoParams." +
           id + ">\n";
}

QByteArray itemPort(const char *name, int min, int max, const QByteArray &types, const char *flags = "",
                    const char *id = nullptr)
{
    return "<Part name=\"" + QByteArray(name) + "\" class=\"ItemPort\"" +
           (id ? " id=\"" + QByteArray(id) + "\"" : QByteArray()) + "><ItemPort minSize=\"" +
           QByteArray::number(min) + "\" maxSize=\"" + QByteArray::number(max) + "\" flags=\"" + flags +
           "\"><Types>" + types + "</Types></ItemPort></Part>";
}

QByteArray type(const char *t, const char *subtypes = nullptr)
{
    return "<Type type=\"" + QByteArray(t) + "\"" +
           (subtypes ? " subtypes=\"" + QByteArray(subtypes) + "\"" : "") + " />";
}

const char *const kLoc = R"x(name_GIMBAL_S3=Swivel S3 Gimbal
name_GUN_S3=Pew Gun
desc_GUN_S3=Manufacturer: Acme\nItem Type: Gun\nClass: Military\n\nIt shoots.
name_GUN_S2=Small Pew Gun
name_LASER_S3=Zap Laser
name_RACK_S3=Two Pack Rack
name_MISSILE_S2=Boom Missile
name_MISSILE_B_S2=Bigger Boom Missile
name_POWER_S1=Spark Plant
name_COOLER_S1=Chill Cooler
desc_COOLER_S1=Item Type: Cooler\nManufacturer: Acme\nSize: 1\nGrade: C\nClass: Civilian\n\nKeeps it cool.
name_SHIELD_S1=Bubble Shield
name_QD_S1=Zoom Drive
name_FC=Test Flight Blade
name_ARMOR=Test Armor
name_LIFE_S1=Breathe Easy
mfr_acme=Acme Industries
ship_test=Acme Dart
ship_test_desc=Manufacturer: Acme Industries\nFocus: Fighter\n\nA test ship.
ship_variant=Acme Dart Variant
ship_rebuilt=Acme Dart Rebuilt
career_combat=Combat
role_fighter=Light Fighter
)x";

QByteArray shipRecord(const char *id, int ref, const char *name, const char *modification,
                      const QByteArray &loadout)
{
    return "<EntityClassDefinition." + QByteArray(id) + " __type=\"EntityClassDefinition\" __ref=\"" +
           guid(ref) +
           "\">\n  <Components>\n    <VehicleComponentParams "
           "vehicleDefinition=\"scripts/entities/vehicles/implementations/xml/acme_dart.xml\" "
           "modification=\"" +
           modification + "\" manufacturer=\"" + kAcme + "\" crewSize=\"2\" vehicleName=\"@" + name +
           "\" vehicleDescription=\"@ship_test_desc\" vehicleCareer=\"@career_combat\" "
           "vehicleRole=\"@role_fighter\">\n      <maxBoundingBoxSize x=\"10\" y=\"20\" z=\"5\" />\n"
           "    </VehicleComponentParams>\n    <SEntityComponentDefaultLoadoutParams>" +
           loadout + "</SEntityComponentDefaultLoadoutParams>\n    " +
           ports(portDef("hardpoint_lifesupport", 1, 1, "LifeSupportGenerator", nullptr, ""),
                 "PortTags=\"ACME_Record\"") +
           "\n    <SomeOtherParams><resourceNetworkPowerPools><itemPools>"
           "<FixedPowerPool itemType=\"WeaponGun\" poolSize=\"4\" />"
           "<DynamicPowerPool itemType=\"Shield\" maxItemCount=\"2\" "
           "/></itemPools></resourceNetworkPowerPools>"
           "</SomeOtherParams>\n  </Components>\n</EntityClassDefinition." +
           id + ">\n";
}

// The stock loadout: the nose entry names a gun but references the gimbal
// that holds its child entry; the wing entry names a gun and references a
// missile, which doesn't fit; the rack overrides one of its missiles.
QByteArray stockLoadout(const char *wingGun = "GUN_S3")
{
    return entries(entry("hardpoint_nose", "GUN_S3", guid(1), entry("hardpoint_class_2", "LASER_S3")) +
                   entry("hardpoint_wing", wingGun, guid(6)) +
                   entry("hardpoint_rack", "RACK_S3", {}, entry("missile_02_attach", "MISSILE_B_S2")) +
                   entry("hardpoint_power_plant", "POWER_S1") + entry("hardpoint_cooler", "COOLER_S1") +
                   entry("hardpoint_shield_left", "SHIELD_S1") +
                   entry("hardpoint_shield_right", "SHIELD_S1") + entry("hardpoint_quantum_drive", "QD_S1") +
                   entry("hardpoint_controller_flight", "FC") + entry("hardpoint_armor", "ARMOR") +
                   entry("hardpoint_engine", "MAIN") + entry("hardpoint_quantum_fuel_tank", "QTANK") +
                   entry("hardpoint_lifesupport", "LIFE_S1") +
                   entry("hardpoint_seat", "SEAT_NOT_IN_CATALOG"));
}

struct Fixture
{
    QTemporaryDir dir;
    QString records, vehicles;
    IniMap loc;
    Catalog catalog;

    Fixture()
    {
        records = dir.filePath(QStringLiteral("raw/libs/foundry/records"));
        vehicles = dir.filePath(QStringLiteral("raw/vehicles"));
        const QString ships = records + QStringLiteral("/entities/scitem/ships/");
        const QByteArray weaponHeat =
            "<SCItemWeaponComponentParams><fireActions>"
            "<SWeaponActionFireRapidParams name=\"Rapid\" fireRate=\"600\" heatPerShot=\"1\">"
            "<launchParams><SProjectileLauncher pelletCount=\"1\" damageMultiplier=\"1\" />"
            "</launchParams></SWeaponActionFireRapidParams></fireActions>"
            "<connectionParams><simplifiedHeatParams><SWeaponSimplifiedHeatParams "
            "overheatTemperature=\"50\" overheatFixTime=\"5\" /></simplifiedHeatParams>"
            "</connectionParams></SCItemWeaponComponentParams>";
        writeFile(ships + QStringLiteral("weapons/gun_s3.xml"),
                  item("GUN_S3", 2, "WeaponGun", "Gun", 3,
                       weaponHeat +
                           "<SAmmoContainerComponentParams maxAmmoCount=\"1000\" ammoParamsRecord=\"" +
                           guid(100) + "\" />" +
                           resources(state("Online", consume("Power", "SStandardResourceUnit", 0.5), 40)),
                       "Tags=\"weaponMountUsable\""));
        writeFile(ships + QStringLiteral("weapons/gun_s2.xml"),
                  item("GUN_S2", 3, "WeaponGun", "Gun", 2,
                       weaponHeat +
                           "<SAmmoContainerComponentParams maxAmmoCount=\"500\" ammoParamsRecord=\"" +
                           guid(100) + "\" />"));
        writeFile(
            ships + QStringLiteral("weapons/laser_s3.xml"),
            item(
                "LASER_S3", 4, "WeaponGun", "Gun", 3,
                "<SCItemWeaponComponentParams><fireActions><SWeaponActionSequenceParams name=\"Single\">"
                "<sequenceEntries><SWeaponSequenceEntryParams delay=\"120\" unit=\"RPM\"><weaponAction>"
                "<SWeaponActionFireSingleParams name=\"Single\" fireRate=\"120\" heatPerShot=\"0\">"
                "<launchParams><SProjectileLauncher pelletCount=\"1\" damageMultiplier=\"1\" />"
                "</launchParams></SWeaponActionFireSingleParams></weaponAction></SWeaponSequenceEntryParams>"
                "</sequenceEntries></SWeaponActionSequenceParams></fireActions></SCItemWeaponComponentParams>"
                "<SAmmoContainerComponentParams maxAmmoCount=\"0\" ammoParamsRecord=\"" +
                    guid(101) +
                    "\" /><SWeaponRegenConsumerParams maxAmmoLoad=\"10\" maxRegenPerSec=\"2\" "
                    "regenerationCooldown=\"1\" />" +
                    resources(state("Online", consume("Power", "SStandardResourceUnit", 1.5), 700))));
        writeFile(records + QStringLiteral("/ammoparams/vehicle/ammoparams.gun_ammo.xml"),
                  ammo("GUN_AMMO", 100, "DamagePhysical=\"20\" DamageEnergy=\"0\""));
        writeFile(records + QStringLiteral("/ammoparams/vehicle/ammoparams.laser_ammo.xml"),
                  ammo("LASER_AMMO", 101, "DamagePhysical=\"0\" DamageEnergy=\"50\""));
        writeFile(ships + QStringLiteral("weapon_mounts/gimbal_s3.xml"),
                  item("GIMBAL_S3", 1, "Turret", "GunTurret", 3,
                       ports(portDef("hardpoint_class_2", 3, 3, "WeaponGun", "Gun")) +
                           "<SEntityComponentDefaultLoadoutParams>" +
                           entries(entry("hardpoint_class_2", "GUN_S3")) +
                           "</SEntityComponentDefaultLoadoutParams>",
                       "Tags=\"gimbalMount\""));
        writeFile(ships + QStringLiteral("missile_racks/rack_s3.xml"),
                  item("RACK_S3", 5, "MissileLauncher", "MissileRack", 3,
                       ports(portDef("missile_01_attach", 2, 2, "Missile", "Missile") +
                             portDef("missile_02_attach", 2, 2, "Missile", "Missile")) +
                           "<SEntityComponentDefaultLoadoutParams>" +
                           entries(entry("missile_01_attach", "MISSILE_S2") +
                                   entry("missile_02_attach", "MISSILE_S2")) +
                           "</SEntityComponentDefaultLoadoutParams>"));
        const auto missile = [](const char *dmg) {
            return QByteArray(
                       "<SCItemMissileParams armTime=\"1\" maxLifetime=\"20\"><explosionParams><damage>"
                       "<DamageInfo DamagePhysical=\"") +
                   dmg +
                   "\" /></damage></explosionParams><GCSParams linearSpeed=\"900\" /><targetingParams "
                   "trackingSignalType=\"Infrared\" lockTime=\"2\" lockingAngle=\"30\" lockRangeMin=\"1000\" "
                   "lockRangeMax=\"8000\" /></SCItemMissileParams>";
        };
        writeFile(ships + QStringLiteral("weapons/missiles/missile_s2.xml"),
                  item("MISSILE_S2", 6, "Missile", "Missile", 2, missile("1000")));
        writeFile(ships + QStringLiteral("weapons/missiles/missile_b_s2.xml"),
                  item("MISSILE_B_S2", 7, "Missile", "Missile", 2, missile("1500")));
        writeFile(ships + QStringLiteral("powerplant/power_s1.xml"),
                  item("POWER_S1", 8, "PowerPlant", "Power", 1,
                       resources(state("Online",
                                       "<ItemResourceDeltaGeneration><generation " +
                                           amount("Power", "SPowerSegmentResourceUnit", 12) +
                                           "</generation></ItemResourceDeltaGeneration>",
                                       1000))));
        const QByteArray ranges =
            "<low start=\"1\" modifier=\"0.7\" /><medium start=\"2\" modifier=\"0.85\" />"
            "<high start=\"3\" modifier=\"1\" />";
        writeFile(ships + QStringLiteral("cooler/cooler_s1.xml"),
                  item("COOLER_S1", 9, "Cooler", "UNDEFINED", 1,
                       resources(state("Online",
                                       "<ItemResourceDeltaConversion minimumConsumptionFraction=\"0.3333\">"
                                       "<consumption " +
                                           amount("Power", "SPowerSegmentResourceUnit", 3) +
                                           "</consumption><generation " +
                                           amount("Coolant", "SStandardResourceUnit", 30) +
                                           "</generation></ItemResourceDeltaConversion>",
                                       0, 2000, ranges))));
        writeFile(
            ships + QStringLiteral("shieldgenerator/shield_s1.xml"),
            item(
                "SHIELD_S1", 10, "Shield", "UNDEFINED", 1,
                "<SCItemShieldGeneratorParams MaxShieldHealth=\"1000\" MaxShieldRegen=\"100\" "
                "DamagedRegenDelay=\"3\" DownedRegenDelay=\"6\"><ShieldResistance>"
                "<SShieldResistance Max=\"0.2\" Min=\"0\" /><SShieldResistance Max=\"0\" Min=\"0\" />"
                "<SShieldResistance Max=\"1\" Min=\"0.5\" /></ShieldResistance><ShieldAbsorption>"
                "<SShieldAbsorption Max=\"0.4\" Min=\"0\" /><SShieldAbsorption Max=\"1\" Min=\"1\" />"
                "<SShieldAbsorption Max=\"1\" Min=\"1\" /></ShieldAbsorption></SCItemShieldGeneratorParams>" +
                    resources(state("Online", consume("Power", "SPowerSegmentResourceUnit", 1, 1), 200, 0,
                                    "<high start=\"1\" modifier=\"1\" />"))));
        writeFile(
            ships + QStringLiteral("quantumdrive/qd_s1.xml"),
            item("QD_S1", 11, "QuantumDrive", "UNDEFINED", 1,
                 "<SCItemQuantumDriveParams quantumFuelRequirement=\"0.01\"><params driveSpeed=\"100000000\" "
                 "cooldownTime=\"10\" stageOneAccelRate=\"1000000\" stageTwoAccelRate=\"2000000\" "
                 "spoolUpTime=\"5\" /><splineJumpParams driveSpeed=\"500000\" /></SCItemQuantumDriveParams>" +
                     resources(state("Idle", consume("Power", "SStandardResourceUnit", 2, 1), 9000) +
                               state("Travelling", consume("Power", "SStandardResourceUnit", 2, 1) +
                                                       consume("QuantumFuel", "SMicroResourceUnit", 5)))));
        writeFile(ships + QStringLiteral("controller/fc.xml"),
                  item("FC", 12, "FlightController", "UNDEFINED", 1,
                       "<IFCSParams scmSpeed=\"200\" boostSpeedForward=\"500\" boostSpeedBackward=\"250\" "
                       "maxSpeed=\"1200\"><maxAngularVelocity x=\"50\" y=\"150\" z=\"40\" /><afterburnerNew "
                       "capacitorMax=\"20\" capacitorRegenPerSec=\"1\" capacitorRegenDelayAfterUse=\"0.5\">"
                       "<afterburnAngVelocityMultiplier x=\"1.2\" y=\"1\" z=\"1.1\" "
                       "/></afterburnerNew></IFCSParams>" +
                           resources(state("Online", consume("Power", "SPowerSegmentResourceUnit", 4)))));
        writeFile(
            ships + QStringLiteral("armor/armor.xml"),
            item("ARMOR", 13, "Armor", "Medium", 1,
                 "<SCItemVehicleArmorParams signalInfrared=\"1.1\" signalElectromagnetic=\"1.2\" "
                 "signalCrossSection=\"0.9\"><damageMultiplier><DamageInfo DamagePhysical=\"0.8\" "
                 "DamageEnergy=\"0.5\" /></damageMultiplier><armorDeflection><deflectionValue "
                 "DamagePhysical=\"25\" DamageEnergy=\"0\" /></armorDeflection></SCItemVehicleArmorParams>"
                 "<SHealthComponentParams Health=\"500\" />"));
        writeFile(ships + QStringLiteral("thrusters/main.xml"),
                  item("MAIN", 14, "MainThruster", "FixedThruster", 2,
                       "<SCItemThrusterParams thrustCapacity=\"2600000\" thrusterType=\"Main\" />"));
        writeFile(
            ships + QStringLiteral("fueltanks/qtank.xml"),
            item("QTANK", 15, "QuantumFuelTank", "QuantumFuel", 1,
                 "<ResourceContainer><capacity><SStandardCargoUnit standardCargoUnits=\"0.5\" /></capacity>"
                 "</ResourceContainer>"));
        writeFile(ships + QStringLiteral("lifesupport/life_s1.xml"),
                  item("LIFE_S1", 16, "LifeSupportGenerator", "UNDEFINED", 1,
                       resources(state("Online", consume("Power", "SPowerSegmentResourceUnit", 1, 1)))));
        writeFile(ships + QStringLiteral("seat/seat.xml"), item("SEAT", 17, "Seat", "UNDEFINED", 1, {}));
        writeFile(records + QStringLiteral("/scitemmanufacturer/scitemmanufacturer.acme.xml"),
                  "<SCItemManufacturer.ACME Code=\"ACM\" __type=\"SCItemManufacturer\" __ref=\"" + kAcme +
                      "\"><Localization Name=\"@mfr_acme\" /></SCItemManufacturer.ACME>");

        const QString spaceships = records + QStringLiteral("/entities/spaceships/");
        writeFile(spaceships + QStringLiteral("acme_dart.xml"),
                  shipRecord("ACME_Dart", 801, "ship_test", "", stockLoadout()));
        // Same name and loadout: a copy that goes. Same name, other loadout: a variant that stays.
        writeFile(spaceships + QStringLiteral("acme_dart_showroom.xml"),
                  shipRecord("ACME_Dart_Showroom", 802, "ship_test", "", stockLoadout()));
        writeFile(spaceships + QStringLiteral("acme_dart_ballistic.xml"),
                  shipRecord("ACME_Dart_Ballistic", 803, "ship_test", "", stockLoadout("GUN_S2")));
        writeFile(spaceships + QStringLiteral("acme_dart_variant.xml"),
                  shipRecord("ACME_Dart_Variant", 804, "ship_variant", "Variant", stockLoadout()));
        writeFile(spaceships + QStringLiteral("acme_dart_rebuilt.xml"),
                  shipRecord("ACME_Dart_Rebuilt", 805, "ship_rebuilt", "Rebuilt", stockLoadout()));
        writeFile(spaceships + QStringLiteral("acme_dart_pu_ai_civ.xml"),
                  shipRecord("ACME_Dart_PU_AI_CIV", 806, "ship_test", "", stockLoadout()));

        const QByteArray guns = type("Turret", "GunTurret") + type("WeaponGun", "Gun");
        writeFile(
            vehicles + QStringLiteral("/acme_dart.xml"),
            "<Vehicle name=\"ACME_Dart\" size=\"3\" itemPortTags=\"ACME_Base\" id=\"modVehicle\"><Parts>"
            "<Part name=\"ACME_Dart\" class=\"Animated\" mass=\"20000\" id=\"modMain\"><Parts>"
            "<Part name=\"Nose\" class=\"AnimatedJoint\" damageMax=\"1000\" />"
            "<Part name=\"Body\" class=\"AnimatedJoint\" damageMax=\"2000\"><Parts>"
            "<Part name=\"Wing\" class=\"AnimatedJoint\" damageMax=\"500\" />" +
                itemPort("hardpoint_nose", 3, 3, guns) +
                "<Part name=\"hardpoint_wing\" class=\"ItemPort\"><ItemPort minsize=\"3\" maxsize=\"3\" "
                "flags=\"\"><Types>" +
                type("WeaponGun", "Gun") + "</Types></ItemPort></Part>" +
                itemPort("hardpoint_rack", 3, 3, type("MissileLauncher", "MissileRack")) +
                itemPort("hardpoint_power_plant", 1, 1, type("PowerPlant"), "invisible editable") +
                itemPort("hardpoint_cooler", 1, 1, type("Cooler")) +
                itemPort("hardpoint_shield_left", 1, 1, type("Shield")) +
                itemPort("hardpoint_shield_right", 1, 1, type("Shield")) +
                itemPort("hardpoint_quantum_drive", 1, 1, type("QuantumDrive", "QDrive")) +
                itemPort("hardpoint_controller_flight", 1, 1, type("FlightController"),
                         "invisible uneditable") +
                itemPort("hardpoint_armor", 1, 1, type("Armor"), "invisible uneditable") +
                itemPort("hardpoint_engine", 1, 4, type("MainThruster"), "uneditable") +
                itemPort("hardpoint_quantum_fuel_tank", 1, 1, type("QuantumFuelTank"), "uneditable") +
                itemPort("hardpoint_extra", 1, 1, type("Cooler"), "", "modExtra") +
                "</Parts></Part></Parts></Part></Parts><Modifications>"
                "<Modification name=\"Variant\"><Elems><Elem idRef=\"modMain\" name=\"mass\" value=\"25000\" "
                "/>"
                "<Elem idRef=\"modExtra\" name=\"skipPart\" value=\"1\" /></Elems></Modification>"
                "<Modification name=\"Rebuilt\" patchFile=\"Modifications/ACME_Dart_Rebuilt\">"
                "<Elems><Elem idRef=\"modNewMain\" name=\"mass\" value=\"1500\" /></Elems></Modification>"
                "</Modifications></Vehicle>");
        // Replaces the whole <Vehicle>, as the Vanguard Sentinel's patch does.
        writeFile(vehicles + QStringLiteral("/modifications/acme_dart_rebuilt.xml"),
                  "<Modifications><Vehicle name=\"ACME_Dart\" size=\"4\" id=\"modVehicle\"><Parts>"
                  "<Part name=\"ACME_Dart\" class=\"Animated\" mass=\"1000\" id=\"modNewMain\"><Parts>"
                  "<Part name=\"Core\" class=\"AnimatedJoint\" damageMax=\"300\" />" +
                      itemPort("hardpoint_nose", 3, 3, guns) +
                      "</Parts></Part></Parts></Vehicle></Modifications>");

        const QString iniPath = dir.filePath(QStringLiteral("base.ini"));
        writeFile(iniPath, kLoc);
        loc = loadIni(iniPath);
        catalog = buildCatalog({records, vehicles, &loc});
    }

    const Ship &ship(const char *id) const
    {
        const Ship *s = catalog.ship(QString::fromLatin1(id));
        if (!s)
            qFatal("no ship %s", id);
        return *s;
    }
};

QString itemAt(const Loadout &l, const char *path)
{
    const Slot *s = l.find(QString::fromLatin1(path));
    return s && s->item ? s->item->id : QString();
}

bool near(double a, double b, double tolerance = 1e-6)
{
    return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(b));
}

} // namespace

class TestLoadout : public QObject
{
    Q_OBJECT

private slots:
    void playerShipRecords()
    {
        QVERIFY(isPlayerShipRecord(QStringLiteral("AEGS_Avenger_Stalker")));
        QVERIFY(isPlayerShipRecord(QStringLiteral("anvl_hornet_f7a_mk2_exec_stealth")));
        QVERIFY(!isPlayerShipRecord(QStringLiteral("aegs_avenger_stalker_pu_ai_civ")));
        QVERIFY(!isPlayerShipRecord(QStringLiteral("AEGS_Gladius_Template")));
        QVERIFY(!isPlayerShipRecord(QStringLiteral("anvl_c8_pisces_tutorial")));
        QVERIFY(!isPlayerShipRecord(QStringLiteral("probe_comms_1")));
    }

    void readsShipsAndVariants()
    {
        const Fixture f;
        QVERIFY(f.catalog.hasVehicles);
        QStringList names;
        for (const Ship &s : f.catalog.ships)
            names << s.name;
        // The AI copy is filtered, the showroom copy merged, the ballistic one kept apart.
        QCOMPARE(names,
                 QStringList({QStringLiteral("Acme Dart"), QStringLiteral("Acme Dart (Ballistic)"),
                              QStringLiteral("Acme Dart Rebuilt"), QStringLiteral("Acme Dart Variant")}));

        const Ship &base = f.ship("ACME_Dart");
        QCOMPARE(base.manufacturer, QStringLiteral("Acme Industries"));
        QCOMPARE(base.manufacturerCode, QStringLiteral("ACM"));
        QCOMPARE(base.description, QStringLiteral("A test ship."));
        QCOMPARE(base.career, QStringLiteral("Combat"));
        QCOMPARE(base.role, QStringLiteral("Light Fighter"));
        QCOMPARE(base.size, 3);
        QCOMPARE(base.crew, 2);
        QCOMPARE(base.length, 20.0);
        QCOMPARE(base.mass, 20000.0);
        QCOMPARE(base.hullHp(), 3500.0);
        QCOMPARE(base.vitalHp(), 3000.0); // the nose and body, not the wing under the body
        QVERIFY(base.portTags.contains(QStringLiteral("ACME_Base")));
        QVERIFY(base.portTags.contains(QStringLiteral("ACME_Record")));
        QCOMPARE(base.powerPools.size(), std::size_t(2));
        QVERIFY(base.powerPools[0].fixed);
        QCOMPARE(base.powerPools[0].size, 4);

        const auto hasPort = [](const Ship &s, const char *name) {
            return std::any_of(s.ports.begin(), s.ports.end(),
                               [&](const Port &p) { return p.name == QLatin1StringView(name); });
        };
        QVERIFY(hasPort(base, "hardpoint_extra"));
        QVERIFY(hasPort(base, "hardpoint_lifesupport")); // declared in the record, not the vehicle XML

        const Ship &variant = f.ship("ACME_Dart_Variant");
        QCOMPARE(variant.mass, 25000.0);               // an Elem set the main part's mass
        QVERIFY(!hasPort(variant, "hardpoint_extra")); // and skipped a port

        const Ship &rebuilt = f.ship("ACME_Dart_Rebuilt");
        QCOMPARE(rebuilt.size, 4);      // its patch file replaced the whole <Vehicle>
        QCOMPARE(rebuilt.mass, 1500.0); // then its Elem applied to the new part
        QCOMPARE(rebuilt.hullHp(), 300.0);
        QVERIFY(hasPort(rebuilt, "hardpoint_nose"));
        QVERIFY(!hasPort(rebuilt, "hardpoint_rack"));
    }

    void readsItems()
    {
        const Fixture f;
        QVERIFY(!f.catalog.item(QStringLiteral("SEAT"))); // not a loadout item type
        const Item *gun = f.catalog.item(QStringLiteral("gun_s3"));
        QVERIFY(gun);
        QCOMPARE(gun->name, QStringLiteral("Pew Gun"));
        QCOMPARE(gun->itemClass, QStringLiteral("Military"));
        QCOMPARE(gun->description, QStringLiteral("It shoots."));
        QCOMPARE(gun->manufacturerCode, QStringLiteral("ACM"));
        QCOMPARE(gun->gradeLetter(), QStringLiteral("C"));
        QCOMPARE(gun->mass, 100.0);
        QVERIFY(gun->weapon);
        const WeaponStats &w = *gun->weapon;
        QCOMPARE(w.fireMode, QStringLiteral("Rapid"));
        QCOMPARE(w.alpha(), 20.0);
        QCOMPARE(w.burstDps(), 200.0);
        QCOMPARE(w.range, 2000.0);
        QCOMPARE(w.penetration, 3.0);
        QCOMPARE(w.ammo, 1000);
        QCOMPARE(w.timeToOverheat(), 5.0); // 50 heat at 10 shots of 1 a second
        QCOMPARE(w.sustainedDps(), 100.0); // 5 s firing, 5 s locked out

        const WeaponStats &laser = *f.catalog.item(QStringLiteral("LASER_S3"))->weapon;
        QCOMPARE(laser.fireRate, 120.0); // from the sequence's fire action
        QVERIFY(laser.regenerates);
        QCOMPARE(laser.ammo, 10);
        QCOMPARE(laser.burstDps(), 100.0);
        // 10 shots of 50 over 5 s emptying + 1 s delay + 5 s refilling at 2 a second.
        QVERIFY(near(laser.sustainedDps(), 500.0 / 11));
        QVERIFY(near(laser.sustainedDps(0.5), 500.0 / 16)); // refilling at half power

        const Item *cooler = f.catalog.item(QStringLiteral("COOLER_S1"));
        QCOMPARE(cooler->itemClass, QStringLiteral("Civilian"));
        QCOMPARE(cooler->resources.powerDraw, 3.0);
        QCOMPARE(cooler->resources.coolantOutput, 30.0);
        QCOMPARE(cooler->resources.ranges.size(), std::size_t(3));
        QCOMPARE(powerModifier(cooler->resources, 0), 0.0);
        QCOMPARE(powerModifier(cooler->resources, 1), 0.7);
        QCOMPARE(powerModifier(cooler->resources, 2), 0.85);
        QCOMPARE(powerModifier(cooler->resources, 3), 1.0);

        const QuantumStats &qd = *f.catalog.item(QStringLiteral("QD_S1"))->quantum;
        QCOMPARE(qd.speed, 1e8);
        QCOMPARE(qd.spoolTime, 5.0);
        QCOMPARE(qd.splineSpeed, 500000.0);
        QCOMPARE(qd.fuelPerGm, 0.005); // the travelling state's 5 micro units: 5 mSCU a gigametre

        const ShieldStats &shield = *f.catalog.item(QStringLiteral("SHIELD_S1"))->shield;
        QCOMPARE(shield.hp, 1000.0);
        QCOMPARE(shield.resistanceMax.physical, 0.2);
        QCOMPARE(shield.resistanceMax.distortion, 1.0);
        QCOMPARE(shield.absorptionMax.physical, 0.4);

        const FlightStats &fc = *f.catalog.item(QStringLiteral("FC"))->flight;
        QCOMPARE(fc.scmSpeed, 200.0);
        QCOMPARE(fc.pitch, 50.0);
        QCOMPARE(fc.roll, 150.0);
        QCOMPARE(fc.yaw, 40.0);
        QCOMPARE(fc.pitchBoost, 1.2);

        const MissileStats &m = *f.catalog.item(QStringLiteral("MISSILE_S2"))->missile;
        QCOMPARE(m.damage.total(), 1000.0);
        QCOMPARE(m.tracking, QStringLiteral("Infrared"));
        QCOMPARE(m.lockRangeMax, 8000.0);
    }

    void stockLoadout()
    {
        const Fixture f;
        const Loadout l(f.catalog, f.ship("ACME_Dart"));
        // The nose entry's reference holds its child entry: the gimbal wins
        // over the named gun, and the child replaces the gimbal's own gun.
        QCOMPARE(itemAt(l, "hardpoint_nose"), QStringLiteral("GIMBAL_S3"));
        QCOMPARE(itemAt(l, "hardpoint_nose/hardpoint_class_2"), QStringLiteral("LASER_S3"));
        // The wing's reference is a missile, which doesn't fit: the named gun.
        QCOMPARE(itemAt(l, "hardpoint_wing"), QStringLiteral("GUN_S3"));
        // The rack keeps its own first missile and takes the ship's second.
        QCOMPARE(itemAt(l, "hardpoint_rack/missile_01_attach"), QStringLiteral("MISSILE_S2"));
        QCOMPARE(itemAt(l, "hardpoint_rack/missile_02_attach"), QStringLiteral("MISSILE_B_S2"));
        QCOMPARE(itemAt(l, "hardpoint_lifesupport"), QStringLiteral("LIFE_S1"));
        QCOMPARE(itemAt(l, "hardpoint_extra"), QString());
        QVERIFY(!l.find(QStringLiteral("hardpoint_seat"))); // its item isn't in the catalog
        QVERIFY(!l.find(QStringLiteral("hardpoint_nose/nothing")));

        const Loadout ballistic(f.catalog, f.ship("ACME_Dart_Ballistic"));
        QCOMPARE(itemAt(ballistic, "hardpoint_wing"), QStringLiteral("GUN_S2"));
    }

    void editing()
    {
        const Fixture f;
        Loadout l(f.catalog, f.ship("ACME_Dart"));
        QStringList ids;
        for (const Item *i : l.compatible(QStringLiteral("hardpoint_nose")))
            ids << i->id;
        // Size 3 guns and gimbals by name; not the size 2 gun, missiles or racks.
        QCOMPARE(ids, QStringList({QStringLiteral("GUN_S3"), QStringLiteral("GIMBAL_S3"),
                                   QStringLiteral("LASER_S3")}));
        ids.clear();
        for (const Item *i : l.compatible(QStringLiteral("hardpoint_quantum_drive")))
            ids << i->id;
        QCOMPARE(ids, QStringList({QStringLiteral("QD_S1")})); // an UNDEFINED subtype goes in a QDrive port

        QVERIFY(!l.setItem(QStringLiteral("hardpoint_nose"), QStringLiteral("GUN_S2"))); // too small
        QVERIFY(!l.setItem(QStringLiteral("hardpoint_armor"), QString()));               // not editable
        QVERIFY(!l.setItem(QStringLiteral("hardpoint_nose"), QStringLiteral("NO_SUCH_ITEM")));
        QVERIFY(l.setItem(QStringLiteral("hardpoint_nose"), QStringLiteral("GUN_S3")));
        QVERIFY(l.find(QStringLiteral("hardpoint_nose"))->children.empty());
        QVERIFY(l.setItem(QStringLiteral("hardpoint_nose"), QStringLiteral("GIMBAL_S3")));
        QCOMPARE(itemAt(l, "hardpoint_nose/hardpoint_class_2"), QStringLiteral("GUN_S3")); // the gimbal's own
        QVERIFY(l.setItem(QStringLiteral("hardpoint_rack/missile_01_attach"), QString()));
        QCOMPARE(itemAt(l, "hardpoint_rack/missile_01_attach"), QString());

        // A saved loadout comes back the same on the stock ship.
        const auto saved = l.entries();
        Loadout restored(f.catalog, f.ship("ACME_Dart"));
        QCOMPARE(restored.apply(saved), 0);
        QCOMPARE(restored.entries(), saved);
        QCOMPARE(itemAt(restored, "hardpoint_nose/hardpoint_class_2"), QStringLiteral("GUN_S3"));
        QVERIFY(!restored.changes().isEmpty());
        QVERIFY(Loadout(f.catalog, f.ship("ACME_Dart")).changes().isEmpty());
        // Entries for slots that are gone are skipped.
        QCOMPARE(restored.apply({{QStringLiteral("hardpoint_gone"), QStringLiteral("GUN_S3")}}), 1);
    }

    void power()
    {
        const Fixture f;
        const Loadout l(f.catalog, f.ship("ACME_Dart"));
        auto cs = consumers(l, {});
        const auto find = [&](const std::vector<Consumer> &list, const QString &key) -> const Consumer & {
            for (const Consumer &c : list)
                if (c.key == key)
                    return c;
            qFatal("no consumer %s", qPrintable(key));
        };
        // 12 pips: cooler 1, shields 1 + 1, life support 1, weapons 2 of 4,
        // engines 2 of 4; then weapons to 4 and engines to 4 (12).
        QCOMPARE(find(cs, QStringLiteral("weapons")).max, 4);
        QCOMPARE(find(cs, QStringLiteral("weapons")).pips, 4);
        QCOMPARE(find(cs, QStringLiteral("hardpoint_controller_flight")).pips, 4);
        QCOMPARE(find(cs, QStringLiteral("hardpoint_cooler")).pips, 1);
        QCOMPARE(find(cs, QStringLiteral("hardpoint_cooler")).min, 1);
        QCOMPARE(find(cs, QStringLiteral("hardpoint_quantum_drive")).pips, 0); // SCM mode
        QCOMPARE(find(cs, QStringLiteral("hardpoint_quantum_drive")).min, 2);

        Totals t = computeTotals(l, cs);
        QCOMPARE(t.powerOutput, 12.0);
        QCOMPARE(t.powerUsed, 12);
        QCOMPARE(t.coolantOutput, 30 * 0.7);
        QCOMPARE(t.shieldHp, 2000.0);
        QCOMPARE(t.shieldRegen, 200.0);

        PowerPlan plan;
        plan.pips.insert(QStringLiteral("hardpoint_cooler"), 3);
        plan.pips.insert(QStringLiteral("hardpoint_shield_right"), 0);
        plan.pips.insert(QStringLiteral("weapons"), 2);
        cs = consumers(l, plan);
        t = computeTotals(l, cs);
        QCOMPARE(t.coolantOutput, 30.0);
        QCOMPARE(t.shieldHp, 1000.0); // an unpowered generator holds nothing
        QCOMPARE(t.weaponEfficiency, 0.5);

        PowerPlan nav;
        nav.nav = true;
        cs = consumers(l, nav);
        QCOMPARE(find(cs, QStringLiteral("weapons")).pips, 0);
        QCOMPARE(find(cs, QStringLiteral("hardpoint_shield_left")).pips, 0);
        QCOMPARE(find(cs, QStringLiteral("hardpoint_quantum_drive")).pips, 2);
    }

    void totals()
    {
        const Fixture f;
        const Loadout l(f.catalog, f.ship("ACME_Dart"));
        const Totals t = computeTotals(l, consumers(l, {}));
        QCOMPARE(t.weapons.size(), std::size_t(2));
        QCOMPARE(t.pilot.guns, 2);
        QCOMPARE(t.alpha, 70.0);     // laser 50 + gun 20
        QCOMPARE(t.burstDps, 300.0); // 100 + 200
        QVERIFY(near(t.sustainedDps, 500.0 / 11 + 100));
        QCOMPARE(t.burstByType.physical, 200.0);
        QCOMPARE(t.missiles, 2);
        QCOMPARE(t.missileDamage, 2500.0);
        QCOMPARE(t.armorHp, 500.0);
        QCOMPARE(t.armorDeflection.physical, 25.0);
        QCOMPARE(t.hullHp, 3500.0);
        QCOMPARE(t.vitalHp, 3000.0);
        QVERIFY(t.flight);
        QCOMPARE(t.flight->scmSpeed, 200.0);
        QCOMPARE(t.mainThrust, 2.6e6);
        QVERIFY(t.loadedMass > t.hullMass);
        QVERIFY(near(t.accelForward, 2.6e6 / t.loadedMass));
        QCOMPARE(t.quantumFuel, 0.5);
        QCOMPARE(t.quantumRange, 100.0); // 0.5 SCU at 5 mSCU a gigametre
        QVERIFY(t.em > 0);
        QVERIFY(t.ir > 0);
    }

    void quantumTravel()
    {
        QuantumStats qd;
        qd.speed = 1e8;
        qd.stageOneAccel = 1e6;
        qd.stageTwoAccel = 2e6;
        QCOMPARE(quantumTravelTime(qd, 0), 0.0);
        // Ramp: 50 s to half speed (1.25e9 m), 25 s more to full (1.875e9 m),
        // each way: 150 s and 6.25e9 m; the rest at full speed.
        QVERIFY(near(quantumTravelTime(qd, 6.25e9), 150.0));
        QVERIFY(near(quantumTravelTime(qd, 16.25e9), 250.0));
        // Too short to reach half speed: accelerate and brake at stage one.
        QVERIFY(near(quantumTravelTime(qd, 2e9), 2 * std::sqrt(2 * 1e9 / 1e6)));
        QVERIFY(quantumTravelTime(qd, 4e9) < quantumTravelTime(qd, 6e9));
    }

    void engagement()
    {
        const Fixture f;
        const Loadout l(f.catalog, f.ship("ACME_Dart"));
        const Totals t = computeTotals(l, consumers(l, {}));
        const Engagement e = engage(t, t);
        // The gun's 20 physical glances off 25 deflection; the laser doesn't.
        QCOMPARE(e.deflected, 1);
        // Shields: the laser's energy is fully absorbed and not resisted,
        // the gun's physical 40% absorbed, 20% of that resisted.
        const double laser = 500.0 / 11;
        const double shieldRate = laser + 100 * 0.4 * 0.8;
        QVERIFY(near(e.shieldTime, 2000 / shieldRate));
        // Nothing bleeds through while they're up (the laser is all
        // absorbed, the gun deflected); then the laser at half through armor.
        QVERIFY(near(e.killTime, e.shieldTime + 3000 / (laser * 0.5)));
    }
};

QTEST_GUILESS_MAIN(TestLoadout)
#include "tst_loadout.moc"
