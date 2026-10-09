pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ScApp

// Missions: every mission and contract in the game data with its payout and
// the places it can send you, searchable and filtered, with details beside.
Item {
    id: page

    // True while the page is showing: the catalog loads on first show.
    property bool active: false
    property int selected: -1 // a mission index, -1 for none
    property var info: ({})   // missions.details(selected)

    function select(mission) {
        selected = mission
        info = mission >= 0 ? missions.details(mission) : ({})
    }

    MissionsController {
        id: missions
        active: page.active
        onCatalogChanged: page.select(-1)
    }

    // "Any" followed by the facet's values.
    component FacetCombo: RowLayout {
        id: facet
        property string label
        property var values: []
        property string value
        signal picked(string value)
        spacing: 4
        Label { text: facet.label; color: Theme.text }
        ComboBox {
            implicitWidth: 170
            model: [qsTr("missions.facet_any")].concat(facet.values)
            currentIndex: facet.value === "" ? 0 : facet.values.indexOf(facet.value) + 1
            onActivated: index => facet.picked(index === 0 ? "" : facet.values[index - 1])
        }
    }

    // A label and its value in the details; hidden when the value is empty.
    component Field: RowLayout {
        id: field
        property string label
        property string value
        visible: value !== ""
        Layout.fillWidth: true
        spacing: 10
        Label {
            text: field.label
            color: Theme.dim
            Layout.preferredWidth: 110
            Layout.alignment: Qt.AlignTop
        }
        Label {
            text: field.value
            color: Theme.text
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    component Heading: Label {
        color: Theme.title
        font.bold: true
        topPadding: 6
    }

    // A titled, selectable list; hidden when empty.
    component RewardList: ColumnLayout {
        id: rewardList
        property string title
        property var lines: []
        visible: lines.length > 0
        Layout.fillWidth: true
        spacing: 2
        Label { text: rewardList.title; color: Theme.text; font.bold: true }
        TextEdit {
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            color: Theme.text
            selectionColor: Theme.highlight
            selectedTextColor: Theme.highlightedText
            font.pixelSize: Theme.fontSize
            text: rewardList.lines.map(line => "•  " + line).join("\n")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: 10

        Label { text: qsTr("missions.title"); color: Theme.title; font.pixelSize: 20; font.bold: true }
        Label { text: qsTr("missions.desc"); color: Theme.dim; wrapMode: Text.WordWrap; Layout.fillWidth: true }

        // ── what's missing ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: missions.status === "nodata" || missions.status === "idle"
                     || (missions.status === "ready" && !missions.hasPlaces)
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.action("restore")
                text: missions.status === "nodata" ? qsTr("missions.no_data")
                    : missions.status === "idle" ? qsTr("missions.not_loaded")
                    : qsTr("missions.no_places")
            }
            AppButton {
                role: "load"
                text: missions.status === "idle" ? qsTr("missions.load_btn") : qsTr("missions.extract_btn")
                enabled: !App.busy
                onClicked: missions.status === "idle" ? missions.reload() : missions.extractGameData()
            }
        }

        // ── filters ──
        TextField {
            id: searchField
            objectName: "missionsSearch"
            Layout.fillWidth: true
            placeholderText: qsTr("missions.search_placeholder")
            text: missions.search
            onTextEdited: searchDelay.restart()
            Timer { id: searchDelay; interval: 200; onTriggered: missions.search = searchField.text }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 14
            FacetCombo {
                label: qsTr("missions.category_label")
                values: missions.categories
                value: missions.category
                onPicked: v => missions.category = v
            }
            FacetCombo {
                label: qsTr("missions.system_label")
                values: missions.systems
                value: missions.system
                onPicked: v => missions.system = v
            }
            RowLayout {
                spacing: 4
                Label { text: qsTr("missions.payout_label"); color: Theme.text }
                ComboBox {
                    implicitWidth: 170
                    readonly property var values: ["", "fixed", "calculated"]
                    model: [qsTr("missions.facet_any"), qsTr("missions.payout_fixed_filter"),
                            qsTr("missions.payout_calculated_filter")]
                    currentIndex: values.indexOf(missions.payout)
                    onActivated: index => missions.payout = values[index]
                }
            }
            RowLayout {
                spacing: 4
                Label { text: qsTr("missions.sort_label"); color: Theme.text }
                ComboBox {
                    implicitWidth: 170
                    readonly property var values: ["title", "payout_high", "payout_low"]
                    model: [qsTr("missions.sort_title"), qsTr("missions.sort_payout_high"),
                            qsTr("missions.sort_payout_low")]
                    currentIndex: values.indexOf(missions.sort)
                    onActivated: index => missions.sort = values[index]
                }
            }
            AppCheckBox {
                text: qsTr("missions.blueprints_only")
                checked: missions.blueprintsOnly
                onToggled: missions.blueprintsOnly = checked
            }
        }
        Label {
            visible: missions.status === "ready"
            text: App.fmt(qsTr("missions.count"), { shown: missions.rows.length, total: missions.totalCount })
            color: Theme.placeholder
        }

        // ── list and details ──
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            // List and details share the width 2:3.
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                Layout.horizontalStretchFactor: 2
                color: Theme.base
                border.color: Theme.border
                radius: Theme.radius

                ListView {
                    id: list
                    objectName: "missionsList"
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: missions.rows
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        readonly property bool isSelected: modelData.mission === page.selected
                        readonly property color ink: isSelected ? Theme.highlightedText : Theme.text
                        width: list.width
                        height: rowLines.implicitHeight + 10
                        color: isSelected ? Theme.highlight : (index % 2 ? Theme.alternateBase : "transparent")
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 8
                            spacing: 10
                            ColumnLayout {
                                id: rowLines
                                Layout.fillWidth: true
                                spacing: 1
                                Text {
                                    Layout.fillWidth: true
                                    text: row.modelData.title
                                    color: row.ink
                                    font.pixelSize: Theme.fontSize
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: [row.modelData.giver, row.modelData.category, row.modelData.systems]
                                          .filter(s => s !== "").join("  ·  ")
                                    color: row.isSelected ? Theme.highlightedText : Theme.dim
                                    font.pixelSize: Theme.smallFont
                                    elide: Text.ElideRight
                                }
                            }
                            // Rewards blueprints.
                            Rectangle {
                                visible: row.modelData.blueprints
                                implicitWidth: badge.implicitWidth + 8
                                implicitHeight: badge.implicitHeight + 2
                                radius: 3
                                color: "transparent"
                                border.color: Theme.em4
                                Text {
                                    id: badge
                                    anchors.centerIn: parent
                                    text: qsTr("missions.bp_badge")
                                    color: row.isSelected ? Theme.highlightedText : Theme.em4
                                    font.pixelSize: Theme.smallFont
                                    font.bold: true
                                }
                            }
                            Text {
                                text: row.modelData.payout
                                color: row.ink
                                font.pixelSize: Theme.fontSize
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: page.select(row.modelData.mission)
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: missions.status === "ready" && list.count === 0
                    text: qsTr("missions.no_matches")
                    color: Theme.placeholder
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                Layout.horizontalStretchFactor: 3
                color: Theme.base
                border.color: Theme.border
                radius: Theme.radius

                Label {
                    anchors.centerIn: parent
                    visible: page.selected < 0
                    text: qsTr("missions.select_hint")
                    color: Theme.placeholder
                }
                ScrollView {
                    id: detailScroll
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: page.selected >= 0
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: detailScroll.availableWidth
                        spacing: 6

                        Label {
                            Layout.fillWidth: true
                            text: page.info.title ?? ""
                            color: Theme.title
                            font.pixelSize: 18
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }
                        Field { label: qsTr("missions.giver_label"); value: page.info.giver ?? "" }
                        Field {
                            label: qsTr("missions.type_label")
                            value: [page.info.category ?? "", page.info.source ?? ""].filter(s => s !== "").join("  ·  ")
                        }
                        Field { label: qsTr("missions.systems_label"); value: page.info.systems ?? "" }
                        Field { label: qsTr("missions.payout_field"); value: page.info.payout ?? "" }
                        Field { label: qsTr("missions.buy_in_label"); value: page.info.buyIn ?? "" }
                        Field { label: qsTr("missions.difficulty_label"); value: page.info.difficulty ?? "" }
                        Field { label: qsTr("missions.requires_label"); value: page.info.requiredRank ?? "" }
                        Label {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: page.info.payoutNote ?? ""
                            color: Theme.placeholder
                            font.pixelSize: Theme.smallFont
                            wrapMode: Text.WordWrap
                        }

                        Heading {
                            visible: (page.info.repSuccess ?? []).length + (page.info.repFailure ?? []).length
                                     + (page.info.blueprints ?? []).length > 0
                            text: qsTr("missions.rewards_label")
                        }
                        RewardList { title: qsTr("missions.rep_success"); lines: page.info.repSuccess ?? [] }
                        RewardList { title: qsTr("missions.rep_failure"); lines: page.info.repFailure ?? [] }
                        Repeater {
                            model: page.info.blueprints ?? []
                            delegate: RewardList {
                                required property var modelData
                                title: modelData.heading
                                lines: modelData.items
                            }
                        }

                        Heading { text: qsTr("missions.description_label") }
                        Label {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: page.info.variants ?? ""
                            color: Theme.placeholder
                            font.pixelSize: Theme.smallFont
                            wrapMode: Text.WordWrap
                        }
                        TextEdit {
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            wrapMode: Text.WordWrap
                            textFormat: Text.PlainText
                            text: page.info.description ?? ""
                            color: Theme.text
                            selectionColor: Theme.highlight
                            selectedTextColor: Theme.highlightedText
                            font.pixelSize: Theme.fontSize
                        }

                        Heading { text: qsTr("missions.locations_label") }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("missions.locations_note")
                            color: Theme.placeholder
                            font.pixelSize: Theme.smallFont
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            visible: (page.info.locations ?? []).length === 0
                            text: qsTr("missions.no_locations")
                            color: Theme.dim
                        }
                        Repeater {
                            model: page.info.locations ?? []
                            delegate: ColumnLayout {
                                id: slot
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: App.fmt(qsTr("missions.slot_heading"),
                                                  { label: slot.modelData.label, count: slot.modelData.count })
                                    color: Theme.text
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: slot.modelData.count === 0
                                    text: App.fmt(qsTr("missions.slot_runtime"), { tags: slot.modelData.tags })
                                    color: Theme.dim
                                    wrapMode: Text.WordWrap
                                }
                                TextEdit {
                                    Layout.fillWidth: true
                                    visible: slot.modelData.count > 0
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: Text.WordWrap
                                    textFormat: Text.PlainText
                                    color: Theme.text
                                    selectionColor: Theme.highlight
                                    selectedTextColor: Theme.highlightedText
                                    font.pixelSize: Theme.fontSize
                                    text: slot.modelData.places.map(p => "•  " + p.label
                                                                    + (p.system !== "" ? "   (" + p.system + ")" : ""))
                                              .join("\n")
                                          + (slot.modelData.more > 0
                                             ? "\n" + App.fmt(qsTr("missions.slot_more"), { count: slot.modelData.more })
                                             : "")
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            topPadding: 8
                            text: App.fmt(qsTr("missions.record_label"), { record: page.info.record ?? "" })
                            color: Theme.placeholder
                            font.pixelSize: Theme.smallFont
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
    }
}
