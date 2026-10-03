pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQml.Models
import ScApp

// The P4K Explorer: browse, search, preview and extract a Data.p4k, browse
// its DataForge records as XML, and export game data.
Item {
    id: page

    // True while this page is showing; the archive opens on the first visit.
    property bool active: false
    property bool triedOpen: false
    onActiveChanged: {
        if (active && !triedOpen && !ex.loaded && App.p4kStatus !== "noarchive" && App.p4kPath !== "") {
            triedOpen = true
            Qt.callLater(ex.open)
        }
    }

    // An archive path to show once the archive is open (--explorer); a path
    // inside "<dcb> (records)" loads the DataForge records first.
    property string revealPath: ""

    ExplorerController {
        id: ex
        onLoadedChanged: {
            if (!loaded || page.revealPath === "")
                return
            if (page.revealPath.indexOf(" (records)") >= 0 && !forgeLoaded) {
                Qt.callLater(loadDataForge)
                return
            }
            const node = findNode(page.revealPath)
            page.revealPath = ""
            if (node > 0)
                Qt.callLater(page.reveal, node)
        }
    }

    function selectedNodes() {
        const rows = tree.selectionModel.selectedRows(0)
        const nodes = []
        for (let i = 0; i < rows.length; ++i)
            nodes.push(ex.tree.nodeAt(rows[i]))
        if (nodes.length === 0 && ex.currentNode > 0)
            nodes.push(ex.currentNode)
        return nodes
    }
    function reveal(node) {
        const idx = ex.tree.indexOfNode(node)
        // The linter has no members for QModelIndex; valid exists at run time.
        if (!idx.valid) // qmllint disable missing-property
            return
        tree.expandToIndex(idx)
        tree.forceLayout()
        const row = tree.rowAtIndex(idx)
        if (row >= 0)
            tree.positionViewAtRow(row, TableView.AlignVCenter)
        tree.selectionModel.setCurrentIndex(idx, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        ex.select(node)
    }
    function startExtract(nodes) {
        extractPopup.nodes = nodes
        extractPopup.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: 8

        // ── archive ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppButton {
                role: "load"
                text: qsTr("scx.explorer_open_channel")
                tip: App.fmt(qsTr("scx.explorer_open_channel_tip"), { path: App.p4kPath })
                enabled: !App.busy && App.p4kPath !== ""
                onClicked: ex.open()
            }
            AppButton {
                text: qsTr("scx.explorer_open_other")
                enabled: !App.busy
                onClicked: openDialog.open()
            }
            AppButton {
                role: "open"
                text: ex.forgeLoaded ? qsTr("scx.explorer_forge_loaded") : qsTr("scx.explorer_load_forge")
                tip: qsTr("scx.explorer_load_forge_tip")
                enabled: ex.loaded && !ex.forgeLoaded && !App.busy
                onClicked: ex.loadDataForge()
            }
            AppButton {
                text: qsTr("scx.explorer_export_gamedata")
                tip: qsTr("scx.explorer_export_gamedata_tip")
                enabled: !App.busy && (ex.loaded || App.p4kPath !== "")
                onClicked: gameDataPopup.open()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: ex.loaded ? ex.summary : qsTr("scx.explorer_not_open")
                color: Theme.placeholder
                elide: Text.ElideMiddle
                Layout.maximumWidth: 520
            }
        }
        Label {
            visible: ex.loaded
            text: ex.archivePath
            color: Theme.dim
            font.pixelSize: Theme.smallFont
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            // ── tree / search ──
            ColumnLayout {
                SplitView.preferredWidth: page.width * 0.5
                SplitView.minimumWidth: 320
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: searchField
                        Layout.fillWidth: true
                        enabled: ex.loaded
                        placeholderText: qsTr("scx.explorer_search_placeholder")
                        onTextEdited: searchDelay.restart()
                        onAccepted: { searchDelay.stop(); ex.search(text) }
                        Timer { id: searchDelay; interval: 300; onTriggered: ex.search(searchField.text) }
                    }
                    AppButton {
                        text: qsTr("scx.explorer_clear_search")
                        visible: searchField.text !== ""
                        onClicked: { searchField.text = ""; ex.search("") }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.base
                    border.color: Theme.border
                    radius: Theme.radius
                    clip: true

                    // The archive tree.
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 1
                        spacing: 0
                        visible: searchField.text === ""

                        HorizontalHeaderView {
                            id: header
                            syncView: tree
                            Layout.fillWidth: true
                            clip: true
                            delegate: Rectangle {
                                required property string display
                                implicitHeight: 26
                                implicitWidth: 80
                                color: Theme.panel
                                border.color: Theme.border
                                Text {
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    verticalAlignment: Text.AlignVCenter
                                    text: parent.display
                                    color: Theme.text
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                            }
                        }
                        TreeView {
                            id: tree
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: ex.tree
                            boundsBehavior: Flickable.StopAtBounds
                            selectionBehavior: TableView.SelectRows
                            selectionMode: TableView.ExtendedSelection
                            selectionModel: ItemSelectionModel {}
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                            columnWidthProvider: c => c === 0 ? Math.max(260, width - 330) : [0, 95, 95, 140][c]
                            onWidthChanged: forceLayout()

                            Connections {
                                target: tree.selectionModel
                                function onCurrentChanged(current) {
                                    const node = ex.tree.nodeAt(current)
                                    if (node !== ex.currentNode)
                                        ex.select(node)
                                }
                            }

                            delegate: TreeViewDelegate {
                                id: cell
                                required property int column
                                required property int node
                                required property bool folder
                                implicitHeight: 24
                                font.pixelSize: Theme.fontSize
                                palette.text: Theme.text
                                palette.highlightedText: Theme.highlightedText
                                palette.highlight: Theme.highlight
                                palette.windowText: Theme.text
                                contentItem: Text {
                                    text: cell.model.display
                                    color: cell.highlighted || cell.selected ? Theme.highlightedText
                                         : cell.column === 0 ? (cell.folder ? Theme.title : Theme.text) : Theme.dim
                                    font: cell.font
                                    elide: cell.column === 0 ? Text.ElideMiddle : Text.ElideRight
                                    horizontalAlignment: cell.column === 1 || cell.column === 2 ? Text.AlignRight : Text.AlignLeft
                                    verticalAlignment: Text.AlignVCenter
                                }
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: {
                                        const idx = tree.index(cell.row, 0)
                                        if (!tree.selectionModel.isSelected(idx))
                                            tree.selectionModel.setCurrentIndex(idx, ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
                                        treeMenu.node = cell.node
                                        treeMenu.popup()
                                    }
                                }
                            }
                        }
                    }

                    // Search results.
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        visible: searchField.text !== ""
                        spacing: 4
                        Label {
                            text: ex.searching ? qsTr("scx.explorer_searching") : ex.resultsNote
                            color: Theme.placeholder
                        }
                        ListView {
                            id: resultList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: ex.results
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                id: hit
                                required property var modelData
                                required property int index
                                width: resultList.width
                                height: 24
                                color: ex.currentNode === modelData.node ? Theme.highlight
                                     : index % 2 ? Theme.alternateBase : "transparent"
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 6
                                    anchors.rightMargin: 6
                                    Text {
                                        Layout.fillWidth: true
                                        text: hit.modelData.path
                                        elide: Text.ElideMiddle
                                        color: ex.currentNode === hit.modelData.node ? Theme.highlightedText : Theme.text
                                        font.pixelSize: Theme.fontSize
                                    }
                                    Text {
                                        text: hit.modelData.size
                                        color: ex.currentNode === hit.modelData.node ? Theme.highlightedText : Theme.dim
                                        font.pixelSize: Theme.smallFont
                                    }
                                }
                                TapHandler {
                                    onTapped: ex.select(hit.modelData.node)
                                    onDoubleTapped: { searchField.text = ""; page.reveal(hit.modelData.node) }
                                }
                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: { ex.select(hit.modelData.node); treeMenu.node = hit.modelData.node; treeMenu.popup() }
                                }
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: !ex.loaded
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("scx.explorer_empty")
                        color: Theme.placeholder
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    AppButton {
                        role: "apply"
                        text: qsTr("scx.explorer_extract_selected")
                        enabled: ex.loaded && !App.busy && (ex.currentNode > 0 || tree.selectionModel.hasSelection)
                        onClicked: page.startExtract(page.selectedNodes())
                    }
                    AppButton {
                        text: qsTr("scx.explorer_reveal")
                        visible: searchField.text !== ""
                        enabled: ex.currentNode > 0
                        onClicked: { const n = ex.currentNode; searchField.text = ""; page.reveal(n) }
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            // ── preview ──
            ColumnLayout {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 300
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    text: ex.previewTitle !== "" ? ex.previewTitle : qsTr("scx.explorer_preview_none")
                    color: Theme.title
                    font.bold: true
                    elide: Text.ElideMiddle
                }
                Label {
                    Layout.fillWidth: true
                    visible: ex.previewInfo !== ""
                    text: ex.previewInfo
                    color: Theme.dim
                    font.pixelSize: Theme.smallFont + 1
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    visible: ex.forgeLoaded
                    spacing: 6
                    Label { text: qsTr("scx.explorer_pointer_depth"); color: Theme.text }
                    SpinBox { from: 0; to: 1000; value: ex.maxPointerDepth; editable: true; onValueModified: ex.maxPointerDepth = value }
                    Label { text: qsTr("scx.explorer_reference_depth"); color: Theme.text }
                    SpinBox { from: 0; to: 100; value: ex.maxReferenceDepth; editable: true; onValueModified: ex.maxReferenceDepth = value }
                    ToolTip.visible: depthHover.hovered
                    ToolTip.text: qsTr("scx.explorer_depth_tip")
                    ToolTip.delay: 700
                    HoverHandler { id: depthHover }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.base
                    border.color: Theme.border
                    radius: Theme.radius

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 4
                        visible: ex.previewKind !== "none" && ex.previewKind !== "message"
                        TextArea {
                            readOnly: true
                            selectByMouse: true
                            text: ex.previewText
                            font.family: Theme.monoFont
                            font.pixelSize: Theme.smallFont + 1
                            color: Theme.text
                            wrapMode: TextEdit.NoWrap
                            background: null
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 40
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: ex.previewBusy || ex.previewKind === "message"
                        text: ex.previewBusy ? qsTr("scx.explorer_preview_loading") : ex.previewText
                        color: Theme.placeholder
                    }
                }
                RowLayout {
                    spacing: 8
                    AppButton {
                        text: qsTr("scx.explorer_copy_path")
                        enabled: ex.currentNode > 0
                        onClicked: ex.copyPath(ex.currentNode)
                    }
                    AppButton {
                        text: qsTr("scx.copy_all")
                        enabled: ex.previewText !== "" && ex.previewKind !== "message"
                        onClicked: App.copyText(ex.previewText)
                    }
                }
            }
        }
    }

    Menu {
        id: treeMenu
        property int node: -1
        MenuItem { text: qsTr("scx.explorer_extract_selected"); onTriggered: page.startExtract(page.selectedNodes()) }
        MenuItem { text: qsTr("scx.explorer_copy_path"); onTriggered: ex.copyPath(treeMenu.node) }
    }

    FileDialog {
        id: openDialog
        title: qsTr("scx.explorer_open_other")
        fileMode: FileDialog.OpenFile
        nameFilters: ["P4K archives (*.p4k)", "All files (*)"]
        onAccepted: ex.openUrl(selectedFile)
    }

    // ── extract options ──
    Popup {
        id: extractPopup
        property var nodes: []
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(page.width - 80, 640)
        padding: 18
        onAboutToShow: outputField.text = ex.defaultExtractDir()
        background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: 6 }

        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label { text: qsTr("scx.explorer_extract_title"); color: Theme.title; font.bold: true; font.pixelSize: 15 }
            Label {
                text: App.fmt(qsTr("scx.explorer_extract_count"), { count: extractPopup.nodes.length })
                color: Theme.dim
            }
            Label { text: qsTr("scx.explorer_extract_output"); color: Theme.text }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: outputField; Layout.fillWidth: true }
                AppButton {
                    text: qsTr("scx.browse")
                    onClicked: { folderDialog.currentFolder = App.pathToUrl(outputField.text); folderDialog.open() }
                }
            }
            AppCheckBox { id: convertCheck; text: qsTr("scx.explorer_convert_cryxml"); tip: qsTr("scx.explorer_convert_cryxml_tip"); checked: true }
            AppCheckBox { id: skipCheck; text: qsTr("scx.explorer_skip_existing"); checked: false }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AppButton { text: qsTr("scx.cancel"); onClicked: extractPopup.close() }
                AppButton {
                    role: "apply"
                    text: qsTr("scx.explorer_extract_go")
                    enabled: outputField.text !== ""
                    onClicked: {
                        extractPopup.close()
                        ex.extract(extractPopup.nodes, App.pathToUrl(outputField.text), convertCheck.checked, skipCheck.checked)
                    }
                }
            }
        }
    }
    FolderDialog {
        id: folderDialog
        onAccepted: outputField.text = App.urlToPath(selectedFolder)
    }

    // ── game data export ──
    Popup {
        id: gameDataPopup
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(page.width - 80, 720)
        padding: 18
        background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: 6 }
        onAboutToShow: {
            channelField.text = App.channel
            baseField.text = App.baseIniPath
            overlayField.text = ""
            gameOutField.text = ex.defaultGameDataPath()
        }

        property string picking: ""

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label { text: qsTr("scx.explorer_gamedata_title"); color: Theme.title; font.bold: true; font.pixelSize: 15 }
            Label { text: qsTr("scx.explorer_gamedata_desc"); color: Theme.dim; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            GridLayout {
                columns: 3
                Layout.fillWidth: true
                columnSpacing: 8
                Label { text: qsTr("scx.explorer_gamedata_channel"); color: Theme.text }
                TextField { id: channelField; Layout.fillWidth: true }
                Item {}
                Label { text: qsTr("scx.explorer_gamedata_base"); color: Theme.text }
                TextField { id: baseField; Layout.fillWidth: true; placeholderText: qsTr("scx.explorer_gamedata_base_none") }
                AppButton { text: qsTr("scx.browse"); onClicked: { gameDataPopup.picking = "base"; gameFileDialog.fileMode = FileDialog.OpenFile; gameFileDialog.open() } }
                Label { text: qsTr("scx.explorer_gamedata_overlay"); color: Theme.text }
                TextField { id: overlayField; Layout.fillWidth: true; placeholderText: qsTr("scx.explorer_gamedata_overlay_none") }
                AppButton { text: qsTr("scx.browse"); onClicked: { gameDataPopup.picking = "overlay"; gameFileDialog.fileMode = FileDialog.OpenFile; gameFileDialog.open() } }
                Label { text: qsTr("scx.explorer_gamedata_output"); color: Theme.text }
                TextField { id: gameOutField; Layout.fillWidth: true }
                AppButton { text: qsTr("scx.browse"); onClicked: { gameDataPopup.picking = "output"; gameFileDialog.fileMode = FileDialog.SaveFile; gameFileDialog.open() } }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AppButton { text: qsTr("scx.cancel"); onClicked: gameDataPopup.close() }
                AppButton {
                    role: "apply"
                    text: qsTr("scx.explorer_gamedata_go")
                    enabled: gameOutField.text !== ""
                    onClicked: {
                        gameDataPopup.close()
                        ex.exportGameData(App.pathToUrl(gameOutField.text), channelField.text,
                                          baseField.text !== "" ? App.pathToUrl(baseField.text) : "",
                                          overlayField.text !== "" ? App.pathToUrl(overlayField.text) : "")
                    }
                }
            }
        }
    }
    FileDialog {
        id: gameFileDialog
        nameFilters: gameDataPopup.picking === "base" ? ["INI files (*.ini)", "All files (*)"] : ["JSON files (*.json)", "All files (*)"]
        onAccepted: {
            const p = App.urlToPath(selectedFile)
            if (gameDataPopup.picking === "base") baseField.text = p
            else if (gameDataPopup.picking === "overlay") overlayField.text = p
            else gameOutField.text = p
        }
    }
}
