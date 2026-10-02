import QtQuick
import QtQuick.Controls.Basic
import ScApp

// A push button in one of the theme's action colours ("apply", "open",
// "clear", ...), or a plain panel button when `role` is empty.
Button {
    id: control

    property string role: ""
    property string tip: ""
    readonly property color fill: role === "" ? Theme.button : Theme.action(role)

    font.pixelSize: Theme.fontSize
    font.bold: role !== ""
    padding: 6
    leftPadding: 12
    rightPadding: 12
    hoverEnabled: true

    ToolTip.visible: tip !== "" && hovered
    ToolTip.text: tip
    ToolTip.delay: 600

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.role === "" ? (control.enabled ? Theme.text : Theme.disabled) : Theme.buttonText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        implicitHeight: 30
        implicitWidth: 80
        radius: Theme.radius
        color: control.down ? Qt.darker(control.fill, 1.25)
                            : control.hovered ? Qt.lighter(control.fill, 1.12) : control.fill
        border.color: control.checked ? Theme.highlight : (control.role === "" ? Theme.border : Qt.darker(control.fill, 1.3))
        border.width: control.checked ? 2 : 1
        opacity: control.enabled || control.role !== "" ? 1.0 : 0.6
    }
}
