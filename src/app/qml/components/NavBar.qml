import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ScApp

// The page tabs across the top, under the title.
Rectangle {
    id: nav

    property var pages: []      // [{id, title}]
    property string current: ""
    signal selected(string id)

    implicitHeight: 38
    color: Theme.panel

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.bottom: parent.bottom
        spacing: 2

        Repeater {
            model: nav.pages
            delegate: AbstractButton {
                id: tab
                required property var modelData
                readonly property bool active: nav.current === modelData.id
                objectName: "nav_" + modelData.id
                height: 34
                width: label.implicitWidth + 28
                hoverEnabled: true
                onClicked: nav.selected(modelData.id)

                contentItem: Label {
                    id: label
                    text: tab.modelData.title
                    color: tab.active ? Theme.title : Theme.text
                    font.bold: tab.active
                    font.pixelSize: Theme.fontSize
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: tab.active ? Theme.window : tab.hovered ? Theme.alternateBase : "transparent"
                    radius: Theme.radius
                    Rectangle {
                        visible: tab.active
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 3
                        color: Theme.title
                    }
                }
            }
        }
    }
}
