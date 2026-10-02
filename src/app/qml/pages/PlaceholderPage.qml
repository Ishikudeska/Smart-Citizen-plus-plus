import QtQuick
import QtQuick.Controls.Basic
import ScApp

// Stands in for a page that isn't built yet.
Item {
    property string title: ""
    Label {
        anchors.centerIn: parent
        text: parent.title
        color: Theme.placeholder
        font.pixelSize: 18
    }
}
