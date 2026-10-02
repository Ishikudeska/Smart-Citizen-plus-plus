import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ScApp

// A titled card grouping related settings or actions.
Rectangle {
    id: section

    property string title: ""
    property string description: ""
    default property alias content: body.data

    implicitHeight: column.implicitHeight + 28
    color: Theme.panel
    border.color: Theme.border
    radius: 6

    ColumnLayout {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        spacing: 8

        Label {
            text: section.title
            color: Theme.title
            font.pixelSize: 15
            font.bold: true
            visible: text !== ""
        }
        Label {
            text: section.description
            color: Theme.dim
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            visible: text !== ""
            font.pixelSize: Theme.smallFont + 1
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: 8
        }
    }
}
