#pragma once

#include "core/tags/TagBuilder.h"

#include <QMap>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class AppController;

// The Enhancements page: which enhancement categories are generated and
// merged, the generator's options (mission body fields, stats placement,
// ship names, RS annotations, mission header texts) and the Tag Builder.
// Category toggles and Tag Builder edits are staged here and saved by
// Apply / Save Tag Changes, as on the Python page.
class EnhancementsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVariantList categories READ categories NOTIFY changed)
    Q_PROPERTY(bool categoriesDirty READ categoriesDirty NOTIFY changed)
    Q_PROPERTY(QVariantList missionFields READ missionFields NOTIFY changed)
    Q_PROPERTY(bool statsPrepend READ statsPrepend WRITE setStatsPrepend NOTIFY changed)
    Q_PROPERTY(bool standardizeShipNames READ standardizeShipNames WRITE setStandardizeShipNames NOTIFY changed)
    Q_PROPERTY(bool rsOreNames READ rsOreNames WRITE setRsOreNames NOTIFY changed)
    Q_PROPERTY(QString repXpLabel READ repXpLabel WRITE setRepXpLabel NOTIFY changed)
    Q_PROPERTY(QString headerEmTag READ headerEmTag WRITE setHeaderEmTag NOTIFY changed)
    Q_PROPERTY(bool generateDirty READ generateDirty NOTIFY changed)
    Q_PROPERTY(QString forgeStatus READ forgeStatus NOTIFY changed)
    Q_PROPERTY(bool tagDirty READ tagDirty NOTIFY tagChanged)
    Q_PROPERTY(bool annotateMissionDescs READ annotateMissionDescs WRITE setAnnotateMissionDescs NOTIFY tagChanged)
    Q_PROPERTY(int tagRevision READ tagRevision NOTIFY tagChanged)

public:
    explicit EnhancementsController(QObject *parent = nullptr);

    QVariantList categories() const;
    bool categoriesDirty() const;
    QVariantList missionFields() const;
    bool statsPrepend() const;
    void setStatsPrepend(bool on);
    bool standardizeShipNames() const;
    void setStandardizeShipNames(bool on);
    bool rsOreNames() const;
    void setRsOreNames(bool on);
    QString repXpLabel() const;
    void setRepXpLabel(const QString &label);
    QString headerEmTag() const;
    void setHeaderEmTag(const QString &tag);
    bool generateDirty() const;
    QString forgeStatus() const;
    bool tagDirty() const;
    bool annotateMissionDescs() const { return annotate_; }
    void setAnnotateMissionDescs(bool on);
    int tagRevision() const { return revision_; }

    Q_INVOKABLE void setCategoryEnabled(const QString &id, bool on);
    Q_INVOKABLE void applyCategories();
    Q_INVOKABLE void setMissionField(const QString &field, bool on);
    Q_INVOKABLE QString missionHeader(const QString &key) const;
    Q_INVOKABLE void setMissionHeader(const QString &key, const QString &text);
    Q_INVOKABLE void generate();

    // ── Tag Builder ──
    Q_INVOKABLE QVariantList tagCategories() const; // [{id, label}]
    Q_INVOKABLE QVariantList elements(const QString &category) const;
    Q_INVOKABLE void setElementEnabled(const QString &category, int index, bool on);
    Q_INVOKABLE void setElementStyle(const QString &category, int index, const QString &style);
    Q_INVOKABLE void moveElement(const QString &category, int index, int delta);
    // Field names: separator, enclosing, placement, usageSeparator,
    // routeArrow, titleSeparator, locationDetail, rankSeparator.
    Q_INVOKABLE QString option(const QString &category, const QString &field) const;
    Q_INVOKABLE void setOption(const QString &category, const QString &field, const QString &value);
    Q_INVOKABLE QVariantList choices(const QString &field) const; // [{key, label}]
    Q_INVOKABLE bool flag(const QString &category, const QString &field) const; // standardizeHauling, route
    Q_INVOKABLE void setFlag(const QString &category, const QString &field, bool on);
    Q_INVOKABLE QVariantList phraseOptions() const; // [{key, label}] shorten + remove + underline
    Q_INVOKABLE bool phraseEnabled(const QString &key) const;
    Q_INVOKABLE void setPhraseEnabled(const QString &key, bool on);
    Q_INVOKABLE bool sizesShortened() const;
    Q_INVOKABLE void setSizesShortened(bool on);
    Q_INVOKABLE bool titleTag(const QString &field) const;
    Q_INVOKABLE void setTitleTag(const QString &field, bool on);
    Q_INVOKABLE QString preview(const QString &category) const;
    // The Short/Medium/Long texts of a mapped element kind.
    Q_INVOKABLE QVariantList mapping(const QString &category, const QString &kind) const;
    Q_INVOKABLE void setMappingText(const QString &category, const QString &raw, int column, const QString &text);
    Q_INVOKABLE void resetMapping(const QString &category, const QString &kind);
    Q_INVOKABLE void resetTagDefaults();
    Q_INVOKABLE void saveTagChanges();

signals:
    void changed();
    void tagChanged();

private:
    AppController &app() const;
    core::tags::TagConfig &config(const QString &category);
    const core::tags::TagConfig &config(const QString &category) const;
    void touchTags();
    void markGenerateDirty();
    QStringList mappedValues(const QString &category, const QString &kind) const;

    QMap<QString, bool> stagedCategories_;
    QMap<QString, core::tags::TagConfig> configs_;
    QMap<QString, core::tags::TagConfig> saved_;
    bool annotate_ = true, savedAnnotate_ = true;
    QMap<QString, bool> titleTags_, savedTitleTags_;
    bool generateDirty_ = false;
    int revision_ = 0;
};
