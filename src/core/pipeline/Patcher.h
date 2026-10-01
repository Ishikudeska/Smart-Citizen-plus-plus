#pragma once

#include "core/text/IniFile.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace core {

// Declarative fixes for CIG data bugs, applied to the DataForge cache right
// after extraction (resources/patches/**/*.patch.json):
//
//   { "target": "contracts/.../file.xml",        relative to .../records/
//     "description": "...",
//     "edits": [ { "xpath": ".//Contract[@debugName='X']//Param",
//                  "attribute": "value", "expected": "@old", "set": "@new" } ],
//     "locstring_workarounds": [ { "target": "key", "append_from": "key2",
//                                  "separator": "..." } ] }
//
// An edit only rewrites a value that still equals "expected", so a fix CIG
// ships upstream turns the patch into a no-op instead of being overwritten;
// an edit whose target already holds "set" counts as already applied.
// Patched files are written back in unforge's format. Ports
// dataforge_patcher.py.
struct PatchReport
{
    int patchesSeen = 0;
    int filesRewritten = 0;
    int editsApplied = 0;
    int editsAlreadyApplied = 0;
    int editsMismatched = 0; // current value was neither "expected" nor "set"
    int editsUnmatched = 0;  // the XPath found nothing
    QStringList errors;

    QString summary() const;
};

PatchReport applyPatches(const QString &patchRoot, const QString &recordsRoot);

// Appends one loc string's value onto another's, so a contract whose
// Description pointer is wrong in Data.p4k (which the game still reads)
// shows the intended text anyway.
struct LocstringWorkaround
{
    QString target;
    QString appendFrom;
    QString separator;
    QString description;
    QString patchFile;
};

QList<LocstringWorkaround> loadLocstringWorkarounds(const QString &patchRoot);

// Applies the workarounds whose keys are both in `entries`; already-applied
// ones are skipped. Returns how many were applied.
int applyLocstringWorkarounds(IniMap &entries, const QList<LocstringWorkaround> &workarounds);

} // namespace core
