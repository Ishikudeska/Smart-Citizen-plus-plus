import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ScApp

// The status bar: the last status message, counts, channel and version.
Rectangle {
    implicitHeight: 26
    color: Theme.panel
    border.color: Theme.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 16

        Label {
            text: App.statusText
            color: Theme.text
            elide: Text.ElideRight
            Layout.fillWidth: true
            font.pixelSize: Theme.smallFont
        }
        Label {
            visible: App.loaded
            text: App.fmt(qsTr("scx.footer_counts"), {
                total: App.strings.totalCount.toLocaleString(Qt.locale("en_US"), "f", 0),
                modified: App.strings.modifiedCount.toLocaleString(Qt.locale("en_US"), "f", 0),
                enhanced: App.strings.enhancedCount.toLocaleString(Qt.locale("en_US"), "f", 0)
            })
            color: Theme.placeholder
            font.pixelSize: Theme.smallFont
        }
        Label {
            text: App.fmt(qsTr("status_bar.channel_indicator"), { channel: App.channel })
            color: Theme.title
            font.pixelSize: Theme.smallFont
            font.bold: true
        }
        Label {
            text: "v" + App.version
            color: Theme.placeholder
            font.pixelSize: Theme.smallFont
        }
    }
}
