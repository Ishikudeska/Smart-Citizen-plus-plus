#pragma once

#include "core/enhancements/Xml.h"
#include "core/text/IniFile.h"

#include <QString>
#include <QStringView>

#include <optional>
#include <string_view>

// Shared pieces of the enhancements generator (scripts/
// generate_enhancements_ini.py): the section separators, stat number
// formatting, loc-key extraction from records. Values use the in-INI
// literal "\n" (backslash, n) as their line break.
namespace core::enh {

// base.ini, and every output dict: insertion-ordered like a Python dict.
using Loc = IniMap;

inline const QString kNl = QStringLiteral("\\n");
inline const QString kEnhancementSeparator = QStringLiteral("\\n\\n--- STATS ---\\n");
inline const QString kEffectSeparator = QStringLiteral("\\n\\n--- EFFECT ---\\n");
inline const QString kStatsPrependSeparator = QStringLiteral("\\n\\n------\\n\\n");
inline constexpr double kOverheatPlaceholder = 450'000; // no real overheat stat
inline constexpr std::string_view kNullUuid = "00000000-0000-0000-0000-000000000000";

// "Eckhart_Nyx_DefendShip" -> "Eckhart Nyx DefendShip".
QString humanizeKey(QStringView key);

// The stats block after (or, with `prepend`, before) the prose, replacing
// any stats/mission-details block from an earlier run.
QString appendEnhancements(const QString &existing, const QString &block,
                           const QString &separator = kEnhancementSeparator, bool prepend = false);

// _fmt: "?" for nothing; float(value) as "1,234" (rounded half to even) or
// with `decimals` places, then the unit; text that isn't a number as is.
QString fmt(const std::optional<std::string_view> &value, QStringView unit = {}, int decimals = 0);
QString fmt(std::optional<double> value, QStringView unit = {}, int decimals = 0);
inline QString fmt(std::string_view value, QStringView unit = {}, int decimals = 0)
{
    return fmt(std::optional(value), unit, decimals);
}

// float(attribute): nothing when absent or not a number.
std::optional<double> toFloat(const std::optional<std::string_view> &value);
std::optional<double> toFloat(std::string_view value);

// CIG's LOC_UNINITIALIZED & co. ("@"-prefixed or not); an empty ref counts.
bool isSentinelLocRef(std::string_view ref);
bool isSentinelKey(QStringView key);
bool isPlaceholderText(QStringView s);

// The first Localization element's @Description / @Name key ("" if none).
QString locKey(Node root);
QString locNameKey(Node root);
// The description attribute of a MissionBrokerEntry record.
QString missionLocKey(Node root);

// A stand-in description for an entity base.ini doesn't know.
QString synthesizeDescription(Node root, const QString &xmlFile, const QString &key);

// "COOL_AEGS_S01_Bracer_SCItem" -> "S1".
QString extractItemSize(QStringView cls);

// __polymorphicType, else the element's tag.
std::string_view polyType(Node el);

// The root tag's part after the first '.' (tag.split(".", 1)[1]), else
// `fallback`; and after the last '.' (tag.split(".")[-1]), else the tag.
QString recordClassName(Node root, const QString &fallback);
QString lastDotPart(std::string_view tag);

// The file name without its extension, as Path.stem.
QString fileStem(const QString &path);

} // namespace core::enh
