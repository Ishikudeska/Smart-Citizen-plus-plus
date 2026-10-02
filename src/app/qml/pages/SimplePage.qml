import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ScApp

// Simple mode: one button that generates enhancements with the current
// settings and applies them, and a way back to the full interface.
Item {
    id: page

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 80, 420)
        spacing: 12

        AppButton {
            objectName: "simpleApplyButton"
            Layout.fillWidth: true
            Layout.preferredWidth: 320
            Layout.maximumWidth: 320
            Layout.alignment: Qt.AlignHCenter
            implicitHeight: 48
            role: "apply"
            font.pixelSize: 15
            text: qsTr("simple_mode.generate_apply_btn")
            tip: qsTr("simple_mode.generate_apply_tip")
            enabled: !App.busy
            onClicked: App.simpleApply()
        }
        AppButton {
            Layout.fillWidth: true
            Layout.preferredWidth: 320
            Layout.maximumWidth: 320
            Layout.alignment: Qt.AlignHCenter
            implicitHeight: 48
            role: "open"
            font.pixelSize: 15
            text: qsTr("simple_mode.switch_to_advanced")
            tip: qsTr("simple_mode.switch_to_advanced_tip")
            onClicked: App.uiMode = "advanced"
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 6
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("simple_mode.defaults_hint")
            color: Theme.placeholder
        }
    }
}
