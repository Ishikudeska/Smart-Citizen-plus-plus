// Runs the C++ enhancements generator outside the app, for parity runs
// against tools/parity/run_python_generator.py (same options JSON).
//
//   scxgen --base-ini <base.ini> --forge-dir <dataforge> --out <dir>
//          [--options opts.json] [--patches <dir>] [--threads N]

#include "core/enhancements/Generator.h"
#include "core/log/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>

using namespace core;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString baseIni, forgeDir, outDir, optionsPath, patches;
    int threads = 0;
    const QStringList args = app.arguments();
    for (qsizetype i = 1; i + 1 < args.size(); i += 2) {
        const QString &flag = args[i];
        const QString &value = args[i + 1];
        if (flag == u"--base-ini")
            baseIni = value;
        else if (flag == u"--forge-dir")
            forgeDir = value;
        else if (flag == u"--out")
            outDir = value;
        else if (flag == u"--options")
            optionsPath = value;
        else if (flag == u"--patches")
            patches = value;
        else if (flag == u"--threads")
            threads = value.toInt();
    }
    if (baseIni.isEmpty() || forgeDir.isEmpty() || outDir.isEmpty()) {
        std::fputs("usage: scxgen --base-ini <file> --forge-dir <dir> --out <dir> [--options json] "
                   "[--patches dir]\n",
                   stderr);
        return 2;
    }

    QJsonObject opts;
    if (!optionsPath.isEmpty()) {
        QFile f(optionsPath);
        if (!f.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "cannot read %s\n", qPrintable(optionsPath));
            return 2;
        }
        opts = QJsonDocument::fromJson(f.readAll()).object();
    }

    // main() writes beside its base.ini: give it a private copy.
    QDir().mkpath(outDir);
    const QString copy = QDir(outDir).filePath(QStringLiteral("base.ini"));
    QFile::remove(copy);
    if (!QFile::copy(baseIni, copy)) {
        std::fprintf(stderr, "cannot copy %s\n", qPrintable(baseIni));
        return 1;
    }

    enh::GeneratorOptions options;
    for (const QString &category : tags::kCategories)
        options.tagConfigs.insert(category, tags::defaultConfig(category));
    options.missionTitleTags.insert(QStringLiteral("rep_track"), false); // the app's default
    enh::applyOptionsJson(options, opts);
    options.baseIni = copy;
    options.forgeDir = forgeDir;
    options.patchesDir = patches;
    options.threads = threads;

    log::LogHub::instance().install({}, qEnvironmentVariableIsSet("SCX_DEBUG"));
    QElapsedTimer timer;
    timer.start();
    const auto result = enh::generateEnhancements(options);
    if (!result) {
        std::fprintf(stderr, "error: %s\n", qPrintable(result.error()));
        return 1;
    }
    std::printf("generated %lld entries in %.1fs -> %s\n", static_cast<long long>(result->entries),
                timer.elapsed() / 1000.0, qPrintable(outDir));
    return 0;
}
