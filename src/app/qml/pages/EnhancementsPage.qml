import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ScApp

// The Enhancements page: categories and generation, generator options,
// mission header labels and the Tag Builder.
Item {
    id: page

    EnhancementsController { id: enh }

    // A combo over [{key, label}] bound to one Tag Builder option.
    component OptionCombo: ComboBox {
        id: combo
        property string category
        property string field
        property string choicesField: field
        implicitWidth: 190
        textRole: "label"
        valueRole: "key"
        model: enh.choices(choicesField)
        currentIndex: { enh.tagRevision; const v = enh.option(category, field); return model.findIndex(o => o.key === v) }
        onActivated: enh.setOption(category, field, currentValue)
    }

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: Math.min(scroller.availableWidth - 32, 1200)
            x: 16
            spacing: 14

            Item { implicitHeight: 4 }
            Label { text: qsTr("enhancements.title"); color: Theme.title; font.pixelSize: 20; font.bold: true }
            Label { text: qsTr("enhancements.desc"); color: Theme.dim; wrapMode: Text.WordWrap; Layout.fillWidth: true }

            // ── categories ──
            Section {
                Layout.fillWidth: true
                title: qsTr("enhancements.enhancements_group")
                description: qsTr("enhancements.enhancements_desc")
                objectName: "enhancementCategories"

                GridLayout {
                    columns: 2
                    columnSpacing: 24
                    rowSpacing: 4
                    Layout.fillWidth: true
                    Repeater {
                        model: enh.categories
                        delegate: RowLayout {
                            required property var modelData
                            spacing: 6
                            Layout.fillWidth: true
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: !modelData.enabled ? Theme.disabled
                                     : modelData.generated ? Theme.action("apply") : Theme.action("restore")
                                ToolTip.visible: dotHover.hovered
                                ToolTip.text: modelData.generated ? qsTr("scx.category_generated") : qsTr("scx.category_missing")
                                HoverHandler { id: dotHover }
                            }
                            AppCheckBox {
                                text: modelData.label
                                checked: modelData.enabled
                                onToggled: enh.setCategoryEnabled(modelData.id, checked)
                            }
                            Label { text: modelData.description; color: Theme.dim; font.pixelSize: Theme.smallFont; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                    }
                }

                Label { text: qsTr("enhancements.mission_detail_fields_heading"); color: Theme.text; font.bold: true }
                Flow {
                    Layout.fillWidth: true
                    spacing: 10
                    Repeater {
                        model: enh.missionFields
                        delegate: AppCheckBox {
                            required property var modelData
                            text: modelData.label
                            tip: modelData.tooltip
                            checked: modelData.enabled
                            onToggled: enh.setMissionField(modelData.id, checked)
                        }
                    }
                }
                Label { text: qsTr("enhancements.mission_detail_fields_note"); color: Theme.dim; font.pixelSize: Theme.smallFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }

                AppCheckBox { text: qsTr("enhancements.stats_prepend_cb"); tip: qsTr("enhancements.stats_prepend_tooltip"); checked: enh.statsPrepend; onToggled: enh.statsPrepend = checked }
                AppCheckBox { text: qsTr("enhancements.standardize_ship_names_cb"); tip: qsTr("enhancements.standardize_ship_names_tooltip"); checked: enh.standardizeShipNames; onToggled: enh.standardizeShipNames = checked }
                AppCheckBox { text: qsTr("enhancements.rs_ore_name_annotations_cb"); tip: qsTr("enhancements.rs_ore_name_annotations_tooltip"); checked: enh.rsOreNames; onToggled: enh.rsOreNames = checked }

                RowLayout {
                    spacing: 8
                    AppButton {
                        text: qsTr("enhancements.apply_btn")
                        enabled: enh.categoriesDirty && !App.busy
                        tip: qsTr("enhancements.apply_categories_tooltip")
                        onClicked: enh.applyCategories()
                    }
                    AppButton {
                        objectName: "generateButton"
                        text: qsTr("enhancements.generate_btn")
                        role: enh.generateDirty ? "needsApply" : "apply"
                        enabled: !App.busy
                        tip: enh.generateDirty ? qsTr("enhancements.generate_enabled_tooltip") : qsTr("enhancements.generate_disabled_tooltip")
                        onClicked: enh.generate()
                    }
                    Label { text: enh.forgeStatus; color: Theme.dim; font.pixelSize: Theme.smallFont }
                }
            }

            // ── mission labels ──
            Section {
                Layout.fillWidth: true
                objectName: "missionLabels"
                title: qsTr("enhancements.mission_labels_group")

                GridLayout {
                    columns: 4
                    columnSpacing: 10
                    rowSpacing: 6
                    Layout.fillWidth: true
                    Repeater {
                        model: [
                            { key: "details", label: qsTr("enhancements.mission_label_details") },
                            { key: "blueprints", label: qsTr("enhancements.mission_label_blueprints") },
                            { key: "items", label: qsTr("enhancements.mission_label_item_rewards") },
                            { key: "blueprint_data", label: qsTr("enhancements.mission_label_blueprint_data") }
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.columnSpan: 2
                            Label { text: modelData.label; color: Theme.text; Layout.preferredWidth: 110 }
                            TextField {
                                Layout.fillWidth: true
                                text: enh.missionHeader(modelData.key)
                                color: Theme.text
                                background: Rectangle { color: Theme.base; border.color: Theme.border; radius: Theme.radius }
                                onEditingFinished: enh.setMissionHeader(modelData.key, text)
                            }
                        }
                    }
                    RowLayout {
                        Layout.columnSpan: 2
                        Label { text: qsTr("enhancements.mission_label_xp"); color: Theme.text; Layout.preferredWidth: 110 }
                        TextField {
                            Layout.fillWidth: true
                            text: enh.repXpLabel
                            color: Theme.text
                            background: Rectangle { color: Theme.base; border.color: Theme.border; radius: Theme.radius }
                            onEditingFinished: enh.repXpLabel = text
                            ToolTip.visible: hovered; ToolTip.text: qsTr("enhancements.rep_xp_label_tooltip"); ToolTip.delay: 800
                        }
                    }
                    RowLayout {
                        Layout.columnSpan: 2
                        Label { text: qsTr("enhancements.mission_label_header_style"); color: Theme.text; Layout.preferredWidth: 110 }
                        ComboBox {
                            implicitWidth: 180
                            textRole: "label"
                            valueRole: "key"
                            model: [{ key: "EM3", label: qsTr("enhancements.em_label_underline") }, { key: "EM4", label: qsTr("enhancements.em_label_blue_text") }]
                            currentIndex: enh.headerEmTag === "EM4" ? 1 : 0
                            onActivated: enh.headerEmTag = currentValue
                            ToolTip.visible: hovered; ToolTip.text: qsTr("enhancements.header_em_tooltip"); ToolTip.delay: 800
                        }
                    }
                }
            }

            // ── Tag Builder ──
            Section {
                id: tagSection
                Layout.fillWidth: true
                title: qsTr("enhancements.tag_builder_group")
                description: qsTr("enhancements.tag_builder_desc")
                objectName: "tagBuilder"

                TabBar {
                    id: tagTabs
                    Layout.fillWidth: true
                    background: Rectangle { color: "transparent" }
                    Repeater {
                        model: enh.tagCategories()
                        delegate: TabButton {
                            required property var modelData
                            text: modelData.label
                            width: implicitWidth + 20
                            contentItem: Label { text: parent.text; color: parent.checked ? Theme.title : Theme.text; font.bold: parent.checked; horizontalAlignment: Text.AlignHCenter }
                            background: Rectangle { color: parent.checked ? Theme.window : Theme.alternateBase; border.color: Theme.border; radius: Theme.radius }
                        }
                    }
                }
                readonly property string category: enh.tagCategories()[tagTabs.currentIndex].id

                // Element tags (components, missiles, ship weapons, commodities).
                ColumnLayout {
                    visible: tagSection.category !== "mission_titles"
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: { enh.tagRevision; return enh.elements(tagSection.category) }
                        delegate: RowLayout {
                            required property int index
                            required property var modelData
                            readonly property string category: tagSection.category
                            spacing: 8
                            AppCheckBox {
                                text: modelData.label
                                checked: modelData.enabled
                                tip: qsTr("enhancements.tag_untick_exclude_tooltip")
                                onToggled: enh.setElementEnabled(category, index, checked)
                                Layout.preferredWidth: 150
                            }
                            ComboBox {
                                implicitWidth: 190
                                visible: modelData.styles.length > 0
                                textRole: "label"
                                valueRole: "key"
                                model: modelData.styles
                                currentIndex: modelData.styles.findIndex(s => s.key === modelData.style)
                                onActivated: enh.setElementStyle(category, index, currentValue)
                            }
                            AppButton { text: "▲"; enabled: index > 0; tip: qsTr("enhancements.tag_move_up_tooltip"); onClicked: enh.moveElement(category, index, -1) }
                            AppButton { text: "▼"; tip: qsTr("enhancements.tag_move_down_tooltip"); onClicked: enh.moveElement(category, index, 1) }
                            AppButton {
                                visible: modelData.mapped
                                text: qsTr("enhancements.tag_edit_mapping_row_btn")
                                onClicked: { mappingEditor.category = category; mappingEditor.kind = modelData.kind; mappingEditor.open() }
                            }
                        }
                    }
                    RowLayout {
                        spacing: 8
                        Label { text: qsTr("enhancements.tag_separator_label"); color: Theme.text }
                        OptionCombo { category: tagSection.category; field: "separator" }
                        Label { text: qsTr("enhancements.tag_enclosing_label"); color: Theme.text }
                        OptionCombo { category: tagSection.category; field: "enclosing" }
                        Label { text: qsTr("enhancements.tag_placement_label"); color: Theme.text }
                        OptionCombo { category: tagSection.category; field: "placement" }
                    }
                    RowLayout {
                        visible: tagSection.category === "commodities"
                        Label { text: qsTr("enhancements.tag_craft_usage_separator_label"); color: Theme.text }
                        OptionCombo { category: "commodities"; field: "usageSeparator" }
                    }
                }

                // Mission titles.
                ColumnLayout {
                    visible: tagSection.category === "mission_titles"
                    Layout.fillWidth: true
                    spacing: 6
                    readonly property string cat: "mission_titles"

                    Label { text: qsTr("enhancements.mt_hauling_group"); color: Theme.title; font.bold: true }
                    AppCheckBox { text: qsTr("enhancements.mt_enable_route_cb"); checked: { enh.tagRevision; return enh.flag("mission_titles", "route") } onToggled: enh.setFlag("mission_titles", "route", checked) }
                    AppCheckBox { text: qsTr("enhancements.mt_standardize_cb"); checked: { enh.tagRevision; return enh.flag("mission_titles", "standardizeHauling") } onToggled: enh.setFlag("mission_titles", "standardizeHauling", checked) }
                    Label { text: qsTr("enhancements.mt_route_hint"); color: Theme.dim; font.pixelSize: Theme.smallFont }
                    GridLayout {
                        columns: 4
                        columnSpacing: 10
                        Label { text: qsTr("enhancements.tag_placement_label"); color: Theme.text }
                        OptionCombo { category: "mission_titles"; field: "placement"; choicesField: "missionPlacement" }
                        Label { text: qsTr("enhancements.mt_route_arrow_label"); color: Theme.text }
                        OptionCombo { category: "mission_titles"; field: "routeArrow" }
                        Label { text: qsTr("enhancements.mt_title_separator_label"); color: Theme.text }
                        OptionCombo { category: "mission_titles"; field: "titleSeparator" }
                        Label { text: qsTr("enhancements.mt_location_detail_label"); color: Theme.text }
                        OptionCombo { category: "mission_titles"; field: "locationDetail" }
                        Label { text: qsTr("enhancements.mt_rank_separator_label"); color: Theme.text }
                        OptionCombo { category: "mission_titles"; field: "rankSeparator" }
                    }
                    Label { text: qsTr("enhancements.mt_shorten_titles_cb"); color: Theme.text; font.bold: true }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: enh.phraseOptions()
                            delegate: AppCheckBox {
                                required property var modelData
                                text: modelData.label
                                checked: { enh.tagRevision; return enh.phraseEnabled(modelData.key) }
                                onToggled: enh.setPhraseEnabled(modelData.key, checked)
                            }
                        }
                    }
                    AppCheckBox { text: qsTr("enhancements.mt_shorten_sizes_cb"); checked: { enh.tagRevision; return enh.sizesShortened() } onToggled: enh.setSizesShortened(checked) }
                    AppCheckBox { text: qsTr("enhancements.mt_underline_direct_cb"); checked: { enh.tagRevision; return enh.phraseEnabled("underline_direct") } onToggled: enh.setPhraseEnabled("underline_direct", checked) }

                    Label { text: qsTr("enhancements.mt_general_tags_group"); color: Theme.title; font.bold: true }
                    Label { text: qsTr("enhancements.mt_general_tags_hint"); color: Theme.dim; font.pixelSize: Theme.smallFont }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        Repeater {
                            model: [
                                { key: "rep", label: qsTr("enhancements.mt_tag_rep_cb") },
                                { key: "blueprint", label: qsTr("enhancements.mt_tag_blueprint_cb") },
                                { key: "ace", label: qsTr("enhancements.mt_tag_ace_cb") },
                                { key: "rep_track", label: qsTr("enhancements.mt_tag_rep_track_cb") }
                            ]
                            delegate: AppCheckBox {
                                required property var modelData
                                text: modelData.label
                                checked: { enh.tagRevision; return enh.titleTag(modelData.key) }
                                onToggled: enh.setTitleTag(modelData.key, checked)
                            }
                        }
                    }
                    Label { text: qsTr("enhancements.mt_scanning_group"); color: Theme.title; font.bold: true }
                    Label { text: qsTr("enhancements.mt_scanning_hint"); color: Theme.dim; font.pixelSize: Theme.smallFont; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    AppCheckBox { text: qsTr("enhancements.mt_tag_rs_cb"); checked: { enh.tagRevision; return enh.titleTag("rs") } onToggled: enh.setTitleTag("rs", checked) }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: previewText.implicitHeight + 16
                    color: Theme.base
                    border.color: Theme.border
                    radius: Theme.radius
                    Text {
                        id: previewText
                        anchors.fill: parent
                        anchors.margins: 8
                        textFormat: Text.StyledText
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: Theme.fontSize + 1
                        text: { enh.tagRevision; return enh.preview(tagSection.category).replace(/</g, "&lt;").replace(/&lt;(\/?)u>/g, "<$1u>") }
                    }
                }

                AppCheckBox {
                    text: qsTr("enhancements.annotate_mission_descs_cb")
                    tip: qsTr("enhancements.annotate_mission_descs_tooltip")
                    checked: enh.annotateMissionDescs
                    onToggled: enh.annotateMissionDescs = checked
                }
                RowLayout {
                    AppButton { text: qsTr("enhancements.reset_defaults_btn"); tip: qsTr("enhancements.reset_tag_tooltip"); onClicked: enh.resetTagDefaults() }
                    AppButton {
                        text: qsTr("enhancements.apply_tag_changes_btn")
                        role: enh.tagDirty ? "needsApply" : ""
                        enabled: enh.tagDirty && !App.busy
                        tip: enh.tagDirty ? qsTr("enhancements.tag_enabled_tooltip") : qsTr("enhancements.tag_disabled_tooltip")
                        onClicked: enh.saveTagChanges()
                    }
                }
            }
            Item { implicitHeight: 12 }
        }
    }

    // Short / Medium / Long texts for one mapped element kind.
    Popup {
        id: mappingEditor
        property string category
        property string kind
        anchors.centerIn: Overlay.overlay
        width: Math.min(760, page.width - 40)
        height: Math.min(560, page.height - 40)
        modal: true
        padding: 16
        background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 8
            Label { text: qsTr("enhancements.edit_mapping_btn").replace("...", "") + " — " + mappingEditor.kind; color: Theme.title; font.bold: true; font.pixelSize: 15 }
            Label { text: qsTr("tag_mapping_dialog.hint"); color: Theme.dim; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Label { text: qsTr("tag_mapping_dialog.col_value"); color: Theme.text; font.bold: true; Layout.preferredWidth: 200 }
                Label { text: qsTr("tag_mapping_dialog.col_short"); color: Theme.text; font.bold: true; Layout.fillWidth: true }
                Label { text: qsTr("tag_mapping_dialog.col_medium"); color: Theme.text; font.bold: true; Layout.fillWidth: true }
                Label { text: qsTr("tag_mapping_dialog.col_long"); color: Theme.text; font.bold: true; Layout.fillWidth: true }
            }
            ListView {
                id: mappingList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: { enh.tagRevision; return mappingEditor.visible ? enh.mapping(mappingEditor.category, mappingEditor.kind) : [] }
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    required property var modelData
                    width: mappingList.width - 12
                    Label { text: modelData.value; color: Theme.text; Layout.preferredWidth: 200; elide: Text.ElideRight }
                    Repeater {
                        model: ["short", "med", "long"]
                        delegate: TextField {
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            text: parent.modelData[modelData]
                            color: Theme.text
                            background: Rectangle { color: Theme.base; border.color: Theme.border; radius: Theme.radius }
                            onEditingFinished: enh.setMappingText(mappingEditor.category, parent.modelData.value, index, text)
                        }
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AppButton { text: qsTr("tag_mapping_dialog.reset_btn"); tip: qsTr("tag_mapping_dialog.reset_tooltip"); onClicked: enh.resetMapping(mappingEditor.category, mappingEditor.kind) }
                AppButton { text: qsTr("scx.close"); role: "open"; onClicked: mappingEditor.close() }
            }
        }
    }
}
