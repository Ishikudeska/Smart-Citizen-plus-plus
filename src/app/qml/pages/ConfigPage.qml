import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import ScApp

// The Config page: the game install, channel and language, data folders,
// Data.p4k extraction, user.ini tools, settings backups and appearance.
Item {
    id: page

    ConfigController { id: config }

    Connections {
        target: config
        function onImportReady() { conflictDialog.open() }
    }

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroller.availableWidth - 32
            x: 16
            spacing: 14

            Item { implicitHeight: 4 }
            Label {
                text: qsTr("config.title")
                color: Theme.title
                font.pixelSize: 20
                font.bold: true
            }
            Label {
                text: qsTr("config.instructions")
                color: Theme.dim
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            // ── Star Citizen ──
            Section {
                Layout.fillWidth: true
                title: qsTr("config.star_citizen_group")
                description: qsTr("config.installation_desc")
                objectName: "configInstall"

                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: installField
                        Layout.fillWidth: true
                        text: App.installRoot
                        placeholderText: qsTr("config.game_path_placeholder")
                        color: Theme.text
                        placeholderTextColor: Theme.placeholder
                        background: Rectangle { color: Theme.base; border.color: Theme.border; radius: Theme.radius }
                        onAccepted: App.installRoot = text
                        ToolTip.visible: hovered; ToolTip.text: qsTr("config.game_path_tooltip"); ToolTip.delay: 800
                    }
                    AppButton {
                        text: qsTr("config.browse_btn")
                        tip: qsTr("config.browse_game_tooltip")
                        onClicked: { installDialog.currentFolder = App.pathToUrl(App.installRoot); installDialog.open() }
                    }
                    AppButton {
                        text: qsTr("scx.detect_btn")
                        tip: qsTr("scx.detect_tooltip")
                        onClicked: App.detectInstall()
                    }
                }
                GridLayout {
                    columns: 4
                    columnSpacing: 10
                    rowSpacing: 8
                    Label { text: qsTr("config.channel_label"); color: Theme.text }
                    ComboBox {
                        id: channelBox
                        implicitWidth: 180
                        model: App.channels
                        currentIndex: App.channels.indexOf(App.channel)
                        enabled: !App.busy
                        onActivated: (i) => App.channel = App.channels[i]
                        ToolTip.visible: hovered; ToolTip.text: qsTr("config.channel_tooltip"); ToolTip.delay: 800
                    }
                    Label {
                        Layout.columnSpan: 2
                        visible: App.installRoot !== "" && App.installedChannels.indexOf(App.channel) < 0
                        text: App.fmt(qsTr("config.channel_not_installed_hint"), { channel: App.channel })
                        color: Theme.action("restore")
                    }
                    Item { Layout.columnSpan: 2; visible: !(App.installRoot !== "" && App.installedChannels.indexOf(App.channel) < 0) }

                    Label { text: qsTr("config.language_label"); color: Theme.text }
                    ComboBox {
                        implicitWidth: 180
                        textRole: "name"
                        valueRole: "id"
                        model: App.languages
                        currentIndex: App.languages.findIndex(l => l.id === App.language)
                        enabled: !App.busy
                        onActivated: App.language = currentValue
                        ToolTip.visible: hovered; ToolTip.text: qsTr("config.language_tooltip"); ToolTip.delay: 800
                    }
                    AppButton {
                        text: qsTr("config.map_language_btn")
                        tip: qsTr("config.map_language_tooltip")
                        onClicked: languageSources.open()
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            // ── Data.p4k ──
            Section {
                Layout.fillWidth: true
                objectName: "configP4k"
                title: qsTr("config.p4k_group")
                description: qsTr("config.p4k_desc")

                RowLayout {
                    spacing: 10
                    Rectangle {
                        width: 12; height: 12; radius: 6
                        color: App.p4kStatus === "fresh" ? Theme.action("apply")
                             : App.p4kStatus === "stale" ? Theme.action("restore") : Theme.action("needsApply")
                    }
                    Label {
                        Layout.fillWidth: true
                        color: Theme.text
                        wrapMode: Text.WordWrap
                        text: App.installRoot === "" ? qsTr("config.p4k_status_no_path")
                            : App.p4kStatus === "noarchive" ? App.fmt(qsTr("config.p4k_status_not_found"), { path: App.p4kPath })
                            : App.p4kStatus === "missing" ? qsTr("config.p4k_status_found_no_base")
                            : App.p4kStatus === "stale" ? qsTr("scx.p4k_status_stale")
                            : qsTr("scx.p4k_status_fresh")
                    }
                    AppButton {
                        text: qsTr("config.extract_btn")
                        role: App.p4kStatus === "fresh" ? "open" : "needsApply"
                        enabled: App.p4kStatus !== "noarchive" && !App.busy
                        tip: qsTr("config.extract_tooltip")
                        onClicked: App.extractFromP4k(true)
                    }
                }
            }

            // ── data folders ──
            Section {
                Layout.fillWidth: true
                objectName: "configData"
                title: qsTr("scx.data_group")
                description: qsTr("scx.data_desc")

                Label { text: qsTr("config.app_data_label"); color: Theme.text }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        Layout.fillWidth: true
                        text: config.dataDir
                        color: Theme.text
                        background: Rectangle { color: Theme.base; border.color: Theme.border; radius: Theme.radius }
                        onAccepted: config.setDataDir(text)
                        ToolTip.visible: hovered; ToolTip.text: qsTr("config.data_dir_tooltip"); ToolTip.delay: 800
                    }
                    AppButton {
                        text: qsTr("config.browse_btn")
                        tip: qsTr("config.browse_data_tooltip")
                        onClicked: { dataDialog.currentFolder = App.pathToUrl(config.dataDir); dataDialog.open() }
                    }
                    AppButton {
                        text: qsTr("config.reset_btn")
                        enabled: config.dataDirOverridden
                        tip: qsTr("config.reset_data_tooltip")
                        onClicked: config.resetDataDir()
                    }
                    AppButton { text: qsTr("scx.open_btn"); onClicked: App.openFolder(config.dataDir) }
                }
                Label { text: qsTr("config.dataforge_cache_label"); color: Theme.text }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        Layout.fillWidth: true
                        text: config.dataForgeBase
                        color: Theme.text
                        background: Rectangle { color: Theme.base; border.color: Theme.border; radius: Theme.radius }
                        onAccepted: config.setDataForgeBase(text)
                        ToolTip.visible: hovered; ToolTip.text: qsTr("config.cache_dir_tooltip"); ToolTip.delay: 800
                    }
                    AppButton {
                        text: qsTr("config.browse_btn")
                        tip: qsTr("config.browse_cache_tooltip")
                        onClicked: { cacheDialog.currentFolder = App.pathToUrl(config.dataForgeBase); cacheDialog.open() }
                    }
                    AppButton {
                        text: qsTr("config.reset_btn")
                        enabled: config.dataForgeOverridden
                        tip: qsTr("config.reset_cache_tooltip")
                        onClicked: config.resetDataForgeBase()
                    }
                }
            }

            // ── user.ini tools ──
            Section {
                Layout.fillWidth: true
                objectName: "configTools"
                title: qsTr("config.tools_group")
                description: qsTr("config.tools_desc")

                AppCheckBox {
                    text: qsTr("config.include_new_cb")
                    tip: qsTr("config.include_new_tooltip")
                    checked: config.includeNewLines
                    onToggled: config.includeNewLines = checked
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    AppButton { text: qsTr("config.import_ini_btn"); role: "open"; tip: qsTr("config.import_ini_tooltip"); onClicked: importSource.open() }
                    AppButton { text: qsTr("config.reset_user_ini_btn"); role: "restore"; tip: qsTr("config.reset_user_ini_tooltip"); onClicked: config.resetUserIni() }
                    AppButton { text: qsTr("config.restore_user_ini_btn"); tip: qsTr("config.restore_user_ini_tooltip"); onClicked: config.restoreUserIni() }
                    AppButton { text: qsTr("config.preview_apply_btn"); tip: qsTr("config.preview_apply_tooltip"); onClicked: config.previewApply() }
                }
            }

            // ── settings backup ──
            Section {
                Layout.fillWidth: true
                title: qsTr("config.backup_group")
                description: qsTr("config.backup_desc")

                RowLayout {
                    AppButton {
                        text: qsTr("config.export_settings_btn")
                        tip: qsTr("config.export_settings_tooltip")
                        onClicked: { exportSettingsDialog.selectedFile = App.pathToUrl(config.defaultSettingsBackupPath()); exportSettingsDialog.open() }
                    }
                    AppButton {
                        text: qsTr("config.import_settings_btn")
                        tip: qsTr("config.import_settings_tooltip")
                        onClicked: importSettingsDialog.open()
                    }
                }
            }

            // ── appearance ──
            Section {
                Layout.fillWidth: true
                objectName: "configAppearance"
                title: qsTr("config.appearance_group")

                RowLayout {
                    spacing: 10
                    Label { text: qsTr("config.theme_label"); color: Theme.text }
                    ComboBox {
                        implicitWidth: 160
                        textRole: "label"
                        valueRole: "id"
                        model: [
                            { id: "dark", label: qsTr("config.theme_dark") },
                            { id: "light", label: qsTr("config.theme_light") }
                        ]
                        currentIndex: ["dark", "light"].indexOf(App.theme)
                        onActivated: App.theme = currentValue
                        ToolTip.visible: hovered; ToolTip.text: qsTr("config.theme_tooltip"); ToolTip.delay: 800
                    }
                    Item { width: 20 }
                    Label { text: qsTr("scx.favorite_prefix_label"); color: Theme.text }
                    TextField {
                        implicitWidth: 60
                        text: App.favoritePrefix
                        maximumLength: 3
                        color: Theme.text
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle { color: Theme.base; border.color: Theme.border; radius: Theme.radius }
                        onEditingFinished: if (text !== "" && text !== App.favoritePrefix) App.favoritePrefix = text
                        ToolTip.visible: hovered; ToolTip.text: qsTr("scx.favorite_prefix_tooltip"); ToolTip.delay: 800
                    }
                    Item { width: 20 }
                    AppCheckBox {
                        text: qsTr("config.disable_tutorial_cb")
                        tip: qsTr("config.disable_tutorial_tooltip")
                        checked: config.tutorialDisabled
                        onToggled: config.tutorialDisabled = checked
                    }
                    Item { width: 20 }
                    AppButton {
                        text: qsTr("config.check_updates_btn")
                        tip: qsTr("config.check_updates_tooltip")
                        visible: App.updateCheckEnabled
                        onClicked: App.checkForUpdates(true)
                    }
                }
            }
            Item { implicitHeight: 12 }
        }
    }

    FolderDialog {
        id: installDialog
        title: qsTr("config.select_sc_root")
        onAccepted: App.installRoot = App.urlToPath(selectedFolder)
    }
    FolderDialog {
        id: dataDialog
        title: qsTr("config.select_data_folder")
        onAccepted: config.setDataDir(App.urlToPath(selectedFolder))
    }
    FolderDialog {
        id: cacheDialog
        title: qsTr("config.select_cache_folder")
        onAccepted: config.setDataForgeBase(App.urlToPath(selectedFolder))
    }
    FileDialog {
        id: exportSettingsDialog
        title: qsTr("settings_backup.export_dialog_title")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("settings_backup.zip_filter")]
        onAccepted: config.exportSettings(selectedFile)
    }
    FileDialog {
        id: importSettingsDialog
        title: qsTr("settings_backup.import_dialog_title")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("settings_backup.zip_filter")]
        onAccepted: config.importSettings(selectedFile)
    }
    FileDialog {
        id: iniFileDialog
        title: qsTr("import_flow.select_ini_file_title")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("import_flow.ini_file_filter")]
        onAccepted: importSourceField.text = App.urlToPath(selectedFile)
    }

    // Import INI: a file or a URL.
    Popup {
        id: importSource
        anchors.centerIn: Overlay.overlay
        width: 600
        modal: true
        padding: 16
        background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: qsTr("import_flow.source_dialog_title"); color: Theme.title; font.bold: true; font.pixelSize: 15 }
            Label { text: qsTr("import_flow.source_dialog_label"); color: Theme.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: importSourceField
                    Layout.fillWidth: true
                    placeholderText: qsTr("import_flow.source_dialog_placeholder")
                    color: Theme.text
                    placeholderTextColor: Theme.placeholder
                    background: Rectangle { color: Theme.base; border.color: Theme.border; radius: Theme.radius }
                }
                AppButton { text: qsTr("import_flow.browse_btn"); onClicked: iniFileDialog.open() }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AppButton {
                    text: qsTr("scx.ok")
                    role: "open"
                    enabled: importSourceField.text.trim() !== ""
                    onClicked: { importSource.close(); config.importIni(importSourceField.text) }
                }
                AppButton { text: qsTr("scx.cancel"); onClicked: importSource.close() }
            }
        }
    }

    // Import INI conflicts: one resolution per key.
    Popup {
        id: conflictDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(1000, page.width - 40)
        height: Math.min(640, page.height - 40)
        modal: true
        padding: 16
        closePolicy: Popup.NoAutoClose
        property var choices: []
        onAboutToShow: {
            const c = []
            for (let i = 0; i < config.importConflicts.length; ++i) c.push("keep")
            choices = c
        }
        background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: qsTr("import_dialog.title"); color: Theme.title; font.bold: true; font.pixelSize: 15 }
            Label {
                text: App.fmt(qsTr("scx.import_summary"), { added: config.importAdded, conflicts: config.importConflicts.length, excluded: config.importExcluded })
                color: Theme.text
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                AppButton {
                    text: qsTr("import_dialog.keep_all_btn")
                    onClicked: { const c = conflictDialog.choices.map(() => "keep"); conflictDialog.choices = c }
                }
                AppButton {
                    text: qsTr("import_dialog.import_all_btn")
                    onClicked: { const c = conflictDialog.choices.map(() => "use"); conflictDialog.choices = c }
                }
            }
            ListView {
                id: conflictList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: config.importConflicts
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    width: conflictList.width - 12
                    height: col.implicitHeight + 12
                    color: index % 2 ? Theme.alternateBase : Theme.base
                    radius: Theme.radius
                    ColumnLayout {
                        id: col
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: modelData.key; color: Theme.title; font.family: Theme.monoFont; Layout.fillWidth: true; elide: Text.ElideRight }
                            ComboBox {
                                implicitWidth: 200
                                model: [qsTr("import_dialog.resolution_keep"), qsTr("import_dialog.resolution_use"),
                                        qsTr("import_dialog.resolution_append"), qsTr("import_dialog.resolution_prepend")]
                                readonly property var keys: ["keep", "use", "append", "prepend"]
                                currentIndex: Math.max(0, keys.indexOf(conflictDialog.choices[index]))
                                onActivated: (i) => {
                                    const c = conflictDialog.choices.slice()
                                    c[index] = keys[i]
                                    conflictDialog.choices = c
                                }
                            }
                        }
                        Label { text: qsTr("import_dialog.col_current_value") + ": " + modelData.current; color: Theme.dim; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; maximumLineCount: 3; elide: Text.ElideRight }
                        Label { text: qsTr("import_dialog.col_imported_value") + ": " + modelData.imported; color: Theme.text; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; maximumLineCount: 3; elide: Text.ElideRight }
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AppButton {
                    text: qsTr("scx.import_btn")
                    role: "open"
                    onClicked: { conflictDialog.close(); config.finishImport(conflictDialog.choices) }
                }
                AppButton { text: qsTr("scx.cancel"); onClicked: { conflictDialog.close(); config.cancelImport() } }
            }
        }
    }

    // Map Language File: per-language base.ini URLs.
    Popup {
        id: languageSources
        anchors.centerIn: Overlay.overlay
        width: Math.min(820, page.width - 40)
        modal: true
        padding: 16
        property var rows: []
        onAboutToShow: rows = config.languageSources()
        background: Rectangle { color: Theme.panel; border.color: Theme.border; radius: 6 }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: qsTr("config.map_language_title"); color: Theme.title; font.bold: true; font.pixelSize: 15 }
            Label { text: qsTr("config.map_language_desc"); color: Theme.dim; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                Repeater {
                    model: languageSources.rows
                    delegate: RowLayout {
                        required property var modelData
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        Label { text: modelData.name; color: Theme.text; Layout.preferredWidth: 140 }
                        TextField {
                            Layout.fillWidth: true
                            text: modelData.url
                            placeholderText: modelData.bundled !== "" ? modelData.bundled : qsTr("config.language_source_placeholder")
                            color: Theme.text
                            placeholderTextColor: Theme.placeholder
                            background: Rectangle { color: Theme.base; border.color: Theme.border; radius: Theme.radius }
                            onEditingFinished: config.setLanguageSource(modelData.id, text)
                        }
                    }
                }
            }
            AppButton { Layout.alignment: Qt.AlignRight; text: qsTr("scx.close"); onClicked: languageSources.close() }
        }
    }
}
