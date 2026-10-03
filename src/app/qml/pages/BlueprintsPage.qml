pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import ScApp

// The Blueprint Tracker: available and owned blueprints side by side with
// search and facet filters, log scanning and owned-list import/export.
Item {
    id: page

    BlueprintController { id: bp }

    // A facet combo: "Any" followed by the facet's values.
    component FacetCombo: RowLayout {
        id: facet
        property string label
        property var values: []
        property string value
        signal picked(string value)
        spacing: 4
        Label { text: facet.label; color: Theme.text }
        ComboBox {
            implicitWidth: 150
            model: [qsTr("enhancements.blueprints_facet_any")].concat(facet.values)
            currentIndex: facet.value === "" ? 0 : facet.values.indexOf(facet.value) + 1
            onActivated: index => facet.picked(index === 0 ? "" : facet.values[index - 1])
        }
    }

    // One list with Ctrl/Shift multi-selection; double-click moves a row.
    component BlueprintList: Rectangle {
        id: box
        property string heading
        property var rows: []
        property var selected: ({})
        property int anchorRow: -1
        signal activated(var names)

        function selectedNames() {
            return rows.filter(r => selected[r.name]).map(r => r.name)
        }
        function clearSelection() { selected = ({}); anchorRow = -1 }
        onRowsChanged: {
            const keep = {}
            for (const r of rows) if (selected[r.name]) keep[r.name] = true
            selected = keep
        }

        color: Theme.base
        border.color: Theme.border
        radius: Theme.radius

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 4
            Label {
                text: box.heading + "  (" + box.rows.length + ")"
                color: Theme.title
                font.bold: true
            }
            ListView {
                id: view
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: box.rows
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: rowItem
                    required property var modelData
                    required property int index
                    readonly property bool isSelected: box.selected[modelData.name] === true
                    width: view.width
                    height: rowText.implicitHeight + 6
                    color: isSelected ? Theme.highlight : (index % 2 ? Theme.alternateBase : "transparent")
                    Text {
                        id: rowText
                        anchors.verticalCenter: parent.verticalCenter
                        x: 6
                        width: parent.width - 12
                        text: rowItem.modelData.display
                        color: rowItem.isSelected ? Theme.highlightedText : Theme.text
                        elide: Text.ElideRight
                        font.pixelSize: Theme.fontSize
                    }
                    HoverHandler { id: rowHover }
                    ToolTip.visible: rowHover.hovered && rowItem.modelData.tooltip !== ""
                    ToolTip.text: rowItem.modelData.tooltip
                    ToolTip.delay: 700
                    MouseArea {
                        anchors.fill: parent
                        onClicked: (mouse) => {
                            const name = rowItem.modelData.name
                            const mods = mouse.modifiers
                            let sel = Object.assign({}, box.selected)
                            if (mods & Qt.ShiftModifier && box.anchorRow >= 0) {
                                if (!(mods & Qt.ControlModifier))
                                    sel = {}
                                const lo = Math.min(box.anchorRow, rowItem.index)
                                const hi = Math.max(box.anchorRow, rowItem.index)
                                for (let i = lo; i <= hi && i < box.rows.length; ++i)
                                    sel[box.rows[i].name] = true
                            } else if (mods & Qt.ControlModifier) {
                                if (sel[name]) delete sel[name]; else sel[name] = true
                                box.anchorRow = rowItem.index
                            } else {
                                sel = {}
                                sel[name] = true
                                box.anchorRow = rowItem.index
                            }
                            box.selected = sel
                        }
                        onDoubleClicked: box.activated([rowItem.modelData.name])
                    }
                }
            }
        }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier)) {
                const sel = {}
                for (const r of rows) sel[r.name] = true
                selected = sel
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                activated(selectedNames())
                event.accepted = true
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: 10

        Label { text: qsTr("blueprint_tracker.title"); color: Theme.title; font.pixelSize: 20; font.bold: true }
        Label { text: qsTr("enhancements.blueprints_desc"); color: Theme.dim; wrapMode: Text.WordWrap; Layout.fillWidth: true }

        // ── actions ──
        Flow {
            Layout.fillWidth: true
            spacing: 8
            AppButton {
                role: "load"
                text: qsTr("blueprint_tracker.scan_logs_btn")
                tip: qsTr("blueprint_tracker.scan_logs_tooltip")
                enabled: !App.busy
                onClicked: bp.scanLogs()
            }
            AppCheckBox {
                text: qsTr("blueprint_tracker.scan_other_channels_checkbox")
                tip: qsTr("blueprint_tracker.scan_other_channels_tooltip")
                checked: bp.scanOtherChannels
                onToggled: bp.scanOtherChannels = checked
            }
            AppCheckBox {
                text: qsTr("blueprint_tracker.force_rescan_checkbox")
                tip: qsTr("blueprint_tracker.force_rescan_tooltip")
                checked: bp.forceRescan
                onToggled: bp.forceRescan = checked
            }
            AppButton {
                role: "apply"
                text: qsTr("blueprint_tracker.apply_owned_tag_btn")
                tip: qsTr("blueprint_tracker.apply_owned_tag_tooltip")
                enabled: App.loaded && !App.busy
                onClicked: bp.applyOwnedTags()
            }
            AppButton {
                text: qsTr("blueprint_tracker.export_owned_btn")
                tip: qsTr("blueprint_tracker.export_owned_tooltip")
                onClicked: { exportDialog.selectedFile = App.pathToUrl(bp.defaultExportPath(false)); exportDialog.open() }
            }
            AppButton {
                text: qsTr("blueprint_tracker.import_owned_btn")
                tip: qsTr("blueprint_tracker.import_owned_tooltip")
                onClicked: importDialog.open()
            }
        }

        // ── filters ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("enhancements.blueprints_search_placeholder")
                text: bp.search
                onTextEdited: searchDelay.restart()
                Timer { id: searchDelay; interval: 200; onTriggered: bp.search = parent.text }
            }
            AppCheckBox {
                text: qsTr("enhancements.blueprints_show_tags_checkbox")
                checked: bp.showTags
                onToggled: bp.showTags = checked
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 14
            FacetCombo { label: qsTr("enhancements.blueprints_mission_label"); values: bp.missions; value: bp.mission; onPicked: v => bp.mission = v }
            FacetCombo { label: qsTr("enhancements.blueprints_facet_type"); values: bp.types; value: bp.type; onPicked: v => bp.type = v }
            FacetCombo { label: qsTr("enhancements.blueprints_facet_class"); values: bp.classes; value: bp.cls; onPicked: v => bp.cls = v }
            FacetCombo { label: qsTr("enhancements.blueprints_facet_size"); values: bp.sizes; value: bp.size; onPicked: v => bp.size = v }
            FacetCombo { label: qsTr("enhancements.blueprints_facet_grade"); values: bp.grades; value: bp.grade; onPicked: v => bp.grade = v }
        }
        Label {
            text: qsTr("enhancements.blueprints_filter_note")
            color: Theme.placeholder
            font.pixelSize: Theme.smallFont
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            visible: bp.totalCount === 0
            text: qsTr("enhancements.blueprints_empty_note")
            color: Theme.action("restore")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // ── lists ──
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            BlueprintList {
                id: availableList
                objectName: "blueprintsAvailable"
                Layout.fillWidth: true
                Layout.fillHeight: true
                heading: qsTr("enhancements.blueprints_available_label")
                rows: bp.available
                onActivated: names => { bp.own(names); clearSelection() }
                TapHandler { onTapped: availableList.forceActiveFocus() }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 8
                AppButton {
                    text: "▶"
                    tip: qsTr("enhancements.blueprints_add_tooltip")
                    implicitWidth: 44
                    font.pixelSize: 16
                    enabled: Object.keys(availableList.selected).length > 0
                    onClicked: { bp.own(availableList.selectedNames()); availableList.clearSelection() }
                }
                AppButton {
                    text: "◀"
                    tip: qsTr("enhancements.blueprints_remove_tooltip")
                    implicitWidth: 44
                    font.pixelSize: 16
                    enabled: Object.keys(ownedList.selected).length > 0
                    onClicked: { bp.unown(ownedList.selectedNames()); ownedList.clearSelection() }
                }
            }
            BlueprintList {
                id: ownedList
                Layout.fillWidth: true
                Layout.fillHeight: true
                heading: qsTr("enhancements.blueprints_owned_label")
                rows: bp.owned
                onActivated: names => { bp.unown(names); clearSelection() }
                TapHandler { onTapped: ownedList.forceActiveFocus() }
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("blueprint_tracker.export_dialog_title")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("blueprint_tracker.export_json_filter"), qsTr("blueprint_tracker.export_csv_filter")]
        onAccepted: bp.exportOwned(selectedFile)
    }
    FileDialog {
        id: importDialog
        title: qsTr("blueprint_tracker.import_dialog_title")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("blueprint_tracker.import_file_filter")]
        onAccepted: bp.importOwned(selectedFile)
    }
}
