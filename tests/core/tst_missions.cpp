// The mission catalog over a small DataForge cache written by the test:
// tags, places, organizations, a mission type, mission broker entries and a
// contract generator with a template.

#include "core/missions/Catalog.h"
#include "core/missions/Places.h"
#include "core/text/IniFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace core;
using namespace core::missions;

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot open %s", qPrintable(path));
    f.write(bytes);
}

// guid, name, parent. Tag ids need not look like GUIDs.
const QByteArray kTags = "root\tLocationType\t\n"
                         "sys\tSystem\troot\n"
                         "stanton\tStanton\tsys\n"
                         "stanton1\tStanton1\tstanton\n"
                         "stanton2\tStanton2\tstanton\n"
                         "stanton2b\tStanton2b\tstanton2\n"
                         "pyro\tPyro\tsys\n"
                         "hub\tShippingHub\troot\n"
                         "outpost\tOutpost\troot\n"
                         "lawless\tLawless\troot\n"
                         "hydrogen\tHydrogen\troot\n"
                         "name\tName\t\n"
                         "address\tAddress\t\n"
                         "couriers\tCouriers\t\n";

const char *const kLoc = R"x(m_title=~mission(Title)
m_title_1=Deliver to ~mission(Destination)
m_title_2=Second title
m_desc=Go to ~mission(Destination|Address).\nPick up at ~mission(Location).
m_giver=~mission(Contractor)
c_title=Clear the outpost
t1_title=Template title
t1_desc=Pick up at ~mission(Location).
c3_title=Fixed job
ct_contractor=Contract Co
org1_name=Couriers Guild
org2_name=Second Couriers
loc_gundo=Covalex Station Gundo
loc_gundo_addr=Covalex Station Gundo around Daymar
loc_bravo=Bravo Outpost
Loc_Charlie=Charlie Outpost
loc_nope=< / NOT AVAILABLE / >
mt_delivery=Delivery
faction_covalex=Covalex Shipping
scope_courier=Courier
RepStanding_Courier_Rank2=Jr. Runner
RepScope_Courier_Name=Courier
item_test_rifle=Test Rifle
)x";

QByteArray place(const char *name, const char *tags, const char *strings, const char *extra = "",
                 const char *disabled = "0")
{
    return QStringLiteral(R"x(<MissionLocationTemplate.%1 __type="MissionLocationTemplate" __ref="place-%1">
  <locationData disabled="%4">
    <generalTags><tags>%2</tags></generalTags>%5
    <stringVariants><variants>%3</variants></stringVariants>
  </locationData>
</MissionLocationTemplate.%1>)x")
        .arg(s(name), s(tags), s(strings), s(disabled), s(extra))
        .toUtf8();
}

// A location search property: positive and negative General tags.
QByteArray locationValue(const char *positive, const char *negative = "", const char *produces = "")
{
    QString conditions =
        QStringLiteral(R"x(<DataSetMatchCondition_TagSearch tagType="General"><tagSearch><TagSearchTerm>
<positiveTags>%1</positiveTags><negativeTags>%2</negativeTags></TagSearchTerm></tagSearch></DataSetMatchCondition_TagSearch>)x")
            .arg(s(positive), s(negative));
    if (*produces)
        conditions +=
            QStringLiteral(R"x(<DataSetMatchCondition_TagSearch tagType="Produces"><tagSearch><TagSearchTerm>
<positiveTags>%1</positiveTags></TagSearchTerm></tagSearch></DataSetMatchCondition_TagSearch>)x")
                .arg(s(produces));
    return QStringLiteral("<MissionPropertyValue_Location><matchConditions>%1</matchConditions>"
                          "</MissionPropertyValue_Location>")
        .arg(conditions)
        .toUtf8();
}

QByteArray ref(const char *guid)
{
    return QByteArray("<Reference value=\"") + guid + "\" />";
}

void writeCache(const QString &root)
{
    writeFile(root + s("/raw/tags.tsv"), kTags);
    const QString r = root + s("/raw/libs/foundry/records/");

    const QByteArray nameOnly = R"(<MissionStringVariant tag="name" string="@loc_bravo" />)";
    writeFile(
        r + s("missiondata/pu_locations/stations/gundo.xml"),
        place(
            "Gundo", ref("hub") + ref("stanton") + ref("stanton2") + ref("stanton2b"),
            R"(<MissionStringVariant tag="name" string="@loc_gundo" /><MissionStringVariant tag="address" string="@loc_gundo_addr" />)"));
    // No system tags: placed by its record name, on Stanton 1.
    writeFile(r + s("missiondata/pu_locations/outposts/outpost_stanton1_bravo.xml"),
              place("Bravo", ref("outpost"), nameOnly));
    writeFile(
        r + s("missiondata/pu_locations/outposts/outpost_pyro_charlie.xml"),
        place("Charlie", ref("outpost") + ref("lawless"),
              R"(<MissionStringVariant tag="name" string="@loc_charlie" />)",
              "<producesTags><positiveTags><Reference value=\"hydrogen\" /></positiveTags></producesTags>"));
    writeFile(r + s("missiondata/pu_locations/outposts/disabled.xml"),
              place("Disabled", ref("outpost"), nameOnly, "", "1"));

    for (const auto &[id, name] : {std::pair{"org1", "@org1_name"}, {"org2", "@org2_name"}})
        writeFile(r + s("missiondata/pu_organizations/%1.xml").arg(s(id)),
                  QStringLiteral(R"x(<MissionOrganization.%1 __type="MissionOrganization" __ref="%1">
  <organizationTags><MissionLocationTags><tags><Reference value="couriers" /></tags></MissionLocationTags></organizationTags>
  <stringVariants><variants><MissionStringVariant tag="name" string="%2" /></variants></stringVariants>
</MissionOrganization.%1>)x")
                      .arg(s(id), s(name))
                      .toUtf8());
    writeFile(
        r + s("missiontype/missiontype.delivery.xml"),
        R"(<MissionType.Delivery __type="MissionType" __ref="type-delivery" LocalisedTypeName="@mt_delivery" />)");

    // Reputation: amounts, a faction, a track and a rank.
    writeFile(r + s("reputation/rewards/missionrewards_reputation/gain.xml"),
              R"(<SReputationRewardAmount.Gain __ref="rep-gain" reputationAmount="150" />)");
    writeFile(r + s("reputation/rewards/missionrewards_reputation/loss.xml"),
              R"(<SReputationRewardAmount.Loss __ref="rep-loss" reputationAmount="-100" />)");
    writeFile(
        r + s("factions/factionreputation/covalex.xml"),
        R"(<FactionReputation.Covalex __type="FactionReputation" __ref="faction-covalex" displayName="@faction_covalex" />)");
    writeFile(
        r + s("reputation/scopes/courier.xml"),
        R"(<SReputationScopeParams.Courier __type="SReputationScopeParams" __ref="scope-courier" scopeName="Courier" displayName="@scope_courier" />)");
    writeFile(
        r + s("reputation/standings/courier_rank2.xml"),
        R"(<SReputationStandingParams.Rank2 __ref="standing-2" displayName="@RepStanding_Courier_Rank2" />)");

    // A blueprint pool: its item named from the item record its file matches.
    writeFile(
        r + s("crafting/blueprints/crafting/bp_craft_test_rifle.xml"),
        R"(<CraftingBlueprintRecord.R __ref="bp-rifle"><process><CraftingProcess_Creation entityClass="ent-rifle" /></process></CraftingBlueprintRecord.R>)");
    writeFile(
        r + s("crafting/blueprintrewards/bp_rewards_rank2to4.xml"),
        R"(<BlueprintPoolRecord.P __ref="pool-1"><blueprintRewards><BlueprintReward blueprintRecord="bp-rifle" /></blueprintRewards></BlueprintPoolRecord.P>)");
    writeFile(
        r + s("entities/scitem/weapons/test_rifle.xml"),
        R"(<EntityClassDefinition.test_rifle __ref="ent-rifle"><Components><SAttachableComponentParams><AttachDef><Localization Name="@item_test_rifle" /></AttachDef></SAttachableComponentParams></Components></EntityClassDefinition.test_rifle>)");

    writeFile(
        r + s("missionbroker/pu_missions/delivery/a.xml"),
        QByteArray(
            R"(<MissionBrokerEntry.A __type="MissionBrokerEntry" notForRelease="0" title="@m_title" description="@m_desc" missionGiver="@m_giver" type="type-delivery" missionBuyInAmount="250">
  <missionReward reward="5000" max="0" plusBonuses="0" currencyType="UEC" />
  <missionResultReputationRewards>
    <SReputationAmountListParams><reputationAmounts><SReputationAmountParams factionReputation="faction-covalex" reputationScope="scope-courier" reward="rep-gain" /></reputationAmounts></SReputationAmountListParams>
    <SReputationAmountListParams><reputationAmounts><SReputationAmountParams factionReputation="faction-covalex" reputationScope="scope-courier" reward="rep-loss" /></reputationAmounts></SReputationAmountListParams>
    <SReputationAmountListParams><reputationAmounts><SReputationAmountParams factionReputation="faction-covalex" reputationScope="scope-courier" reward="rep-loss" /></reputationAmounts></SReputationAmountListParams>
  </missionResultReputationRewards>
  <reputationRequirements><SReputationMissionRequirementsParams><expression>
    <SReputationMissionGiverRequirementParams factionReputation="faction-covalex" reputationScope="scope-courier" comparison="GreaterThanOrEqualTo" standing="standing-2" />
  </expression></SReputationMissionRequirementsParams></reputationRequirements>
  <MissionProperty missionVariableName="Mission_Title" extendedTextToken="Title"><value><MissionPropertyValue_StringHash><options>
    <MissionPropertyValueOption_StringHash textId="@m_title_1" /><MissionPropertyValueOption_StringHash textId="@m_title_2" /><MissionPropertyValueOption_StringHash textId="@missing" />
  </options></MissionPropertyValue_StringHash></value></MissionProperty>
  <MissionProperty missionVariableName="Destination" extendedTextToken="Destination"><value>)") +
            locationValue(ref("hub")) + R"(</value></MissionProperty>
  <MissionProperty missionVariableName="Pickup_BP" extendedTextToken="Location"><value><MissionPropertyValue_CombinedDataSetEntries><dataSetEntryProperties>
    <MissionProperty missionVariableName="PickupLocation"><value>)" +
            locationValue(ref("outpost") + ref("stanton")) + R"(</value></MissionProperty>
  </dataSetEntryProperties></MissionPropertyValue_CombinedDataSetEntries></value></MissionProperty>
  <MissionProperty extendedTextToken="Contractor"><value><MissionPropertyValue_Organization><matchConditions>
    <DataSetMatchCondition_SpecificOrganizationsDef><organizations><Reference value="org1" /></organizations></DataSetMatchCondition_SpecificOrganizationsDef>
  </matchConditions></MissionPropertyValue_Organization></value></MissionProperty>
</MissionBrokerEntry.A>)");
    writeFile(r + s("missionbroker/pu_missions/delivery/b.xml"),
              R"(<MissionBrokerEntry.B __type="MissionBrokerEntry" notForRelease="1" title="@c_title" />)");
    writeFile(
        r + s("missionbroker/pu_missions/mercenary/c.xml"),
        QByteArray(R"(<MissionBrokerEntry.C __type="MissionBrokerEntry" notForRelease="0" title="@c_title">
  <MissionProperty missionVariableName="Target" extendedTextToken="Location"><value>)") +
            locationValue(ref("outpost"), ref("lawless")) + R"(</value></MissionProperty>
</MissionBrokerEntry.C>)");

    writeFile(r + s("contracts/contracttemplates/t1.xml"),
              QByteArray(R"(<ContractTemplate.T1 __type="ContractTemplate" __ref="tmpl-1">
  <contractDisplayInfo><ContractDisplayInfo type="type-delivery">
    <title><LocID value="@t1_title" /></title><description><LocID value="@t1_desc" /></description>
  </ContractDisplayInfo></contractDisplayInfo>
  <contractProperties>
    <MissionProperty missionVariableName="PickupLocation" extendedTextToken="Location"><value>)") +
                  locationValue(ref("hub")) + R"(</value></MissionProperty>
  </contractProperties>
</ContractTemplate.T1>)");
    writeFile(r + s("contracts/contractgenerator/guild/g.xml"),
              QByteArray(R"(<ContractGenerator.G __type="ContractGenerator">
  <generators><ContractGeneratorHandler_List notForRelease="0" debugName="Handler">
    <contractParams><stringParamOverrides><ContractStringParam param="Contractor" value="@ct_contractor" /></stringParamOverrides></contractParams>
    <contracts>
      <Contract debugName="One" notForRelease="0" template="tmpl-1" minStanding="standing-2">
        <paramOverrides><propertyOverrides>
          <MissionProperty missionVariableName="PickupLocation"><value>)") +
                  locationValue(ref("outpost"), "", ref("hydrogen")) + R"(</value></MissionProperty>
        </propertyOverrides></paramOverrides>
        <contractResults contractBuyInAmount="0"><contractResults><ContractResult_CalculatedReward />
            <ContractResult_LegacyReputation><missionResults><Bool value="1" /><Bool value="0" /><Bool value="0" /></missionResults>
              <contractResultReputationAmounts factionReputation="faction-covalex" reputationScope="scope-courier" reward="rep-gain" /></ContractResult_LegacyReputation>
            <ContractResult_LegacyReputation><missionResults><Bool value="0" /><Bool value="0" /><Bool value="1" /></missionResults>
              <contractResultReputationAmounts factionReputation="faction-covalex" reputationScope="scope-courier" reward="rep-loss" /></ContractResult_LegacyReputation>
            <ContractResult_BlueprintReward><BlueprintRewards chance="0.4" blueprintPool="pool-1" /></ContractResult_BlueprintReward>
            <ContractResult_BlueprintReward><BlueprintRewards chance="0.4" blueprintPool="no-such-pool" /></ContractResult_BlueprintReward>
          </contractResults>
          <difficulty><ContractDifficulty mechanicalSkill="Some_risk_3" /></difficulty></contractResults>
      </Contract>
      <Contract debugName="Hidden" notForRelease="1" template="tmpl-1" />
      <Contract debugName="Three" notForRelease="0">
        <paramOverrides>
          <stringParamOverrides><ContractStringParam param="Title" value="@c3_title" /></stringParamOverrides>
          <propertyOverrides><MissionProperty extendedTextToken="Contractor"><value><MissionPropertyValue_Organization><matchConditions>
            <DataSetMatchCondition_TagSearch tagType="General"><tagSearch><TagSearchTerm><positiveTags><Reference value="couriers" /></positiveTags></TagSearchTerm></tagSearch></DataSetMatchCondition_TagSearch>
          </matchConditions></MissionPropertyValue_Organization></value></MissionProperty></propertyOverrides>
        </paramOverrides>
        <contractResults contractBuyInAmount="100"><contractResults><ContractResult_Reward><contractReward reward="1000" max="2000" currencyType="UEC" /></ContractResult_Reward></contractResults></contractResults>
      </Contract>
    </contracts>
  </ContractGeneratorHandler_List></generators>
</ContractGenerator.G>)");
}

const Mission *byTitle(const Catalog &catalog, const QString &title)
{
    for (const Mission &m : catalog.missions)
        if (m.title == title)
            return &m;
    return nullptr;
}

QStringList placeNames(const Catalog &catalog, const LocationSlot &slot)
{
    QStringList names;
    for (const int i : catalog.placeSets[std::size_t(slot.placeSet)])
        names << catalog.places[std::size_t(i)].name;
    return names;
}

QStringList titles(const Catalog &catalog, const std::vector<int> &shown)
{
    QStringList out;
    for (const int i : shown)
        out << catalog.missions[std::size_t(i)].title;
    return out;
}

} // namespace

class TestMissions : public QObject
{
    Q_OBJECT

    QTemporaryDir dir_;
    IniMap loc_;
    Catalog catalog_;

private slots:
    void initTestCase()
    {
        writeCache(dir_.path());
        loc_ = parseIni(s(kLoc));
        catalog_ = buildCatalog(
            {dir_.filePath(s("raw/libs/foundry/records")), dir_.filePath(s("raw/tags.tsv")), &loc_});
    }

    void tagTable()
    {
        const TagTable tags = TagTable::parse(kTags);
        QCOMPARE(tags.name(s("stanton2b")), s("Stanton2b"));
        QCOMPARE(tags.parent(s("stanton2b")), s("stanton2"));
        QCOMPARE(tags.lineage(s("stanton2b")),
                 (QStringList{s("stanton2b"), s("stanton2"), s("stanton"), s("sys"), s("root")}));
        QCOMPARE(tags.findByName(s("Stanton")), QStringList{s("stanton")});
        QCOMPARE(tags.children(s("stanton")), (QStringList{s("stanton1"), s("stanton2")}));
        QVERIFY(TagTable::load(dir_.filePath(s("missing.tsv"))).isEmpty());
    }

    void locText()
    {
        const LocText text(loc_);
        QCOMPARE(text(u"@loc_gundo"), s("Covalex Station Gundo"));
        QCOMPARE(text(u"loc_charlie"), s("Charlie Outpost")); // the key's case differs
        QCOMPARE(text(u"@loc_nope"), QString());              // CIG's placeholder
        QCOMPARE(text(u"@LOC_UNINITIALIZED"), QString());
        QCOMPARE(text(u"@missing"), QString());
    }

    void places()
    {
        QVERIFY(catalog_.hasPlaces);
        QCOMPARE(catalog_.places.size(), std::size_t(3)); // the disabled one is left out
        QCOMPARE(catalog_.places[0].name, s("Bravo Outpost"));
        QCOMPARE(catalog_.places[0].system, s("Stanton")); // from its record name
        QCOMPARE(catalog_.places[2].name, s("Covalex Station Gundo"));
        QCOMPARE(catalog_.places[2].address, s("Covalex Station Gundo around Daymar"));
        QCOMPARE(catalog_.systems, (QStringList{s("Pyro"), s("Stanton")}));
        QCOMPARE(catalog_.missions.size(), std::size_t(4)); // not-for-release entries are left out
    }

    void brokerEntry()
    {
        const Mission *m = byTitle(catalog_, s("Deliver to Covalex Station Gundo"));
        QVERIFY(m);
        QVERIFY(!m->contract);
        QCOMPARE(m->titleVariants, 2); // the missing option doesn't count
        QCOMPARE(m->description, s("Go to Covalex Station Gundo around Daymar.\nPick up at Bravo Outpost."));
        QCOMPARE(m->giver, s("Couriers Guild"));
        QCOMPARE(m->category, s("Delivery"));
        QCOMPARE(m->payout.kind, Payout::Kind::Fixed);
        QCOMPARE(m->payout.amount, 5000);
        QCOMPARE(m->payout.currency, s("UEC"));
        QCOMPARE(m->payout.buyIn, 250);
        QCOMPARE(m->systems, QStringList{s("Stanton")});
        QCOMPARE(m->file, s("missionbroker/pu_missions/delivery/a.xml"));
        // The combined value and the property inside it are one place to go.
        QCOMPARE(m->locations.size(), std::size_t(2));
        QCOMPARE(m->locations[0].token, s("Destination"));
        QCOMPARE(placeNames(catalog_, m->locations[0]), QStringList{s("Covalex Station Gundo")});
        QCOMPARE(m->locations[1].token, s("Location"));
        // A search for Stanton finds a place on Stanton 1; Charlie is on Pyro.
        QCOMPARE(placeNames(catalog_, m->locations[1]), QStringList{s("Bravo Outpost")});
        // The first outcome is success; failing and abandoning lose the same, listed once.
        QCOMPARE(m->reputation.size(), std::size_t(2));
        QCOMPARE(m->reputation[0], (ReputationReward{s("Covalex Shipping"), s("Courier"), 150, true}));
        QCOMPARE(m->reputation[1], (ReputationReward{s("Covalex Shipping"), s("Courier"), -100, false}));
        QCOMPARE(m->requiredRank, s("Jr. Runner (Courier)"));
        QVERIFY(m->blueprints.empty());
    }

    void negativeTagsAndFolderCategory()
    {
        const Mission *m = byTitle(catalog_, s("Clear the outpost"));
        QVERIFY(m);
        QCOMPARE(placeNames(catalog_, m->locations.front()),
                 QStringList{s("Bravo Outpost")}); // not the lawless one
        QCOMPARE(m->category, s("Mercenary"));
        QCOMPARE(m->payout.kind, Payout::Kind::None);
        QCOMPARE(m->locations.front().searchTags, QStringList{s("Outpost")});
    }

    void contractOverridesTemplate()
    {
        const Mission *m = byTitle(catalog_, s("Template title"));
        QVERIFY(m);
        QVERIFY(m->contract);
        QCOMPARE(m->id, s("One"));
        // The contract's search replaces the template's: an outpost that produces hydrogen.
        QCOMPARE(m->description, s("Pick up at Charlie Outpost."));
        QCOMPARE(m->locations.size(), std::size_t(1));
        QCOMPARE(m->locations[0].token, s("Location")); // kept from the template
        QCOMPARE(m->giver, s("Contract Co"));           // the generator's string param
        QCOMPARE(m->category, s("Delivery"));           // the template's type
        QCOMPARE(m->payout.kind, Payout::Kind::Calculated);
        QCOMPARE(m->difficulty, s("Combat 3/7"));
        QCOMPARE(m->systems, QStringList{s("Pyro")});
        // Reputation by its outcome flags; the rank from minStanding.
        QCOMPARE(m->reputation.size(), std::size_t(2));
        QCOMPARE(m->reputation[0], (ReputationReward{s("Covalex Shipping"), s("Courier"), 150, true}));
        QCOMPARE(m->reputation[1], (ReputationReward{s("Covalex Shipping"), s("Courier"), -100, false}));
        QCOMPARE(m->requiredRank, s("Jr. Runner (Courier)"));
        // The pool's item is named from its item record; an unknown pool is skipped.
        QCOMPARE(m->blueprints.size(), std::size_t(1));
        QCOMPARE(m->blueprints[0].label, s("Rank 2–4"));
        QCOMPARE(m->blueprints[0].chance, 0.4);
        QCOMPARE(m->blueprints[0].items, QStringList{s("Test Rifle")});
    }

    void contractWithFixedRewardAndOrganizations()
    {
        const Mission *m = byTitle(catalog_, s("Fixed job"));
        QVERIFY(m);
        QCOMPARE(m->payout.kind, Payout::Kind::Fixed);
        QCOMPARE(m->payout.amount, 1000);
        QCOMPARE(m->payout.max, 2000);
        QCOMPARE(m->payout.buyIn, 100);
        // Found by tag: either organization, so both names.
        QCOMPARE(m->giver, s("Couriers Guild / Second Couriers"));
        QCOMPARE(m->category, s("Guild")); // no type: the generator's folder
    }

    void filtering()
    {
        Filter f;
        QCOMPARE(titles(catalog_, filterMissions(catalog_, f)),
                 (QStringList{s("Clear the outpost"), s("Deliver to Covalex Station Gundo"), s("Fixed job"),
                              s("Template title")}));
        f.search = s("charlie"); // a place name only
        QCOMPARE(titles(catalog_, filterMissions(catalog_, f)), QStringList{s("Template title")});
        f.search = s("COURIERS");
        QCOMPARE(titles(catalog_, filterMissions(catalog_, f)),
                 (QStringList{s("Deliver to Covalex Station Gundo"), s("Fixed job")}));

        f = {};
        f.category = s("Delivery");
        QCOMPARE(titles(catalog_, filterMissions(catalog_, f)),
                 (QStringList{s("Deliver to Covalex Station Gundo"), s("Template title")}));
        f = {};
        f.system = s("Pyro");
        QCOMPARE(titles(catalog_, filterMissions(catalog_, f)), QStringList{s("Template title")});
        f = {};
        f.payout = Filter::PayoutKind::Calculated;
        QCOMPARE(titles(catalog_, filterMissions(catalog_, f)), QStringList{s("Template title")});
        f = {};
        f.blueprintsOnly = true;
        QCOMPARE(titles(catalog_, filterMissions(catalog_, f)), QStringList{s("Template title")});
        f = {};
        f.search = s("test rifle"); // a blueprint
        QCOMPARE(titles(catalog_, filterMissions(catalog_, f)), QStringList{s("Template title")});
        f.search = s("covalex shipping"); // a reputation faction
        QCOMPARE(titles(catalog_, filterMissions(catalog_, f)),
                 (QStringList{s("Deliver to Covalex Station Gundo"), s("Template title")}));

        f = {};
        f.sort = Filter::Sort::PayoutHigh;
        QCOMPARE(titles(catalog_, filterMissions(catalog_, f)),
                 (QStringList{s("Deliver to Covalex Station Gundo"), s("Fixed job"), s("Template title"),
                              s("Clear the outpost")}));
        f.sort = Filter::Sort::PayoutLow;
        QCOMPARE(titles(catalog_, filterMissions(catalog_, f)).first(), s("Fixed job"));
    }

    // A cache from before the catalog: missions, but no places.
    void oldCacheHasNoPlaces()
    {
        QTemporaryDir old;
        writeFile(old.filePath(s("raw/libs/foundry/records/missionbroker/pu_missions/x.xml")),
                  R"(<MissionBrokerEntry.X __type="MissionBrokerEntry" title="@c_title" />)");
        const Catalog catalog = buildCatalog(
            {old.filePath(s("raw/libs/foundry/records")), old.filePath(s("raw/tags.tsv")), &loc_});
        QVERIFY(!catalog.hasPlaces);
        QCOMPARE(catalog.missions.size(), std::size_t(1));
        QCOMPARE(catalog.missions[0].title, s("Clear the outpost"));
    }

    void cancelStops()
    {
        const std::atomic<bool> cancel = true;
        const Catalog catalog = buildCatalog(
            {dir_.filePath(s("raw/libs/foundry/records")), dir_.filePath(s("raw/tags.tsv")), &loc_}, &cancel);
        QVERIFY(catalog.missions.empty());
    }
};

QTEST_GUILESS_MAIN(TestMissions)
#include "tst_missions.moc"
