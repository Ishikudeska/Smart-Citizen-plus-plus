#pragma once

#include "core/enhancements/Lookups.h"
#include "core/enhancements/RecordStore.h"
#include "core/tags/TagBuilder.h"

#include <QHash>
#include <QMap>
#include <QString>

#include <memory>

// Everything the category generators read: main()'s ctx dict.
namespace core::enh {

struct Context
{
    std::shared_ptr<const RecordStore> store;
    const Loc *loc = nullptr;    // base.ini of the language being generated
    const Loc *tagLoc = nullptr; // the English base.ini (== loc on an English run)

    ScitemLookups scitem;
    RecordLookup vehicleAmmo;
    RecordLookup fpsAmmo;
    RecordLookup controllers;
    RecordLookup armor;
    QHash<QString, qint64> reputation;
    Standings standings;

    QMap<QString, tags::TagConfig> tagConfigs;
    bool annotateMissionDescs = true;
    QString repXpLabel = QStringLiteral("Rep");
    QHash<QString, QString> missionHeaders; // details / blueprints / items / blueprint_data
    QString missionHeaderEm = QStringLiteral("EM3");
    QHash<QString, bool> missionDetailFields; // missing = shown
    QHash<QString, bool> missionTitleTags;    // missing = shown
    bool statsPrepend = false;
    bool rsOreNameAnnotations = true;

    // The user's config for a category, else the default.
    const tags::TagConfig &config(const QString &category) const;
    QString missionHeader(const QString &key) const;
    bool showField(const QString &field) const { return missionDetailFields.value(field, true); }
    bool showTitleTag(const QString &field) const { return missionTitleTags.value(field, true); }
};

} // namespace core::enh
