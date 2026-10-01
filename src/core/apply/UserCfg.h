#pragma once

#include <QString>

namespace core {

enum class UserCfgResult {
    Created,   // no user.cfg existed; written with just the language line
    Added,     // the language line was appended
    Updated,   // an existing g_language line was changed
    Unchanged, // already set to this language
    Failed,    // channel folder missing or the file couldn't be written
};

// Makes Star Citizen's user.cfg in `channelDir` select `scLanguageId`
// (e.g. "english", "german_(germany)"). SC's parser is lenient about spacing,
// case and quotes, so any existing g_language line is recognised and
// rewritten in place rather than duplicated. Ports ensure_user_cfg_language.
UserCfgResult ensureUserCfgLanguage(const QString &channelDir, const QString &scLanguageId);

} // namespace core
