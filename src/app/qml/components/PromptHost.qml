pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ScApp

// Shows App.prompts.current, the message box a C++ flow is waiting on, and
// sends the answer back.
Popup {
    id: host

    readonly property var prompt: App.prompts.current
    readonly property bool hasPrompt: App.prompts.active
    property int choice: -1

    anchors.centerIn: Overlay.overlay
    width: Math.min(620, parent ? parent.width - 40 : 620)
    modal: true
    closePolicy: Popup.CloseOnEscape
    visible: hasPrompt
    padding: 20
    onHasPromptChanged: {
        choice = -1
        check.checked = false
    }
    onClosed: if (hasPrompt) App.prompts.answer(-1, check.checked, choice)

    Overlay.modal: Rectangle { color: "#80000000" }

    background: Rectangle {
        color: Theme.panel
        border.color: host.prompt.kind === "error" ? Theme.action("needsApply")
                    : host.prompt.kind === "warning" ? Theme.action("restore") : Theme.border
        border.width: 1
        radius: 6
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            spacing: 10
            Label {
                text: host.prompt.kind === "error" ? "✖" : host.prompt.kind === "warning" ? "⚠"
                    : host.prompt.kind === "question" ? "?" : "ℹ"
                color: host.prompt.kind === "error" ? Theme.action("needsApply")
                     : host.prompt.kind === "warning" ? Theme.action("restore") : Theme.title
                font.pixelSize: 22
                font.bold: true
            }
            Label {
                text: host.prompt.title || ""
                color: Theme.title
                font.pixelSize: 16
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(body.implicitHeight, 380)
            clip: true
            Label {
                id: body
                width: host.availableWidth - 4
                text: host.prompt.text || ""
                color: Theme.text
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
            }
        }
        Rectangle {
            visible: (host.prompt.detail || "") !== ""
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(detail.implicitHeight + 12, 160)
            color: Theme.base
            border.color: Theme.border
            radius: Theme.radius
            ScrollView {
                anchors.fill: parent
                anchors.margins: 6
                clip: true
                TextEdit {
                    id: detail
                    text: host.prompt.detail || ""
                    readOnly: true
                    selectByMouse: true
                    color: Theme.text
                    font.family: Theme.monoFont
                    font.pixelSize: Theme.smallFont
                }
            }
        }
        ListView {
            id: choiceList
            visible: (host.prompt.choices || []).length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 220)
            clip: true
            model: host.prompt.choices || []
            delegate: ItemDelegate {
                required property int index
                required property string modelData
                width: choiceList.width
                text: modelData
                highlighted: host.choice === index
                onClicked: host.choice = index
                onDoubleClicked: App.prompts.answer(0, check.checked, index)
            }
        }
        CheckBox {
            id: check
            visible: (host.prompt.checkbox || "") !== ""
            text: host.prompt.checkbox || ""
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            Repeater {
                model: host.prompt.buttons || []
                delegate: AppButton {
                    required property int index
                    required property string modelData
                    text: modelData
                    role: index === (host.prompt.defaultButton || 0) ? "open" : ""
                    enabled: !choiceList.visible || index !== 0 || host.choice >= 0
                    onClicked: App.prompts.answer(index, check.checked, host.choice)
                }
            }
        }
    }
}
