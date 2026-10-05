pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQml.Models
import ScApp

// The String Editor: every localization string with its stock, current and
// custom values. A search box and filters across the top, per-column
// filters under the header, inline editing of Custom Value and the ship
// sort order, a side editor for long text and a rendered preview of the
// selected row.
Item {
    id: page

    readonly property var model: App.strings
    property int currentRow: -1
    property int widthsRevision: 0
    property bool widthsRestored: false
    property int dataRevision: 0
    property bool editorOpen: false

    function rowValid(row) { return row >= 0 && row < model.visibleCount }

    function selectRow(row) {
        if (!rowValid(row)) {
            selection.clear()
            currentRow = -1
            return
        }
        selection.setCurrentIndex(model.index(row, 0), ItemSelectionModel.ClearAndSelect | ItemSelectionModel.Rows)
        currentRow = row
    }

    Connections {
        target: page.model
        function onDataReset() { page.selectRow(-1) }
        function onRowsRelaid() {
            page.selectRow(-1)
            page.dataRevision++
        }
        function onDataChanged() { page.dataRevision++ }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        // ── actions ──
        RowLayout {
            spacing: Theme.spacing
            objectName: "stringsToolbar"

            AppButton {
                objectName: "applyButton"
                text: qsTr("toolbar.apply_btn")
                role: App.applyDirty ? "needsApply" : "apply"
                enabled: App.applyDirty && !App.busy
                tip: App.applyDirty ? qsTr("toolbar.apply_enabled_tooltip") : qsTr("toolbar.apply_disabled_tooltip")
                onClicked: App.applyToGame()
            }
            AppButton {
                objectName: "editorButton"
                text: qsTr("toolbar.editor_btn")
                role: "open"
                checkable: true
                checked: page.editorOpen
                tip: qsTr("toolbar.editor_tooltip")
                onClicked: page.editorOpen = !page.editorOpen
            }
            AppButton {
                text: qsTr("toolbar.restore_backup_btn")
                role: "restore"
                tip: qsTr("toolbar.restore_backup_tooltip")
                onClicked: backupPicker.open()
            }
            AppButton {
                id: moreButton
                text: qsTr("toolbar.more_btn") + "  ▾"
                role: "clear"
                tip: qsTr("toolbar.more_tooltip")
                onClicked: moreMenu.popup(moreButton, 0, moreButton.height)

                Menu {
                    id: moreMenu
                    MenuItem { text: qsTr("toolbar.menu_clear_localization"); onTriggered: App.clearLocalization() }
                    MenuItem { text: qsTr("toolbar.menu_clear_cache"); onTriggered: App.clearCache() }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("toolbar.menu_export_ini")
                        onTriggered: {
                            exportDialog.selectedFile = App.pathToUrl(App.defaultLocPackPath())
                            exportDialog.open()
                        }
                    }
                    MenuItem { text: qsTr("toolbar.open_loc_dir_btn"); onTriggered: App.openLocalizationDir() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("toolbar.menu_reset_window_proportions"); onTriggered: App.windowLayout.resetWindowProportions() }
                    MenuItem { text: qsTr("toolbar.menu_switch_to_simple"); onTriggered: App.uiMode = "simple" }
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: App.fmt(qsTr("strings_tab.showing_count"), {
                    shown: page.model.visibleCount.toLocaleString(Qt.locale("en_US"), "f", 0),
                    total: page.model.totalCount.toLocaleString(Qt.locale("en_US"), "f", 0)
                })
                color: Theme.placeholder
            }
        }

        // ── filters ──
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacing
            objectName: "stringsFilters"

            TextField {
                id: searchField
                objectName: "stringsSearch"
                width: 260
                height: 30
                placeholderText: qsTr("filters.search_placeholder")
                selectByMouse: true
                onTextEdited: searchDelay.restart()
                onAccepted: { searchDelay.stop(); page.model.searchText = text }
                Keys.onEscapePressed: { searchDelay.stop(); text = ""; page.model.searchText = "" }
                Timer { id: searchDelay; interval: 250; onTriggered: page.model.searchText = searchField.text }
                ToolTip.visible: hovered && text === ""
                ToolTip.text: qsTr("filters.search_tooltip")
                ToolTip.delay: 800
            }
            Label { text: qsTr("filters.category_label"); color: Theme.text; height: 30; verticalAlignment: Text.AlignVCenter }
            ComboBox {
                id: categoryCombo
                width: 190
                model: ["All"].concat(page.model.categories)
                displayText: currentIndex <= 0 ? qsTr("filters.status_all") : currentText
                currentIndex: Math.max(0, model.indexOf(page.model.categoryFilter))
                onActivated: (i) => page.model.categoryFilter = model[i]
                ToolTip.visible: hovered
                ToolTip.text: qsTr("filters.category_tooltip")
                ToolTip.delay: 600
            }
            Label { text: qsTr("filters.status_label"); color: Theme.text; height: 30; verticalAlignment: Text.AlignVCenter }
            ComboBox {
                width: 130
                textRole: "label"
                valueRole: "value"
                model: [
                    { value: "All", label: qsTr("filters.status_all") },
                    { value: "Modified", label: qsTr("filters.status_modified") },
                    { value: "Enhanced", label: qsTr("filters.status_enhanced") },
                    { value: "Unmodified", label: qsTr("filters.status_unmodified") },
                    { value: "New", label: qsTr("filters.status_new") }
                ]
                currentIndex: Math.max(0, ["All", "Modified", "Enhanced", "Unmodified", "New"].indexOf(page.model.statusFilter))
                onActivated: page.model.statusFilter = currentValue
                ToolTip.visible: hovered
                ToolTip.text: qsTr("filters.status_tooltip")
                ToolTip.delay: 600
            }
            CheckBox {
                text: qsTr("filters.hide_unmodified")
                checked: page.model.hideUnmodified
                onToggled: page.model.hideUnmodified = checked
                ToolTip.visible: hovered; ToolTip.text: qsTr("filters.hide_unmodified_tooltip"); ToolTip.delay: 600
            }
            CheckBox {
                text: qsTr("filters.ship_vehicle_names_only")
                checked: page.model.shipNamesOnly
                onToggled: page.model.shipNamesOnly = checked
                ToolTip.visible: hovered; ToolTip.text: qsTr("filters.ship_vehicle_names_only_tooltip"); ToolTip.delay: 600
            }
            CheckBox {
                text: qsTr("filters.favorites_only")
                checked: page.model.favoritesOnly
                onToggled: page.model.favoritesOnly = checked
                ToolTip.visible: hovered; ToolTip.text: qsTr("filters.favorites_only_tooltip"); ToolTip.delay: 600
            }
            CheckBox {
                text: qsTr("filters.bp_titles_only")
                checked: page.model.bpTitlesOnly
                onToggled: page.model.bpTitlesOnly = checked
                ToolTip.visible: hovered; ToolTip.text: qsTr("filters.bp_titles_only_tooltip"); ToolTip.delay: 600
            }
            CheckBox {
                text: qsTr("filters.bp_descs_only")
                checked: page.model.bpDescsOnly
                onToggled: page.model.bpDescsOnly = checked
                ToolTip.visible: hovered; ToolTip.text: qsTr("filters.bp_descs_only_tooltip"); ToolTip.delay: 600
            }
            AppButton {
                text: qsTr("filters.group_sort_btn")
                checkable: true
                checked: page.model.groupedSort
                tip: qsTr("filters.group_sort_tooltip")
                onClicked: page.model.sortGrouped()
            }
            AppButton {
                text: qsTr("filters.clear_filters_btn")
                tip: qsTr("filters.clear_filters_tooltip")
                onClicked: {
                    page.model.clearFilters()
                    filterRow.clearAll()
                    searchDelay.stop()
                    searchField.text = ""
                }
            }
            AppButton {
                text: qsTr("filters.copy_filtered_btn")
                tip: qsTr("filters.copy_filtered_tooltip")
                onClicked: App.copyFilteredRows()
            }
        }

        // ── table + side editor ──
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            Rectangle {
                SplitView.fillWidth: true
                color: Theme.base
                border.color: Theme.border
                clip: true

                HorizontalHeaderView {
                    id: header
                    anchors.left: table.left
                    anchors.top: parent.top
                    syncView: table
                    clip: true
                    resizableColumns: true
                    delegate: Rectangle {
                        id: headerCell
                        required property int index
                        required property string display
                        implicitHeight: 28
                        implicitWidth: 60
                        color: Theme.button
                        border.color: Theme.border
                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 14
                            text: headerCell.display
                            color: Theme.text
                            font.bold: true
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: (headerCell.index === 4 || headerCell.index === 5 || headerCell.index === 8)
                                                 ? Text.AlignHCenter : Text.AlignLeft
                        }
                        Label {
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            visible: page.model.sortColumn === headerCell.index
                            text: page.model.groupedSort ? "≡" : (page.model.sortDescending ? "▼" : "▲")
                            color: Theme.title
                            font.pixelSize: 9
                        }
                        TapHandler { onTapped: page.model.sortBy(headerCell.index) }
                    }
                }

                // Per-column filter boxes, kept aligned with the columns.
                Row {
                    id: filterRow
                    objectName: "stringsFilterRow"
                    anchors.left: table.left
                    anchors.top: header.bottom
                    x: -table.contentX
                    height: 28
                    clip: false
                    function clearAll() {
                        for (let i = 0; i < repeater.count; ++i)
                            (repeater.itemAt(i) as TextField).text = ""
                    }
                    Repeater {
                        id: repeater
                        model: 9
                        delegate: TextField {
                            required property int index
                            width: { void page.widthsRevision; return Math.max(0, table.columnWidth(index)) }
                            height: 28
                            placeholderText: index === 8 ? "" : "⌕"
                            enabled: index !== 8
                            font.pixelSize: Theme.smallFont
                            color: Theme.text
                            placeholderTextColor: Theme.placeholder
                            background: Rectangle { color: Theme.alternateBase; border.color: Theme.border }
                            onTextChanged: filterTimer.restart()
                            Timer {
                                id: filterTimer
                                interval: 250
                                onTriggered: page.model.setColumnFilter(parent.index, parent.text)
                            }
                            ToolTip.visible: hovered && index !== 8
                            ToolTip.text: App.fmt(qsTr("strings_tab.column_filter_tooltip"), { column: page.model.headerData(index, Qt.Horizontal) })
                            ToolTip.delay: 800
                        }
                    }
                }

                TableView {
                    id: table
                    objectName: "stringsTable"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: filterRow.bottom
                    anchors.bottom: parent.bottom
                    anchors.margins: 1
                    clip: true
                    model: page.model
                    boundsBehavior: Flickable.StopAtBounds
                    resizableColumns: true
                    selectionBehavior: TableView.SelectRows
                    selectionMode: TableView.SingleSelection
                    editTriggers: TableView.DoubleTapped | TableView.EditKeyPressed | TableView.SelectedTapped
                    selectionModel: ItemSelectionModel {
                        id: selection
                        model: page.model
                        onCurrentChanged: (current) => page.currentRow = current.row
                    }
                    // Spare width goes to the text columns the user has not
                    // sized, in proportion; to the last column if all are sized.
                    readonly property var stretchColumns: [1, 2, 3, 6]
                    function baseWidth(column) {
                        const w = explicitColumnWidth(column)
                        return w >= 0 ? w : page.model.defaultColumnWidth(column)
                    }
                    columnWidthProvider: (column) => {
                        const base = baseWidth(column)
                        let total = 0
                        let flex = 0
                        for (let c = 0; c < columns; ++c) {
                            total += baseWidth(c)
                            if (stretchColumns.includes(c) && explicitColumnWidth(c) < 0)
                                flex += baseWidth(c)
                        }
                        const spare = width - total
                        if (spare <= 0)
                            return base
                        if (flex > 0)
                            return stretchColumns.includes(column) && explicitColumnWidth(column) < 0
                                   ? base + spare * base / flex : base
                        return column === columns - 1 ? base + spare : base
                    }
                    onWidthChanged: forceLayout()
                    rowHeightProvider: () => 26
                    onLayoutChanged: {
                        page.widthsRevision++
                        if (page.widthsRestored)
                            saveWidths.restart()
                    }
                    Component.onCompleted: {
                        const saved = App.windowLayout.columnWidths()
                        for (let c = 0; c < saved.length && c < columns; ++c)
                            if (saved[c] >= 0)
                                setColumnWidth(c, saved[c])
                        page.widthsRestored = true
                    }
                    // Persist widths the user dragged (debounced).
                    Timer {
                        id: saveWidths
                        interval: 800
                        onTriggered: {
                            const widths = []
                            let any = false
                            for (let c = 0; c < table.columns; ++c) {
                                const w = table.explicitColumnWidth(c)
                                widths.push(w >= 0 ? Math.round(w) : -1)
                                any = any || w >= 0
                            }
                            if (any)
                                App.windowLayout.saveColumnWidths(widths)
                        }
                    }
                    Connections {
                        target: App.windowLayout
                        function onWindowProportionsReset() { table.clearColumnWidths(); table.forceLayout() }
                    }
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        id: cell
                        required property int row
                        required property int column
                        required property string display
                        required property var foreground
                        required property bool favoriteRow
                        required property bool centered
                        required property string kind
                        required property string tooltip
                        required property bool editable
                        required property bool selected
                        required property bool current

                        implicitHeight: 26
                        color: selected ? Theme.highlight
                             : favoriteRow ? Theme.favoriteRow
                             : (row % 2 ? Theme.alternateBase : Theme.base)
                        border.color: current ? Theme.title : "transparent"
                        border.width: current ? 1 : 0

                        Label {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            text: cell.display
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            horizontalAlignment: cell.centered ? Text.AlignHCenter : Text.AlignLeft
                            font.pixelSize: cell.kind === "star" || cell.kind === "owned" ? 15 : Theme.fontSize
                            color: cell.selected ? Theme.highlightedText
                                 : (cell.foreground !== undefined && cell.foreground !== null && String(cell.foreground) !== "")
                                   ? cell.foreground : Theme.text
                        }

                        HoverHandler { id: hover }
                        ToolTip.visible: hover.hovered && cell.tooltip !== "" && cell.tooltip.length > 0
                        ToolTip.text: cell.tooltip.length > 400 ? cell.tooltip.substring(0, 400) + "…" : cell.tooltip
                        ToolTip.delay: 900

                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: {
                                page.selectRow(cell.row)
                                if (cell.kind === "star" && page.model.isFavoritable(cell.row))
                                    page.model.toggleFavorite(cell.row)
                            }
                            onDoubleTapped: {
                                if (cell.column === 3 && cell.display !== "") {
                                    page.model.setCustom(cell.row, cell.display)
                                    table.edit(page.model.index(cell.row, 6))
                                }
                            }
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: (point) => {
                                page.selectRow(cell.row)
                                contextMenu.row = cell.row
                                contextMenu.cellText = cell.display
                                contextMenu.popup()
                            }
                        }

                        TableView.editDelegate: TextField {
                            required property int row
                            required property int column
                            required property var edit
                            text: edit === undefined || edit === null ? "" : edit
                            font.pixelSize: Theme.fontSize
                            color: Theme.text
                            horizontalAlignment: column === 5 ? Text.AlignHCenter : Text.AlignLeft
                            validator: column === 5 ? orderValidator : null
                            background: Rectangle { color: Theme.base; border.color: Theme.highlight; border.width: 2 }
                            Component.onCompleted: selectAll()
                            TableView.onCommit: page.model.setData(page.model.index(row, column), text)
                        }
                    }

                }

                RegularExpressionValidator {
                    id: orderValidator
                    regularExpression: /\d{0,2}/
                }

                Label {
                    anchors.centerIn: parent
                    visible: page.model.totalCount === 0 && !App.busy
                    text: qsTr("strings_tab.no_data")
                    color: Theme.placeholder
                    font.pixelSize: 16
                }
            }

            // ── side editor ──
            Rectangle {
                visible: page.editorOpen
                SplitView.preferredWidth: 380
                SplitView.minimumWidth: 240
                color: Theme.panel
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacing
                    spacing: 6

                    Label {
                        text: qsTr("strings_tab.editor_dock_title")
                        color: Theme.title
                        font.bold: true
                    }
                    Label {
                        Layout.fillWidth: true
                        text: page.rowValid(page.currentRow) ? page.model.key(page.currentRow) : qsTr("strings_tab.no_row_selected")
                        color: Theme.placeholder
                        font.family: Theme.monoFont
                        font.pixelSize: Theme.smallFont
                        elide: Text.ElideMiddle
                    }
                    RowLayout {
                        AppButton {
                            text: qsTr("strings_tab.editor_underline_btn")
                            tip: qsTr("strings_tab.editor_underline_tooltip")
                            enabled: page.rowValid(page.currentRow)
                            onClicked: editorArea.wrap("EM3")
                        }
                        AppButton {
                            text: qsTr("strings_tab.editor_highlight_btn")
                            tip: qsTr("strings_tab.editor_highlight_tooltip")
                            enabled: page.rowValid(page.currentRow)
                            onClicked: editorArea.wrap("EM4")
                        }
                    }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        TextArea {
                            id: editorArea
                            property bool loading: false
                            enabled: page.rowValid(page.currentRow)
                            wrapMode: TextEdit.Wrap
                            color: Theme.text
                            placeholderText: qsTr("strings_tab.editor_placeholder")
                            placeholderTextColor: Theme.placeholder
                            selectByMouse: true
                            background: Rectangle { color: Theme.base; border.color: Theme.border }

                            function load() {
                                loading = true
                                text = page.rowValid(page.currentRow) ? page.model.customValue(page.currentRow) : ""
                                loading = false
                            }
                            function wrap(tag) {
                                const a = selectionStart, b = selectionEnd
                                const inner = text.substring(a, b)
                                remove(a, b)
                                insert(a, "<" + tag + ">" + inner + "</" + tag + ">")
                            }
                            onTextChanged: if (!loading) commitTimer.restart()
                            Timer {
                                id: commitTimer
                                interval: 300
                                onTriggered: if (page.rowValid(page.currentRow)) page.model.setCustom(page.currentRow, editorArea.text)
                            }
                            Connections {
                                target: page
                                function onCurrentRowChanged() {
                                    if (commitTimer.running) {
                                        commitTimer.stop()
                                    }
                                    editorArea.load()
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── preview ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            color: Theme.base
            border.color: Theme.border
            radius: Theme.radius
            objectName: "stringsPreview"

            ScrollView {
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                Text {
                    width: parent.width
                    textFormat: Text.RichText
                    wrapMode: Text.Wrap
                    color: Theme.text
                    text: { void page.dataRevision; return page.rowValid(page.currentRow) ? page.model.previewHtml(page.currentRow) : "" }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: !page.rowValid(page.currentRow)
                text: qsTr("strings_tab.preview_placeholder")
                color: Theme.placeholder
            }
        }
    }

    Menu {
        id: contextMenu
        property int row: -1
        property string cellText: ""
        MenuItem { text: qsTr("strings_tab.context_copy_cell"); onTriggered: App.copyText(contextMenu.cellText) }
        MenuItem {
            text: qsTr("strings_tab.context_copy_key")
            onTriggered: {
                const key = page.model.key(contextMenu.row)
                App.copyText(key)
                App.setStatus(App.fmt(qsTr("strings_tab.copied_key"), { loc_key: key }))
            }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("strings_tab.context_edit"); onTriggered: table.edit(page.model.index(contextMenu.row, 6)) }
        MenuItem { text: qsTr("strings_tab.context_reset_to_original"); onTriggered: page.model.resetRow(contextMenu.row) }
        MenuSeparator {}
        MenuItem { text: qsTr("strings_tab.context_copy_all_filtered"); onTriggered: App.copyFilteredRows() }
        MenuSeparator { visible: page.model.isFavoritable(contextMenu.row) }
        MenuItem {
            visible: page.model.isFavoritable(contextMenu.row)
            height: visible ? implicitHeight : 0
            text: page.model.customValue(contextMenu.row).startsWith(App.favoritePrefix)
                  ? qsTr("strings_tab.context_remove_favorite") : qsTr("strings_tab.context_add_favorite")
            onTriggered: page.model.toggleFavorite(contextMenu.row)
        }
    }

    // Restore Backup: pick one of the kept global.ini backups.
    Popup {
        id: backupPicker
        anchors.centerIn: Overlay.overlay
        width: 520
        modal: true
        padding: 16
        property var items: []
        onAboutToShow: { items = App.backups(); list.currentIndex = items.length ? 0 : -1 }
        background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: qsTr("restore_backup.select_file_title"); color: Theme.title; font.bold: true; font.pixelSize: 15 }
            Label {
                visible: backupPicker.items.length === 0
                text: qsTr("scx.no_backups")
                color: Theme.placeholder
            }
            ListView {
                id: list
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 240)
                clip: true
                model: backupPicker.items
                delegate: ItemDelegate {
                    required property int index
                    required property var modelData
                    width: list.width
                    highlighted: list.currentIndex === index
                    text: modelData.name + "   —   " + Qt.formatDateTime(modelData.time, "yyyy-MM-dd hh:mm:ss")
                    onClicked: list.currentIndex = index
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AppButton {
                    text: qsTr("toolbar.restore_backup_btn")
                    role: "restore"
                    enabled: list.currentIndex >= 0
                    onClicked: {
                        const path = backupPicker.items[list.currentIndex].path
                        backupPicker.close()
                        App.restoreBackup(path)
                    }
                }
                AppButton { text: qsTr("scx.cancel"); onClicked: backupPicker.close() }
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("dialogs.export_loc_pack_title")
        fileMode: FileDialog.SaveFile
        nameFilters: ["Zip files (*.zip)", "All files (*)"]
        onAccepted: App.exportLocPack(selectedFile)
    }
}
