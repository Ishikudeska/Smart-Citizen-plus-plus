#include "core/apply/LocPack.h"

#include "core/AppIdentity.h"
#include "core/EnginePaths.h"
#include "engine/zip/ZipWriter.h"

#include <QFile>
#include <QFileInfo>

namespace core {

QString defaultLocPackFilename(const QString &channel, const QDate &today)
{
    return QStringLiteral("%1-LocPack-%2-%3.zip")
        .arg(QString::fromUtf8(identity::kAppName).remove(u' '), channel, today.toString(QStringLiteral("yyyyMMdd")));
}

std::expected<qint64, QString> writeLocPackZip(const QString &sourceGlobalIni, const QString &outputZip)
{
    QFile source(sourceGlobalIni);
    if (!source.exists())
        return std::unexpected(QStringLiteral("Applied global.ini not found at %1. Apply to the game first, then export.")
                                   .arg(sourceGlobalIni));
    if (!source.open(QIODevice::ReadOnly))
        return std::unexpected(source.errorString());
    const QByteArray bytes = source.readAll();
    const std::time_t modified = QFileInfo(source).lastModified().toSecsSinceEpoch();

    auto zip = engine::zip::ZipWriter::create(fsPath(outputZip));
    if (!zip)
        return std::unexpected(errorText(zip.error()));
    // Deflate 9: loc files are repetitive text and shrink about 85%.
    if (auto r = zip->add("global.ini", std::string_view(bytes.constData(), bytes.size()), 9, modified); !r)
        return std::unexpected(errorText(r.error()));
    if (auto r = zip->finish(); !r)
        return std::unexpected(errorText(r.error()));
    return bytes.size();
}

} // namespace core
