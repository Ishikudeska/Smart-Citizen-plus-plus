// scxmissions: the mission catalog outside the app, for checking it against
// real game data.
//
//   scxmissions forge <Data.p4k> <cache-dir>        DataForge cache, as the app builds it
//   scxmissions dump <cache-dir> <base.ini> [out.tsv]   the catalog as TSV, with a summary

#include "core/missions/Catalog.h"
#include "core/pipeline/Extraction.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <cstdio>
#include <map>

using namespace core;
using namespace core::missions;

namespace {

int usage()
{
    std::fputs("usage:\n"
               "  scxmissions forge <Data.p4k> <cache-dir>\n"
               "  scxmissions dump <cache-dir> <base.ini> [out.tsv]\n",
               stderr);
    return 2;
}

int forge(const QString &p4k, const QString &cache)
{
    auto archive = engine::p4k::Archive::open(std::filesystem::path(p4k.toStdU16String()));
    if (!archive) {
        std::fprintf(stderr, "error: %s\n", archive.error().message.c_str());
        return 1;
    }
    QElapsedTimer timer;
    timer.start();
    const auto result = extractDataForge(**archive, cache, QStringLiteral(SC_PATCHES_DIR));
    if (!result) {
        std::fprintf(stderr, "error: %s\n", result.error().message.c_str());
        return 1;
    }
    std::fprintf(stderr, "%zu records in %.1fs\n", result->records, timer.elapsed() / 1000.0);
    return 0;
}

QString payoutText(const Payout &p)
{
    switch (p.kind) {
    case Payout::Kind::Fixed:
        return p.max > p.amount ? QStringLiteral("%1-%2 %3").arg(p.amount).arg(p.max).arg(p.currency)
                                : QStringLiteral("%1 %2").arg(p.amount).arg(p.currency);
    case Payout::Kind::Calculated:
        return QStringLiteral("calculated");
    case Payout::Kind::None:
        break;
    }
    return QStringLiteral("none");
}

QString oneLine(QString s)
{
    return s.replace(u'\n', QStringLiteral("\\n")).replace(u'\t', u' ');
}

int dump(const QString &cache, const QString &baseIni, const QString &out)
{
    const IniMap loc = loadIni(baseIni);
    QElapsedTimer timer;
    timer.start();
    const Catalog catalog = buildCatalog({dataForgeRecordsDir(cache), dataForgeTagTablePath(cache), &loc});
    std::fprintf(stderr, "built in %.2fs: %zu missions, %zu places, %zu place sets, places: %s\n",
                 timer.elapsed() / 1000.0, catalog.missions.size(), catalog.places.size(),
                 catalog.placeSets.size(), catalog.hasPlaces ? "yes" : "no");

    std::map<QString, int> byKind, byCategory;
    int withPlaces = 0, broadOnly = 0, noSlots = 0, unmatched = 0, tokensLeft = 0, noSystem = 0;
    int withRep = 0, repNoFaction = 0, withBlueprints = 0, withRank = 0;
    for (const Mission &m : catalog.missions) {
        const char *kind = m.payout.kind == Payout::Kind::Fixed        ? "fixed"
                           : m.payout.kind == Payout::Kind::Calculated ? "calculated"
                                                                       : "none";
        ++byKind[QStringLiteral("%1 %2").arg(m.contract ? "contract" : "broker", kind)];
        ++byCategory[m.category];
        if (m.locations.empty())
            ++noSlots;
        bool any = false, narrow = false;
        for (const LocationSlot &s : m.locations) {
            const std::size_t n = catalog.placeSets[std::size_t(s.placeSet)].size();
            if (n == 0)
                ++unmatched;
            any = any || n > 0;
            narrow = narrow || (n > 0 && n <= 25);
        }
        withPlaces += any;
        broadOnly += any && !narrow;
        tokensLeft += m.description.contains(u'[') || m.title.contains(u'[');
        noSystem += m.systems.isEmpty();
        withRep += !m.reputation.empty();
        repNoFaction += std::any_of(m.reputation.begin(), m.reputation.end(),
                                    [](const ReputationReward &r) { return r.faction.isEmpty(); });
        withBlueprints += !m.blueprints.empty();
        withRank += !m.requiredRank.isEmpty();
    }
    std::fprintf(stderr, "reputation %d (no faction name %d), blueprints %d, required rank %d\n", withRep,
                 repNoFaction, withBlueprints, withRank);
    std::fprintf(stderr,
                 "with places %d, only broad (>25) %d, no slots %d, unmatched slots %d, "
                 "unfilled tokens %d, no system %d\n",
                 withPlaces, broadOnly, noSlots, unmatched, tokensLeft, noSystem);
    for (const auto &[k, n] : byKind)
        std::fprintf(stderr, "  %-28s %d\n", qPrintable(k), n);
    std::fprintf(stderr, "categories:");
    for (const auto &[k, n] : byCategory)
        std::fprintf(stderr, " %s=%d", qPrintable(k.isEmpty() ? QStringLiteral("(none)") : k), n);
    std::fprintf(stderr, "\nsystems: %s\n", qPrintable(catalog.systems.join(u", ")));

    if (out.isEmpty())
        return 0;
    QFile file(out);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "cannot write %s\n", qPrintable(out));
        return 1;
    }
    QTextStream ts(&file);
    ts << "id\tsource\tcategory\ttitle\tgiver\tpayout\tbuy-"
          "in\tsystems\tdifficulty\trank\treputation\tblueprints\tlocations\tdescription\tfile\n";
    for (const Mission &m : catalog.missions) {
        QStringList slotTexts;
        for (const LocationSlot &s : m.locations) {
            const auto &set = catalog.placeSets[std::size_t(s.placeSet)];
            QStringList names;
            for (std::size_t i = 0; i < set.size() && i < 5; ++i)
                names << catalog.places[std::size_t(set[i])].name;
            slotTexts << QStringLiteral("%1(%2)=%3%4")
                             .arg(s.token.isEmpty() ? s.variable : s.token)
                             .arg(set.size())
                             .arg(names.join(u'|'))
                             .arg(set.empty() ? QStringLiteral(" tags:") + s.searchTags.join(u',')
                                              : QString());
        }
        QStringList rep, bps;
        for (const ReputationReward &r : m.reputation)
            rep << QStringLiteral("%1%2 %3/%4")
                       .arg(r.success ? "" : "fail:")
                       .arg(r.amount)
                       .arg(r.faction, r.scope);
        for (const BlueprintReward &b : m.blueprints)
            bps << QStringLiteral("%1@%2=%3").arg(b.label).arg(b.chance).arg(b.items.join(u'|'));
        ts << m.id << '\t' << (m.contract ? "contract" : "broker") << '\t' << m.category << '\t'
           << oneLine(m.title) << '\t' << oneLine(m.giver) << '\t' << payoutText(m.payout) << '\t'
           << m.payout.buyIn << '\t' << m.systems.join(u',') << '\t' << m.difficulty << '\t' << m.requiredRank
           << '\t' << rep.join(QStringLiteral("; ")) << '\t' << bps.join(QStringLiteral("; ")) << '\t'
           << slotTexts.join(QStringLiteral("; ")) << '\t' << oneLine(m.description) << '\t' << m.file
           << '\n';
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() >= 4 && args[1] == u"forge")
        return forge(args[2], args[3]);
    if (args.size() >= 4 && args[1] == u"dump")
        return dump(args[2], args[3], args.value(4));
    return usage();
}
