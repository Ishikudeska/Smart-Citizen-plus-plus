#pragma once

#include "engine/Error.h"

#include <QString>

#include <filesystem>

// Glue between Qt strings and the Qt-free engine.
namespace core {

inline std::filesystem::path fsPath(const QString &path)
{
    return std::filesystem::path(path.toStdU16String());
}

inline QString errorText(const engine::Error &error)
{
    return QString::fromStdString(error.message);
}

} // namespace core
