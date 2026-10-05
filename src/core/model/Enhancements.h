#pragma once

#include <QString>
#include <QStringList>

#include <span>

namespace core::enhancements {

// One generated INI. Order matters: the files are read in this order and a
// key found in two of them takes the later file's value and category.
struct File
{
    const char *id;       // "ship_descs"
    const char *fileName; // "ships_desc_enhancements.ini"
};

// One user-facing toggle on the Enhancements page and the files it controls.
struct Category
{
    const char *id; // "ship_items"
    QString label;  // table category, e.g. "Ship Items"
    QStringList fileIds;
};

std::span<const File> files();
std::span<const Category> categories();

// The category label a file's keys are shown under, or empty if unknown.
QString categoryLabelForFile(const QString &fileId);
QString fileNameFor(const QString &fileId);

} // namespace core::enhancements
