#pragma once

#include "core/text/IniFile.h"

#include <QString>

namespace core {

// Apply-time watermarks. Both are idempotent: an earlier stamp (including
// one Smart Citizen wrote) is stripped before the current one is added, so
// re-applying or upgrading never stacks them.

// Appends "\n\n[Edited with <app> v<version>]" (literal backslash-n, the
// loc-string line break) to every Journal entry whose value differs from
// stock or is absent from it. Title-like keys (_Title, _ShortTitle,
// _SubTitle, _SubHeading, _From) are left alone. With an empty `stock`, every
// Journal entry is stamped.
void stampJournalEntries(IniMap &merged, const IniMap &stock, const QString &appName, const QString &version);

// Puts "\nLocalizations Enhanced with <app> v<version>" on its own line of
// the main menu's Frontend_PU_Version string. Skipped when the key is absent.
void stampFrontendVersion(IniMap &merged, const QString &appName, const QString &version);

inline const QString kFrontendVersionKey = QStringLiteral("Frontend_PU_Version");

} // namespace core
