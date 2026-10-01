import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window

    width: 1280
    height: 800
    visible: true
    title: Qt.application.name + " " + Qt.application.version

    Label {
        anchors.centerIn: parent
        text: window.title
        font.pixelSize: 24
    }
}
