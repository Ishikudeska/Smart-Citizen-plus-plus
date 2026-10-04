import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ScApp

ApplicationWindow {
    id: window

    // Set by main(): run the startup checks (off for --smoke-test and
    // --screenshot), and the page to show first.
    property bool autoStart: true
    property string initialPage: ""
    property string explorerPath: ""
    // --tour [n]: start the guided tour (n steps in) instead of the startup checks.
    property int tourStep: -1
    readonly property bool simple: App.uiMode === "simple"
    property string page: initialPage !== "" ? initialPage : "strings"

    width: 1440
    height: 900
    visible: true
    title: App.fmt(qsTr("window.title"), { version: App.version })
    color: Theme.window

    palette.window: Theme.window
    palette.windowText: Theme.text
    palette.base: Theme.base
    palette.alternateBase: Theme.alternateBase
    palette.text: Theme.text
    palette.button: Theme.button
    palette.buttonText: Theme.text
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightedText
    palette.toolTipBase: Theme.tooltip
    palette.toolTipText: Theme.text
    palette.placeholderText: Theme.placeholder
    palette.link: Theme.link
    palette.mid: Theme.border
    palette.light: Theme.alternateBase
    palette.dark: Theme.border
    palette.midlight: Theme.button
    palette.shadow: "#000000"

    readonly property var pages: [
        { id: "strings", title: qsTr("tabs.string_editor") },
        { id: "config", title: qsTr("tabs.config") },
        { id: "enhancements", title: qsTr("tabs.enhancements") },
        { id: "blueprints", title: qsTr("tabs.blueprint_tracker") },
        { id: "explorer", title: qsTr("scx.tab_explorer") },
        { id: "log", title: qsTr("tabs.log") },
        { id: "about", title: qsTr("tabs.about") },
        { id: "faq", title: qsTr("tabs.faq") },
        { id: "legal", title: qsTr("tabs.legal") }
    ]
    function pageIndex(id) {
        for (let i = 0; i < pages.length; ++i)
            if (pages[i].id === id)
                return i
        return 0
    }

    Connections {
        target: App
        function onNavigateTo(id) { window.page = id }
    }

    onClosing: (close) => {
        close.accepted = false
        saveGeometry()
        App.requestClose()
    }

    // Window size: the saved geometry when there is one, else by mode
    // (Advanced maximized, Simple compact). Not for --screenshot runs.
    function saveGeometry() {
        if (!autoStart)
            return
        const maximized = visibility === Window.Maximized
        App.windowLayout.saveWindowGeometry({ x: x, y: y, width: width, height: height, maximized: maximized })
    }
    function sizeForMode() {
        if (simple) {
            showNormal()
            width = 560
            height = 420
        } else {
            showMaximized()
        }
    }
    function restoreGeometry() {
        const g = App.windowLayout.windowGeometry()
        if (g.width === undefined) {
            sizeForMode()
            return
        }
        x = g.x; y = g.y; width = g.width; height = g.height
        if (g.maximized)
            showMaximized()
    }
    onSimpleChanged: if (autoStart) sizeForMode()
    minimumWidth: simple ? 420 : 900
    minimumHeight: simple ? 320 : 600

    Connections {
        target: App.windowLayout
        function onWindowProportionsReset() { window.sizeForMode() }
    }

    // First launch of a version: the tour runs before the startup checks,
    // so its coach marks and the startup prompts never overlap.
    Component.onCompleted: {
        if (tourStep >= 0) {
            Qt.callLater(() => { tutorial.start(); for (let i = 0; i < tourStep; ++i) tutorial.next() })
            return
        }
        if (autoStart) {
            restoreGeometry()
            if (tutorial.shouldAutoStart())
                Qt.callLater(tutorial.start)
            else
                Qt.callLater(App.startup)
        }
    }

    TutorialController {
        id: tutorial
        onFinished: if (window.autoStart) App.startup()
    }

    header: ColumnLayout {
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 54
            color: Theme.window
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 14
                ColumnLayout {
                    spacing: 0
                    Label {
                        text: App.appName.toUpperCase()
                        color: Theme.title
                        font.pixelSize: 22
                        font.bold: true
                        font.letterSpacing: 3
                    }
                    Label {
                        text: qsTr("scx.tagline")
                        color: Theme.tagline
                        font.pixelSize: 10
                        font.letterSpacing: 2
                    }
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    visible: !window.simple
                    text: qsTr("toolbar.tutorial_btn")
                    tip: qsTr("toolbar.tutorial_tooltip")
                    enabled: !App.busy && !tutorial.running
                    onClicked: { helpDrawer.close(); tutorial.start() }
                }
                AppButton {
                    objectName: "helpButton"
                    visible: !window.simple
                    text: qsTr("toolbar.help_btn")
                    tip: qsTr("toolbar.help_tooltip")
                    checkable: true
                    checked: helpDrawer.opened
                    onClicked: helpDrawer.opened ? helpDrawer.close() : helpDrawer.open()
                }
                Label { text: qsTr("scx.channel_label"); color: Theme.dim }
                ComboBox {
                    id: channelCombo
                    objectName: "channelCombo"
                    model: App.channels
                    currentIndex: App.channels.indexOf(App.channel)
                    enabled: !App.busy
                    onActivated: (i) => App.channel = App.channels[i]
                    implicitWidth: 150
                    delegate: ItemDelegate {
                        required property int index
                        required property string modelData
                        width: channelCombo.width
                        text: modelData + (App.installedChannels.indexOf(modelData) >= 0 ? "" : "  —  " + qsTr("scx.not_installed"))
                        highlighted: channelCombo.highlightedIndex === index
                    }
                }
            }
        }
        NavBar {
            Layout.fillWidth: true
            visible: !window.simple
            pages: window.pages
            current: window.page
            onSelected: (id) => window.page = id
        }
    }

    footer: StatusFooter {}

    SimplePage {
        anchors.fill: parent
        visible: window.simple
    }

    StackLayout {
        anchors.fill: parent
        visible: !window.simple
        currentIndex: window.pageIndex(window.page)

        StringsPage {}
        ConfigPage {}
        EnhancementsPage {}
        BlueprintsPage {}
        ExplorerPage { active: window.page === "explorer"; revealPath: window.explorerPath }
        LogPage {}
        DocsPage { document: "ABOUT" }
        DocsPage { document: "FAQ" }
        DocsPage { document: "LEGAL" }
    }

    // The Help side panel (HELP.md).
    Drawer {
        id: helpDrawer
        edge: Qt.RightEdge
        width: Math.min(560, window.width * 0.45)
        height: window.height
        modal: false
        interactive: opened
        background: Rectangle { color: Theme.window; border.color: Theme.border }
        DocsPage { anchors.fill: parent; document: "HELP" }
    }

    ProgressOverlay {}
    PromptHost {}
    CoachMarkOverlay {
        parent: Overlay.overlay
        anchors.fill: parent
        tutorial: tutorial
        searchRoot: window.contentItem.parent
        onShowPage: (id) => window.page = id
    }
}
