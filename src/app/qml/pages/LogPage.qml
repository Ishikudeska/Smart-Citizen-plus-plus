pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import ScApp

// The session log, filtered by level, following new lines.
Item {
    id: page

    LogModel { id: log }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        RowLayout {
            spacing: 10
            Label { text: qsTr("log.min_level_label"); color: Theme.text }
            ComboBox {
                implicitWidth: 140
                textRole: "label"
                valueRole: "level"
                model: [
                    { level: 10, label: "DEBUG" }, { level: 20, label: "INFO" }, { level: 30, label: "WARNING" },
                    { level: 40, label: "ERROR" }, { level: 50, label: "CRITICAL" }
                ]
                currentIndex: [10, 20, 30, 40, 50].indexOf(log.minLevel)
                onActivated: log.minLevel = currentValue
                ToolTip.visible: hovered; ToolTip.text: qsTr("log.min_level_tooltip"); ToolTip.delay: 800
            }
            AppCheckBox { id: follow; text: qsTr("log.auto_scroll_check"); tip: qsTr("log.auto_scroll_tooltip"); checked: true }
            AppButton { text: qsTr("log.clear_btn"); tip: qsTr("log.clear_tooltip"); onClicked: log.clear() }
            AppButton {
                text: qsTr("log.export_btn")
                tip: qsTr("log.export_tooltip")
                onClicked: { exportDialog.selectedFile = App.pathToUrl(log.defaultExportPath()); exportDialog.open() }
            }
            AppButton { text: qsTr("scx.copy_all"); onClicked: App.copyText(log.allText()) }
            Item { Layout.fillWidth: true }
            Label { text: App.fmt(qsTr("log.lines_label"), { count: log.count }); color: Theme.placeholder }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.base
            border.color: Theme.border
            radius: Theme.radius

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                model: log
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                contentWidth: Math.max(width, 2400)
                flickableDirection: Flickable.AutoFlickDirection
                onCountChanged: if (follow.checked) Qt.callLater(positionViewAtEnd)
                delegate: TextEdit {
                    required property var model
                    width: list.contentWidth
                    text: model.text
                    readOnly: true
                    selectByMouse: true
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.smallFont + 1
                    color: model.lineColor ? model.lineColor : Theme.text
                }
            }
        }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("log.export_title")
        fileMode: FileDialog.SaveFile
        nameFilters: ["Log files (*.log)", "All files (*)"]
        onAccepted: log.exportTo(selectedFile)
    }
}
