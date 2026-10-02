import QtQuick
import QtQuick.Controls.Basic
import ScApp

// A check box drawn in the theme's colours, with an optional tooltip.
CheckBox {
    id: control

    property string tip: ""

    hoverEnabled: true
    ToolTip.visible: tip !== "" && hovered
    ToolTip.text: tip
    ToolTip.delay: 600

    indicator: Rectangle {
        implicitWidth: 18
        implicitHeight: 18
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 3
        color: control.checked ? Theme.highlight : Theme.base
        border.color: control.checked ? Theme.highlight : (control.hovered ? Theme.title : Theme.placeholder)
        border.width: 1
        Text {
            anchors.centerIn: parent
            text: "✓"
            visible: control.checked
            color: Theme.highlightedText
            font.pixelSize: 13
            font.bold: true
        }
    }
    contentItem: Text {
        leftPadding: control.indicator.width + 6
        text: control.text
        color: control.enabled ? Theme.text : Theme.disabled
        font.pixelSize: Theme.fontSize
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
    }
}
