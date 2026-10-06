pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ScApp

// Loadout: a ship's stock loadout from the game data and what it adds up
// to; swap items for ones that fit, split the power, save loadouts, and run
// the quantum travel and versus simulations.
Item {
    id: page

    // True while the page is showing: the catalog loads on first show.
    property bool active: false
    property string shipSearch: ""
    property string versusId: "" // the versus simulation's target ship; "" for none
    readonly property var shownShips: {
        const q = shipSearch.trim().toLowerCase()
        return q === "" ? lc.ships
                        : lc.ships.filter(s => (s.name + " " + s.manufacturer + " " + s.role).toLowerCase().includes(q))
    }

    LoadoutController {
        id: lc
        active: page.active
    }

    // ── building blocks ──

    // A titled panel.
    component Panel: Rectangle {
        id: panel
        property string title
        default property alias content: body.data
        Layout.fillWidth: true
        implicitHeight: column.implicitHeight + 20
        color: Theme.panel
        border.color: Theme.border
        radius: 6
        ColumnLayout {
            id: column
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 10
            spacing: 6
            Label {
                text: panel.title
                color: Theme.title
                font.bold: true
                font.pixelSize: 14
                visible: text !== ""
            }
            ColumnLayout {
                id: body
                Layout.fillWidth: true
                spacing: 4
            }
        }
    }

    // A big figure with its caption.
    component Figure: ColumnLayout {
        id: figure
        property string value
        property string caption
        Layout.fillWidth: true
        Layout.preferredWidth: 100
        spacing: 0
        Label { text: figure.value; color: Theme.text; font.pixelSize: 20; font.bold: true }
        Label { text: figure.caption; color: Theme.placeholder; font.pixelSize: Theme.smallFont }
    }

    // A label and its value on one line; hidden when the value is empty.
    component Row: RowLayout {
        id: row
        property string label
        property string value
        property color valueColor: Theme.text
        visible: value !== ""
        Layout.fillWidth: true
        spacing: 8
        Label { text: row.label; color: Theme.dim; Layout.fillWidth: true; elide: Text.ElideRight }
        Label { text: row.value; color: row.valueColor; font.bold: true }
    }

    // Physical / energy / distortion columns for one quantity.
    component TypeRow: RowLayout {
        id: typeRow
        property string label
        property var values: ({})
        Layout.fillWidth: true
        spacing: 8
        Label { text: typeRow.label; color: Theme.dim; Layout.fillWidth: true; elide: Text.ElideRight }
        Repeater {
            model: ["physical", "energy", "distortion"]
            delegate: Label {
                required property string modelData
                text: typeRow.values[modelData] ?? ""
                color: Theme.text
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 58
            }
        }
    }

    // A small outlined tag.
    component Tag: Rectangle {
        id: tag
        property string text
        property color tint: Theme.dim
        visible: text !== ""
        implicitWidth: tagText.implicitWidth + 10
        implicitHeight: tagText.implicitHeight + 4
        radius: 3
        color: "transparent"
        border.color: tag.tint
        Text {
            id: tagText
            anchors.centerIn: parent
            text: tag.text
            color: tag.tint
            font.pixelSize: Theme.smallFont
            font.bold: true
        }
    }

    // ── layout ──

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: qsTr("loadout.title"); color: Theme.title; font.pixelSize: 20; font.bold: true }
            Label {
                Layout.fillWidth: true
                text: qsTr("loadout.desc")
                color: Theme.dim
                elide: Text.ElideRight
            }
        }

        // ── what's missing ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            visible: lc.status === "nodata" || lc.status === "idle" || (lc.status === "ready" && !lc.hasVehicles)
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.action("restore")
                text: lc.status === "nodata" ? qsTr("loadout.no_data")
                    : lc.status === "idle" ? qsTr("loadout.not_loaded")
                    : qsTr("loadout.no_vehicles")
            }
            AppButton {
                role: "load"
                text: lc.status === "idle" ? qsTr("loadout.load_btn") : qsTr("loadout.extract_btn")
                enabled: !App.busy
                onClicked: lc.status === "idle" ? lc.reload() : lc.extractGameData()
            }
        }

        // ── ship and saved loadouts ──
        Flow {
            Layout.fillWidth: true
            spacing: 8
            visible: lc.ships.length > 0
            TextField {
                id: shipSearchField
                width: 180
                placeholderText: qsTr("loadout.search_placeholder")
                onTextEdited: page.shipSearch = text
            }
            ComboBox {
                id: shipBox
                objectName: "loadoutShip"
                width: 300
                model: page.shownShips
                textRole: "name"
                currentIndex: page.shownShips.findIndex(s => s.id === lc.shipId)
                displayText: lc.ship.name ?? ""
                onActivated: index => lc.shipId = page.shownShips[index].id
            }
            AppButton {
                text: qsTr("loadout.reset_btn")
                enabled: lc.modified
                tip: qsTr("loadout.reset_tip")
                onClicked: lc.resetLoadout()
            }
            Tag {
                text: lc.modified ? qsTr("loadout.modified") : ""
                tint: Theme.action("restore")
            }
            Item { width: 16; height: 1 }
            ComboBox {
                id: savedBox
                width: 200
                model: lc.saved
                enabled: lc.saved.length > 0
                displayText: lc.saved.length > 0 ? currentText : qsTr("loadout.no_saved")
            }
            AppButton {
                text: qsTr("loadout.load_saved_btn")
                enabled: savedBox.currentIndex >= 0 && lc.saved.length > 0
                onClicked: lc.loadLoadout(savedBox.currentText)
            }
            AppButton {
                text: qsTr("loadout.delete_saved_btn")
                enabled: savedBox.currentIndex >= 0 && lc.saved.length > 0
                onClicked: lc.deleteLoadout(savedBox.currentText)
            }
            TextField {
                id: saveName
                width: 170
                placeholderText: qsTr("loadout.save_placeholder")
            }
            AppButton {
                role: "apply"
                text: qsTr("loadout.save_btn")
                enabled: saveName.text.trim() !== "" && lc.shipId !== ""
                onClicked: {
                    lc.saveLoadout(saveName.text)
                    saveName.clear()
                }
            }
        }

        Label {
            visible: lc.status === "loading"
            text: qsTr("loadout.loading")
            color: Theme.placeholder
        }

        // ── summary beside the slots ──
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            visible: lc.shipId !== ""

            ScrollView {
                id: summaryScroll
                Layout.preferredWidth: 380
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: summaryScroll.availableWidth - 4
                    spacing: 8

                    Panel {
                        title: lc.ship.name ?? ""
                        Label {
                            text: lc.ship.manufacturer ?? ""
                            color: Theme.dim
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 4
                            Tag { text: lc.ship.size ?? ""; tint: Theme.em4 }
                            Tag { text: lc.ship.career ?? ""; tint: Theme.em4 }
                            Tag { text: lc.ship.role ?? ""; tint: Theme.em4 }
                            Tag { text: lc.ship.crew ?? ""; tint: Theme.em4 }
                            Tag { text: (lc.ship.ground ?? false) ? qsTr("loadout.ground") : ""; tint: Theme.em4 }
                        }
                        Row { label: qsTr("loadout.dimensions"); value: lc.ship.dimensions ?? "" }
                        Row { label: qsTr("loadout.hull_mass"); value: lc.ship.mass ?? "" }
                        Row { label: qsTr("loadout.loaded_mass"); value: lc.ship.loadedMass ?? "" }
                        Row { label: qsTr("loadout.hull_hp"); value: lc.ship.hullHp ?? "" }
                        Row {
                            label: App.fmt(qsTr("loadout.vital_hp"), { count: lc.ship.vitalParts ?? 0 })
                            value: lc.ship.vitalHp ?? ""
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: lc.ship.description ?? ""
                            color: Theme.text
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.smallFont + 1
                            topPadding: 4
                        }
                    }

                    Panel {
                        title: qsTr("loadout.damage")
                        RowLayout {
                            Layout.fillWidth: true
                            Figure { value: lc.totals.damage?.alpha ?? ""; caption: qsTr("loadout.alpha") }
                            Figure { value: lc.totals.damage?.sustained ?? ""; caption: qsTr("loadout.sustained_dps") }
                            Figure { value: lc.totals.damage?.burst ?? ""; caption: qsTr("loadout.burst_dps") }
                        }
                        Repeater {
                            model: lc.totals.damage?.groups ?? []
                            delegate: Row {
                                required property var modelData
                                label: App.fmt(qsTr("loadout.group_line"), { group: modelData.label, guns: modelData.guns })
                                value: modelData.alpha + "  ·  " + modelData.sustained + "  ·  " + modelData.burst
                            }
                        }
                        Row { label: qsTr("loadout.missiles"); value: lc.totals.damage?.missiles ?? "" }
                        Row { label: qsTr("loadout.bombs"); value: lc.totals.damage?.bombs ?? "" }
                        TypeRow {
                            label: ""
                            values: ({ physical: qsTr("loadout.type_physical"), energy: qsTr("loadout.type_energy"),
                                       distortion: qsTr("loadout.type_distortion") })
                        }
                        TypeRow {
                            label: qsTr("loadout.burst_by_type")
                            values: lc.totals.damage ?? ({})
                        }
                    }

                    Panel {
                        title: qsTr("loadout.defense")
                        RowLayout {
                            Layout.fillWidth: true
                            Figure { value: lc.totals.defense?.shieldHp ?? ""; caption: qsTr("loadout.shield_hp") }
                            Figure { value: lc.totals.defense?.shieldRegen ?? ""; caption: qsTr("loadout.shield_regen") }
                            Figure { value: lc.totals.defense?.armorHp ?? ""; caption: qsTr("loadout.armor_hp") }
                        }
                        TypeRow {
                            label: ""
                            values: ({ physical: qsTr("loadout.type_physical"), energy: qsTr("loadout.type_energy"),
                                       distortion: qsTr("loadout.type_distortion") })
                        }
                        TypeRow { label: qsTr("loadout.shield_resistance"); values: lc.totals.defense?.resistance ?? ({}) }
                        TypeRow { label: qsTr("loadout.shield_absorption"); values: lc.totals.defense?.absorption ?? ({}) }
                        TypeRow { label: qsTr("loadout.armor_deflection"); values: lc.totals.defense?.deflection ?? ({}) }
                        TypeRow { label: qsTr("loadout.armor_reduction"); values: lc.totals.defense?.reduction ?? ({}) }
                        Row {
                            label: qsTr("loadout.signatures")
                            value: lc.totals.defense ? "EM " + lc.totals.defense.em + "  ·  IR " + lc.totals.defense.ir
                                                       + "  ·  CS " + lc.totals.defense.cs : ""
                        }
                        Row {
                            label: qsTr("loadout.countermeasures")
                            value: lc.totals.defense ? App.fmt(qsTr("loadout.countermeasures_value"),
                                                               { decoys: lc.totals.defense.decoys,
                                                                 noise: lc.totals.defense.noise }) : ""
                        }
                    }

                    Panel {
                        title: qsTr("loadout.power")
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            AppButton {
                                text: qsTr("loadout.scm")
                                checkable: true
                                checked: !lc.nav
                                onClicked: lc.nav = false
                            }
                            AppButton {
                                text: qsTr("loadout.nav")
                                checkable: true
                                checked: lc.nav
                                onClicked: lc.nav = true
                            }
                            Item { Layout.fillWidth: true }
                            AppButton {
                                text: qsTr("loadout.auto_power")
                                tip: qsTr("loadout.auto_power_tip")
                                onClicked: lc.resetPower()
                            }
                        }
                        Row {
                            label: qsTr("loadout.pips_used")
                            value: (lc.totals.power?.used ?? 0) + " / " + (lc.totals.power?.output ?? "")
                            valueColor: (lc.totals.power?.over ?? false) ? Theme.action("needsApply") : Theme.text
                        }
                        Repeater {
                            model: lc.power
                            delegate: RowLayout {
                                id: consumer
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 6
                                ColumnLayout {
                                    Layout.preferredWidth: 130
                                    spacing: 0
                                    Label {
                                        Layout.fillWidth: true
                                        text: consumer.modelData.label
                                        color: Theme.text
                                        elide: Text.ElideRight
                                        font.pixelSize: Theme.smallFont + 1
                                    }
                                    Label {
                                        text: consumer.modelData.categoryLabel
                                        color: Theme.placeholder
                                        font.pixelSize: Theme.smallFont
                                    }
                                }
                                // Click a pip to power up to it; the first again to switch off.
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Repeater {
                                        model: consumer.modelData.max
                                        delegate: Rectangle {
                                            id: pip
                                            required property int index
                                            readonly property bool on: index < consumer.modelData.pips
                                            width: 16
                                            height: 16
                                            radius: 3
                                            color: on ? Theme.chunk : "transparent"
                                            border.color: index < consumer.modelData.min ? Theme.highlight : Theme.groove
                                            MouseArea {
                                                anchors.fill: parent
                                                onClicked: lc.setPips(consumer.modelData.key,
                                                                      pip.index === 0 && consumer.modelData.pips === 1
                                                                          ? 0 : pip.index + 1)
                                            }
                                        }
                                    }
                                }
                                Label {
                                    text: consumer.modelData.pips + "/" + consumer.modelData.max
                                    color: Theme.dim
                                    font.pixelSize: Theme.smallFont
                                }
                            }
                        }
                        Row { label: qsTr("loadout.coolant"); value: lc.totals.power?.coolant ?? "" }
                        Row { label: qsTr("loadout.weapon_power"); value: lc.totals.damage?.efficiency ?? "" }
                        Row {
                            label: qsTr("loadout.signature_estimate")
                            value: lc.totals.power ? "EM " + lc.totals.power.em + "  ·  IR " + lc.totals.power.ir : ""
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("loadout.power_note")
                            color: Theme.placeholder
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.smallFont
                        }
                    }

                    Panel {
                        title: qsTr("loadout.flight")
                        Row { label: qsTr("loadout.scm_speed"); value: lc.totals.flight?.scm ?? "" }
                        Row { label: qsTr("loadout.boost_speed"); value: lc.totals.flight?.boost ?? "" }
                        Row { label: qsTr("loadout.max_speed"); value: lc.totals.flight?.max ?? "" }
                        Row { label: qsTr("loadout.pitch"); value: lc.totals.flight?.pitch ?? "" }
                        Row { label: qsTr("loadout.yaw"); value: lc.totals.flight?.yaw ?? "" }
                        Row { label: qsTr("loadout.roll"); value: lc.totals.flight?.roll ?? "" }
                        Row { label: qsTr("loadout.boost_tank_label"); value: lc.totals.flight?.boostTank ?? "" }
                        Row { label: qsTr("loadout.main_thrust"); value: lc.totals.flight?.mainThrust ?? "" }
                        Row { label: qsTr("loadout.retro_thrust"); value: lc.totals.flight?.retroThrust ?? "" }
                        Row { label: qsTr("loadout.maneuver_thrust"); value: lc.totals.flight?.maneuverThrust ?? "" }
                        Row { label: qsTr("loadout.acceleration"); value: lc.totals.flight?.accel ?? "" }
                        Row { label: qsTr("loadout.hydrogen"); value: lc.totals.flight?.fuel ?? "" }
                        Row { label: qsTr("loadout.quantum_fuel"); value: lc.totals.flight?.quantumFuel ?? "" }
                    }

                    Panel {
                        title: qsTr("loadout.quantum")
                        visible: (lc.totals.quantum?.speed ?? "") !== ""
                        Row { label: qsTr("loadout.qt_speed"); value: lc.totals.quantum?.speed ?? "" }
                        Row { label: qsTr("loadout.qt_spline"); value: lc.totals.quantum?.spline ?? "" }
                        Row { label: qsTr("loadout.qt_spool"); value: lc.totals.quantum?.spool ?? "" }
                        Row { label: qsTr("loadout.qt_cooldown"); value: lc.totals.quantum?.cooldown ?? "" }
                        Row { label: qsTr("loadout.qt_accel"); value: lc.totals.quantum?.accel ?? "" }
                        Row { label: qsTr("loadout.qt_fuel"); value: lc.totals.quantum?.fuel ?? "" }
                        Row { label: qsTr("loadout.qt_range"); value: lc.totals.quantum?.range ?? "" }
                    }

                    Panel {
                        title: qsTr("loadout.simulations")
                        // Quantum travel time.
                        RowLayout {
                            Layout.fillWidth: true
                            visible: (lc.totals.quantum?.speed ?? "") !== ""
                            spacing: 6
                            Label { text: qsTr("loadout.travel_label"); color: Theme.dim }
                            SpinBox {
                                id: distance
                                from: 1
                                to: 100000
                                value: 20
                                editable: true
                                Layout.preferredWidth: 110
                            }
                            Label { text: "Gm"; color: Theme.dim }
                            Item { Layout.fillWidth: true }
                            Label {
                                text: lc.totals.quantum ? lc.travelTime(distance.value) : ""
                                color: Theme.text
                                font.bold: true
                            }
                        }
                        // Versus another ship's stock loadout.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label { text: qsTr("loadout.versus_label"); color: Theme.dim }
                            ComboBox {
                                id: targetBox
                                Layout.fillWidth: true
                                model: lc.ships
                                textRole: "name"
                                currentIndex: lc.ships.findIndex(s => s.id === page.versusId)
                                displayText: currentIndex < 0 ? qsTr("loadout.versus_pick") : currentText
                                onActivated: index => page.versusId = lc.ships[index].id
                            }
                        }
                        Item {
                            id: fight
                            Layout.fillWidth: true
                            implicitHeight: fightRows.implicitHeight
                            visible: page.versusId !== ""
                            // Recomputed whenever the loadout's totals change.
                            readonly property var result: lc.totals && page.versusId !== "" ? lc.engage(page.versusId)
                                                                                           : ({})
                            ColumnLayout {
                                id: fightRows
                                width: parent.width
                                spacing: 4
                                Row { label: qsTr("loadout.versus_shields"); value: fight.result.shieldTime ?? "" }
                                Row { label: qsTr("loadout.versus_kill"); value: fight.result.killTime ?? "" }
                                Row {
                                    label: qsTr("loadout.versus_deflected")
                                    value: (fight.result.deflected ?? 0) > 0 ? String(fight.result.deflected) : ""
                                    valueColor: Theme.action("needsApply")
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("loadout.simulation_note")
                            color: Theme.placeholder
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.smallFont
                        }
                    }
                }
            }

            // ── slots ──
            ScrollView {
                id: slotScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                Flow {
                    width: slotScroll.availableWidth - 4
                    spacing: 8

                    Repeater {
                        model: lc.sections
                        delegate: Rectangle {
                            id: section
                            required property var modelData
                            width: Math.min(460, slotScroll.availableWidth - 4)
                            height: sectionColumn.implicitHeight + 20
                            color: Theme.panel
                            border.color: Theme.border
                            radius: 6

                            ColumnLayout {
                                id: sectionColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 10
                                spacing: 6
                                Label {
                                    text: section.modelData.title
                                    color: Theme.title
                                    font.bold: true
                                    font.pixelSize: 14
                                }
                                Repeater {
                                    model: section.modelData.cards
                                    delegate: Rectangle {
                                        id: cardBox
                                        required property var modelData
                                        Layout.fillWidth: true
                                        implicitHeight: cardRows.implicitHeight + 12
                                        color: Theme.base
                                        border.color: Theme.border
                                        radius: Theme.radius
                                        ColumnLayout {
                                            id: cardRows
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.top: parent.top
                                            anchors.margins: 6
                                            spacing: 6
                                            Repeater {
                                                model: cardBox.modelData.rows
                                                delegate: ColumnLayout {
                                                    id: slotRow
                                                    required property var modelData
                                                    readonly property var item: modelData.item
                                                    Layout.fillWidth: true
                                                    Layout.leftMargin: modelData.depth * 14
                                                    spacing: 2
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 6
                                                        Tag { text: slotRow.modelData.size; tint: Theme.em4 }
                                                        Label {
                                                            Layout.fillWidth: true
                                                            text: slotRow.item ? slotRow.item.name : qsTr("loadout.empty_slot")
                                                            color: slotRow.item ? Theme.text : Theme.placeholder
                                                            font.bold: slotRow.item !== null
                                                            elide: Text.ElideRight
                                                        }
                                                        Tag { text: slotRow.item ? slotRow.item.maker : "" }
                                                        Tag { text: slotRow.item ? slotRow.item.grade : "" }
                                                        Tag { text: slotRow.item ? slotRow.item.cls : "" }
                                                        AppButton {
                                                            visible: slotRow.modelData.editable
                                                            text: qsTr("loadout.change_btn")
                                                            padding: 2
                                                            leftPadding: 8
                                                            rightPadding: 8
                                                            onClicked: picker.openFor(slotRow.modelData.path,
                                                                                      slotRow.modelData.label)
                                                        }
                                                    }
                                                    Label {
                                                        text: slotRow.modelData.label
                                                        color: Theme.placeholder
                                                        font.pixelSize: Theme.smallFont
                                                    }
                                                    Flow {
                                                        Layout.fillWidth: true
                                                        spacing: 10
                                                        visible: slotRow.item !== null
                                                        Repeater {
                                                            model: slotRow.item ? slotRow.item.stats : []
                                                            delegate: Text {
                                                                required property var modelData
                                                                text: modelData.label + " <b>" + modelData.value + "</b>"
                                                                textFormat: Text.StyledText
                                                                color: Theme.dim
                                                                font.pixelSize: Theme.smallFont + 1
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── item picker ──
    Popup {
        id: picker
        property string path
        property string label
        property var items: []
        property string filter: ""
        readonly property var shown: {
            const q = filter.trim().toLowerCase()
            return q === "" ? items : items.filter(i => (i.name + " " + i.maker + " " + i.cls).toLowerCase().includes(q))
        }
        function openFor(slotPath, slotLabel) {
            path = slotPath
            label = slotLabel
            items = lc.compatibleItems(slotPath)
            filter = ""
            pickerSearch.text = ""
            open()
            pickerSearch.forceActiveFocus()
        }
        anchors.centerIn: Overlay.overlay
        width: Math.min(640, page.width - 40)
        height: Math.min(560, page.height - 40)
        modal: true
        padding: 12
        background: Rectangle { color: Theme.window; border.color: Theme.border; radius: 6 }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label {
                text: App.fmt(qsTr("loadout.picker_title"), { slot: picker.label })
                color: Theme.title
                font.bold: true
                font.pixelSize: 15
            }
            TextField {
                id: pickerSearch
                Layout.fillWidth: true
                placeholderText: qsTr("loadout.picker_search")
                onTextEdited: picker.filter = text
            }
            ListView {
                id: pickerList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: picker.shown
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                header: ItemDelegate {
                    width: pickerList.width
                    text: qsTr("loadout.empty_slot")
                    onClicked: {
                        lc.setItem(picker.path, "")
                        picker.close()
                    }
                }
                delegate: Rectangle {
                    id: choice
                    required property var modelData
                    required property int index
                    width: pickerList.width
                    height: choiceLines.implicitHeight + 10
                    color: modelData.current ? Theme.highlight : (index % 2 ? Theme.alternateBase : "transparent")
                    readonly property color ink: modelData.current ? Theme.highlightedText : Theme.text
                    ColumnLayout {
                        id: choiceLines
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 1
                        Text {
                            Layout.fillWidth: true
                            text: choice.modelData.name + "   " + [choice.modelData.size, choice.modelData.grade,
                                                                   choice.modelData.maker, choice.modelData.cls]
                                                                  .filter(s => s !== "").join(" · ")
                            color: choice.ink
                            font.bold: true
                            font.pixelSize: Theme.fontSize
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: choice.modelData.summary
                            color: choice.modelData.current ? Theme.highlightedText : Theme.dim
                            font.pixelSize: Theme.smallFont
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            lc.setItem(picker.path, choice.modelData.id)
                            picker.close()
                        }
                    }
                }
            }
            Label {
                visible: picker.shown.length === 0
                text: qsTr("loadout.picker_none")
                color: Theme.placeholder
            }
        }
    }
}
