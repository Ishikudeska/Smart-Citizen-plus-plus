// The enhancements generator's helpers against the original Python: every
// call Smart Citizen's own generator tests made (tests/fixtures/
// generator_calls.json, recorded by tools/parity/gen_generator_fixtures.py)
// is replayed here and must return the same value. Whole-file output is
// checked against real data by tests/parity/tst_parity_generator.

#include "core/enhancements/Crafting.h"
#include "core/enhancements/Missions.h"
#include "core/enhancements/Stats.h"
#include "core/enhancements/Tags.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <functional>
#include <map>

using namespace core;
using namespace core::enh;

namespace {

// ── decoding the recorder's encoding ──────────────────────────────────

struct Decoder
{
    std::vector<XmlDoc> docs; // keep parsed elements alive

    Node element(const QJsonValue &v)
    {
        const QJsonObject o = v.toObject();
        if (!o.contains(u"$el"))
            return {};
        XmlDoc doc = XmlDoc::parse(o.value(u"$el").toString().toStdString());
        if (!doc)
            qFatal("unparsable recorded element");
        const Node node =
            doc.root().root().select_node(o.value(u"path").toString().toStdString().c_str()).node();
        docs.push_back(std::move(doc));
        return node;
    }
};

QString str(const QJsonValue &v)
{
    if (v.isObject() && v.toObject().contains(u"$path"))
        return v.toObject().value(u"$path").toString();
    return v.toString();
}

QStringList strList(const QJsonValue &v)
{
    QStringList out;
    QJsonArray a = v.isObject() ? v.toObject().value(u"$set").toArray() : v.toArray();
    for (const QJsonValue &x : a)
        out << x.toString();
    return out;
}

// (key, value) pairs of a recorded str-keyed dict.
QList<std::pair<QString, QJsonValue>> items(const QJsonValue &v)
{
    QList<std::pair<QString, QJsonValue>> out;
    for (const QJsonValue &kv : v.toObject().value(u"$dict").toArray())
        out.push_back({kv[0].toString(), kv[1]});
    return out;
}

Loc loc(const QJsonValue &v)
{
    Loc out;
    for (const auto &[k, x] : items(v))
        out.insert(k, x.toString());
    return out;
}

std::optional<tags::TagConfig> config(const QJsonValue &v)
{
    if (!v.isObject() || !v.toObject().contains(u"$cfg"))
        return std::nullopt;
    return tags::TagConfig::fromObject(v.toObject().value(u"$cfg").toObject());
}

QHash<QString, qint64> intMap(const QJsonValue &v)
{
    QHash<QString, qint64> out;
    for (const auto &[k, x] : items(v))
        out.insert(k, x.toInteger());
    return out;
}

// ── encoding results the same way ─────────────────────────────────────

QJsonValue optStr(const QString &s)
{
    return s.isEmpty() ? QJsonValue() : QJsonValue(s);
}

QJsonArray list(const QStringList &l)
{
    return QJsonArray::fromStringList(l);
}

template <class Map> QJsonObject dict(const Map &m)
{
    QJsonArray a;
    for (auto it = m.begin(); it != m.end(); ++it)
        a.append(QJsonArray{it.key(), it.value()});
    return {{QStringLiteral("$dict"), a}};
}

QJsonObject dict(const Loc &m)
{
    QJsonArray a;
    for (const auto &[k, v] : m)
        a.append(QJsonArray{k, v});
    return {{QStringLiteral("$dict"), a}};
}

const char *bucketName(Spawn s)
{
    switch (s) {
    case Spawn::Hostile:
        return "hostile";
    case Spawn::Friendly:
        return "friendly";
    case Spawn::Objective:
        return "objective";
    case Spawn::Unknown:
        return "unknown";
    }
    return "";
}

QJsonObject breakdownJson(const SpawnBreakdown &b)
{
    QJsonArray a;
    for (Spawn s : {Spawn::Hostile, Spawn::Friendly, Spawn::Objective, Spawn::Unknown}) {
        QJsonArray labels;
        for (auto it = b[s].begin(); it != b[s].end(); ++it)
            labels.append(QJsonArray{it.key(), it.value()});
        a.append(
            QJsonArray{QString::fromLatin1(bucketName(s)), QJsonObject{{QStringLiteral("$dict"), labels}}});
    }
    return {{QStringLiteral("$dict"), a}};
}

SpawnBreakdown breakdownFrom(const QJsonValue &v)
{
    SpawnBreakdown b;
    for (const auto &[bucket, labels] : items(v)) {
        const Spawn s = bucket == u"hostile"     ? Spawn::Hostile
                        : bucket == u"friendly"  ? Spawn::Friendly
                        : bucket == u"objective" ? Spawn::Objective
                                                 : Spawn::Unknown;
        for (const auto &[label, count] : items(labels))
            b[s].insert(label, count.toInteger());
    }
    return b;
}

// Python dict order the C++ side doesn't keep (QHash/QMap): compare sorted.
QJsonValue sortDicts(const QJsonValue &v)
{
    if (v.isArray()) {
        QJsonArray out;
        for (const QJsonValue &x : v.toArray())
            out.append(sortDicts(x));
        return out;
    }
    if (!v.isObject())
        return v;
    QJsonObject o = v.toObject();
    if (o.contains(u"$dict")) {
        QList<QJsonArray> pairs;
        for (const QJsonValue &kv : o.value(u"$dict").toArray())
            pairs.append(QJsonArray{kv[0], sortDicts(kv[1])});
        std::sort(pairs.begin(), pairs.end(),
                  [](const QJsonArray &a, const QJsonArray &b) { return a[0].toString() < b[0].toString(); });
        QJsonArray a;
        for (const QJsonArray &p : pairs)
            a.append(p);
        o.insert(QStringLiteral("$dict"), a);
    }
    return o;
}

// ── the replay table ──────────────────────────────────────────────────

struct Replay
{
    std::function<QJsonValue(const QJsonObject &args, Decoder &d)> run;
    bool unorderedDicts = false;
};

const std::map<QString, Replay> &replays()
{
    static const std::map<QString, Replay> table = {
        {QStringLiteral("append_enhancements"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return appendEnhancements(str(a[u"existing_value"]), str(a[u"enhancements_block"]),
                                       str(a[u"separator"]), a[u"prepend"].toBool());
         }}},
        {QStringLiteral("classify_spawn_group"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             const auto [bucket, label] = classifySpawnGroup(str(a[u"name"]), str(a[u"kind"]));
             return QJsonArray{QString::fromLatin1(bucketName(bucket)), label};
         }}},
        {QStringLiteral("_extract_spawn_counts"),
         {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
              const QJsonValue ex = a[u"exclude_within"];
              const QStringList exclude = ex.isString() ? QStringList{ex.toString()} : strList(ex);
              return breakdownJson(extractSpawnCounts(d.element(a[u"element"]), exclude));
          },
          true}},
        {QStringLiteral("_format_spawn_lines"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return list(formatSpawnLines(breakdownFrom(a[u"breakdown"])));
         }}},
        {QStringLiteral("_extract_turret_info"), {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
             return optStr(extractTurretInfo(d.element(a[u"root"])));
         }}},
        {QStringLiteral("_classify_mission_engagement"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return classifyMissionEngagement(str(a[u"loc_key"]));
         }}},
        {QStringLiteral("_route_token_role"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return optStr(routeTokenRole(str(a[u"var"])));
         }}},
        {QStringLiteral("_is_route_title"),
         {[](const QJsonObject &a, Decoder &) -> QJsonValue { return isRouteTitle(str(a[u"title_key"])); }}},
        {QStringLiteral("_title_has_route_token"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return titleHasRouteToken(str(a[u"title"]));
         }}},
        {QStringLiteral("_size_abbreviation_overrides"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             const QStringList sizes = strList(a[u"shortened_sizes"]);
             return dict(
                 sizeAbbreviationOverrides(loc(a[u"loc"]), QSet<QString>(sizes.begin(), sizes.end())));
         }}},
        {QStringLiteral("_derive_route_fragment"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             const QStringList bodies = strList(a[u"desc_bodies"]);
             QList<const QString *> ptrs;
             for (const QString &b : bodies)
                 ptrs << &b;
             const auto cfg = config(a[u"cfg"]);
             RouteExpandCache cache; // some tests seed it instead of a loc
             for (const auto &[var, pair] : items(a[u"expand_cache"])) {
                 auto &[from, to] = cache[var];
                 for (const auto &[k, v] : items(pair[0]))
                     from[k] = v.toString();
                 for (const auto &[k, v] : items(pair[1]))
                     to[k] = v.toString();
             }
             return deriveRouteFragment(ptrs, cfg ? &*cfg : nullptr, loc(a[u"loc"]), &cache);
         }}},
        {QStringLiteral("_rep_reward_line"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return repRewardLine(str(a[u"field_name"]), str(a[u"amount_str"]), str(a[u"rep_xp_label"]),
                                  str(a[u"track"]));
         }}},
        {QStringLiteral("enhancements_mission"), {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
             Context ctx;
             for (const auto &[k, v] : items(a[u"show_fields"]))
                 ctx.missionDetailFields.insert(k, v.toBool());
             const QStringList ambiguous = strList(a[u"spawn_ambiguous_keys"]);
             const QSet<QString> set(ambiguous.begin(), ambiguous.end());
             return enhancementsMission(d.element(a[u"root"]), intMap(a[u"reputation_lookup"]),
                                        str(a[u"rep_xp_label"]), ctx,
                                        a[u"spawn_ambiguous_keys"].isNull() ? nullptr : &set);
         }}},
        {QStringLiteral("_build_blueprint_body_parts"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             FingerprintMap fps;
             for (const QJsonValue &kv : a[u"unique_fps"].toObject().value(u"$items").toArray()) {
                 std::vector<std::pair<QString, QString>> sources;
                 for (const QJsonValue &s : kv[1].toArray())
                     sources.push_back({s[0].toString(), s[1].toString()});
                 fps.push_back({strList(kv[0]), sources});
             }
             return list(buildBlueprintBodyParts(fps, a[u"allow_overrides"].toBool()));
         }}},
        {QStringLiteral("_name_from_blueprint_filename"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return nameFromBlueprintFilename(str(a[u"bp_xml"]));
         }}},
        {QStringLiteral("_rs_value_steps"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             QJsonArray out;
             for (int v : rsValueSteps(str(a[u"ore"])))
                 out.append(v);
             return out;
         }}},
        {QStringLiteral("_format_rs_details_lines"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return list(formatRsDetailsLines(strList(a[u"ores"]), loc(a[u"loc"])));
         }}},
        {QStringLiteral("_format_rs_tag"),
         {[](const QJsonObject &a, Decoder &) -> QJsonValue { return formatRsTag(strList(a[u"ores"])); }}},
        {QStringLiteral("_build_mineable_rs_name_overrides"),
         {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return dict(mineableRsNameOverrides(loc(a[u"loc"])));
         }}},
        {QStringLiteral("_battaglia_contract_mineable_ores"),
         {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
             return list(battagliaContractOres(d.element(a[u"contract"])));
         }}},
        {QStringLiteral("_craft_usage_key"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return optStr(craftUsageKey(str(a[u"category_path"])));
         }}},
        {QStringLiteral("_build_craft_usage_legend"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             const auto cfg = config(a[u"cfg"]);
             return craftUsageLegend(cfg ? &*cfg : nullptr);
         }}},
        {QStringLiteral("_commodity_tag"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             const auto cfg = config(a[u"cfg"]);
             return commodityTag(cfg ? &*cfg : nullptr, a[u"crafting"].toBool(), a[u"collection"].toBool(),
                                 strList(a[u"usage_keys"]));
         }}},
        {QStringLiteral("_missile_name_tag"), {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
             const auto cfg = config(a[u"config"]);
             return optStr(
                 missileNameTag(str(a[u"desc_value"]), d.element(a[u"root"]), cfg ? &*cfg : nullptr));
         }}},
        {QStringLiteral("enhancements_mining_laser"), {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
             return enhancementsMiningLaser(d.element(a[u"root"]));
         }}},
        {QStringLiteral("enhancements_salvage_tool"), {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
             return enhancementsSalvageTool(d.element(a[u"root"]));
         }}},
        {QStringLiteral("enhancements_weapon"), {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
             RecordLookup ammo;
             for (const auto &[id, el] : items(a[u"ammo_lookup"]))
                 ammo.byId.insert(id, d.element(el));
             const Loc l = loc(a[u"loc"]);
             MagazineLookup mags;
             for (const auto &[id, pair] : items(a[u"magazine_lookup"]))
                 mags.insert(id, {pair[0].toString(), pair[1].toString()});
             return enhancementsWeapon(d.element(a[u"root"]), ammo, a[u"loc"].isNull() ? nullptr : &l,
                                       a[u"magazine_lookup"].isNull() ? nullptr : &mags);
         }}},
        {QStringLiteral("bare_type_name_tag_lookup"),
         {[](const QJsonObject &a, Decoder &) -> QJsonValue {
              const auto cfg = config(a[u"comp_cfg"]);
              return dict(bareTypeNameTagLookup(loc(a[u"loc"]), cfg ? &*cfg : nullptr));
          },
          true}},
        {QStringLiteral("_synthesize_description"), {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
             return synthesizeDescription(d.element(a[u"root"]), str(a[u"xml_file"]), str(a[u"key"]));
         }}},
        {QStringLiteral("_parse_compendium_locations"),
         {[](const QJsonObject &a, Decoder &) -> QJsonValue {
              QJsonArray pairs;
              const auto parsed = parseCompendiumLocations(str(a[u"base_content"]));
              for (auto it = parsed.begin(); it != parsed.end(); ++it)
                  pairs.append(QJsonArray{it.key(), list(it.value())});
              return QJsonObject{{QStringLiteral("$dict"), pairs}};
          },
          true}},
        {QStringLiteral("_lookup_commodity_locations"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             QHash<QString, QStringList> locations;
             for (const auto &[k, v] : items(a[u"mineral_locations"]))
                 locations.insert(k, strList(v));
             const QStringList *found =
                 lookupCommodityLocations(locations, str(a[u"display"]), str(a[u"internal_name"]));
             return found ? QJsonValue(list(*found)) : QJsonValue();
         }}},
        {QStringLiteral("_strip_cig_size_prefix"),
         {[](const QJsonObject &a, Decoder &) -> QJsonValue { return stripCigSizePrefix(str(a[u"name"])); }}},
        {QStringLiteral("_pool_rank_label"),
         {[](const QJsonObject &a, Decoder &) -> QJsonValue { return poolRankLabel(str(a[u"pool_name"])); }}},
        {QStringLiteral("_normalize_commodity_name"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return normalizeCommodityName(str(a[u"raw"]));
         }}},
        {QStringLiteral("_humanize_craft_category"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             return humanizeCraftCategory(str(a[u"cat"]));
         }}},
        {QStringLiteral("_qd_size_range"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             std::vector<int> sizes;
             for (const QJsonValue &v : a[u"sizes"].toArray())
                 sizes.push_back(v.toInt());
             return qdSizeRange(sizes);
         }}},
        {QStringLiteral("_condense_crafted_items"), {[](const QJsonObject &a, Decoder &) -> QJsonValue {
             std::vector<std::pair<QString, QString>> in;
             for (const QJsonValue &v : a[u"items_list"].toArray())
                 in.push_back({v[0].toString(), v[1].toString()});
             return list(condenseCraftedItems(in));
         }}},
        {QStringLiteral("_extract_difficulty"), {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
             return extractDifficulty(d.element(a[u"element"]));
         }}},
        {QStringLiteral("_extract_mission_flags"), {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
             return list(extractMissionFlags(d.element(a[u"root"])));
         }}},
        {QStringLiteral("_extract_mission_xp"), {[](const QJsonObject &a, Decoder &d) -> QJsonValue {
             return extractMissionXp(d.element(a[u"root"]), intMap(a[u"reputation_lookup"]));
         }}},
    };
    return table;
}

QJsonObject fixture()
{
    QFile f(QStringLiteral(SC_SOURCE_DIR "/tests/fixtures/generator_calls.json"));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing generator_calls.json fixture");
    return QJsonDocument::fromJson(f.readAll()).object();
}

QByteArray compact(const QJsonValue &v)
{
    return QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
}

} // namespace

class TestGenerator : public QObject
{
    Q_OBJECT

private slots:
    void replaysPythonCalls_data()
    {
        QTest::addColumn<QString>("function");
        QTest::addColumn<QJsonObject>("args");
        QTest::addColumn<QJsonValue>("expected");
        const QJsonObject calls = fixture();
        QVERIFY(!calls.isEmpty());
        for (auto it = calls.begin(); it != calls.end(); ++it) {
            const QJsonArray list = it.value().toArray();
            for (qsizetype i = 0; i < list.size(); ++i) {
                const QJsonObject call = list[i].toObject();
                QTest::addRow("%s#%lld", qPrintable(it.key()), static_cast<long long>(i))
                    << it.key() << call.value(u"args").toObject() << call.value(u"result");
            }
        }
    }

    void replaysPythonCalls()
    {
        QFETCH(QString, function);
        QFETCH(QJsonObject, args);
        QFETCH(QJsonValue, expected);
        const auto it = replays().find(function);
        QVERIFY2(it != replays().end(), qPrintable(QStringLiteral("no replay for ") + function));
        Decoder decoder;
        QJsonValue actual;
        try {
            actual = it->second.run(args, decoder);
        } catch (const PyError &e) {
            actual = QJsonObject{{QStringLiteral("$raises"), QString::fromUtf8(e.what())}};
        }
        if (expected.toObject().contains(u"$raises")) {
            QVERIFY2(
                actual.toObject().contains(u"$raises"),
                qPrintable(QStringLiteral("python raised %1, c++ returned %2")
                               .arg(expected[u"$raises"].toString(), QString::fromUtf8(compact(actual)))));
            return;
        }
        if (it->second.unorderedDicts) {
            actual = sortDicts(actual);
            expected = sortDicts(expected);
        }
        if (actual != expected)
            qWarning("args: %s", compact(args).left(2000).constData());
        QCOMPARE(compact(actual), compact(expected));
    }

    void everyRecordedFunctionHasAReplay()
    {
        const QJsonObject calls = fixture();
        for (auto it = calls.begin(); it != calls.end(); ++it)
            QVERIFY2(replays().contains(it.key()), qPrintable(it.key()));
    }

    // PyError is raised like Python's exception, caught by reference, and
    // still a std::exception for the task runner's last-resort handler.
    void raisesPyErrors()
    {
        QCOMPARE(requireFloat(std::string_view("2.5")), 2.5);
        try {
            requireFloat(std::nullopt);
            QFAIL("float(None) did not raise");
        } catch (const PyError &e) {
            QVERIFY(QByteArray(e.what()).contains("NoneType"));
        }
        try {
            requireFloat(std::string_view("abc"));
            QFAIL("float('abc') did not raise");
        } catch (const std::exception &e) {
            QCOMPARE(QByteArray(e.what()), QByteArray("could not convert string to float"));
        }
    }
};

QTEST_GUILESS_MAIN(TestGenerator)
#include "tst_generator.moc"
