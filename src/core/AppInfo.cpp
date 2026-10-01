#include "core/AppInfo.h"

#include "core/AppIdentity.h"
#include "engine/Version.h"

#include <QCoreApplication>

namespace core {

void applyIdentity()
{
    QCoreApplication::setApplicationName(QString::fromLatin1(identity::kAppName));
    QCoreApplication::setOrganizationName(QString::fromLatin1(identity::kOrgName));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(identity::kVersion));
}

QString buildDescription()
{
    const engine::LibraryVersions libs = engine::libraryVersions();
    return QStringLiteral("%1 %2%3 (Qt %4, zstd %5, zlib %6, pugixml %7)")
        .arg(QString::fromLatin1(identity::kAppName), QString::fromLatin1(identity::kVersion),
             identity::kPortable ? QStringLiteral(" portable") : QString(),
             QString::fromLatin1(qVersion()), QString::fromStdString(libs.zstd),
             QString::fromStdString(libs.zlib), QString::fromStdString(libs.pugixml));
}

} // namespace core
