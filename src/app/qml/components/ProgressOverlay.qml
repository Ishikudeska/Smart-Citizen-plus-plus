import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ScApp

// The single progress dialog every background job reports into
// (App.tasks). Modal while a job runs, with Cancel when the job allows it.
Popup {
    id: overlay

    readonly property var task: App.tasks
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, parent ? parent.width - 40 : 520)
    modal: true
    closePolicy: Popup.NoAutoClose
    visible: task.running
    padding: 20

    Overlay.modal: Rectangle { color: "#80000000" }

    background: Rectangle {
        color: Theme.panel
        border.color: Theme.border
        radius: 6
    }

    contentItem: ColumnLayout {
        spacing: 12

        Label {
            text: overlay.task.title
            color: Theme.title
            font.pixelSize: 16
            font.bold: true
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Label {
            text: overlay.task.message
            color: Theme.text
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            visible: text !== ""
        }
        ProgressBar {
            id: bar
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, overlay.task.total)
            value: overlay.task.completed
            indeterminate: overlay.task.total <= 0
            background: Rectangle {
                implicitHeight: 8
                radius: 4
                color: Theme.groove
            }
            contentItem: Item {
                implicitHeight: 8
                Rectangle {
                    visible: !bar.indeterminate
                    width: bar.visualPosition * parent.width
                    height: parent.height
                    radius: 4
                    color: Theme.chunk
                }
                Rectangle {
                    id: runner
                    visible: bar.indeterminate
                    width: parent.width * 0.25
                    height: parent.height
                    radius: 4
                    color: Theme.chunk
                    NumberAnimation on x {
                        running: bar.indeterminate && overlay.visible
                        from: -runner.width
                        to: bar.width
                        duration: 1400
                        loops: Animation.Infinite
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: overlay.task.total > 0 ? overlay.task.completed + " / " + overlay.task.total : ""
                color: Theme.placeholder
                font.pixelSize: Theme.smallFont
                Layout.fillWidth: true
            }
            AppButton {
                text: overlay.task.cancelRequested ? qsTr("scx.cancelling") : qsTr("scx.cancel")
                visible: overlay.task.cancellable
                enabled: !overlay.task.cancelRequested
                onClicked: overlay.task.cancel()
            }
        }
    }
}
