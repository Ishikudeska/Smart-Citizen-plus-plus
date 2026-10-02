import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ScApp

// The guided tour's coach marks: dims the window, cuts a spotlight around
// the step's target (found by objectName among the visible items) and
// floats a callout with Back / Next / Skip beside it.
Item {
    id: overlay

    required property TutorialController tutorial
    required property Item searchRoot   // where targets are looked up
    signal showPage(string page)

    readonly property int pad: 6
    property rect spot: Qt.rect(0, 0, 0, 0)
    property bool hasSpot: false

    visible: tutorial.running
    z: 1000
    focus: visible

    function findItem(item, name) {
        if (!item || !item.visible)
            return null
        if (item.objectName === name && item.width > 0 && item.height > 0)
            return item
        const kids = item.children
        for (let i = 0; i < kids.length; ++i) {
            const found = findItem(kids[i], name)
            if (found)
                return found
        }
        return null
    }
    // Scrolls the nearest Flickable so `item` is in view.
    function bringIntoView(item) {
        for (let p = item.parent; p; p = p.parent) {
            if (p instanceof Flickable && p.contentHeight > p.height) {
                const pos = item.mapToItem(p.contentItem, 0, 0)
                const wanted = Math.min(pos.y - 40, p.contentHeight - p.height)
                if (pos.y < p.contentY || pos.y + Math.min(item.height, p.height) > p.contentY + p.height)
                    p.contentY = Math.max(0, wanted)
                return
            }
        }
    }
    function locate() {
        const step = tutorial.step
        const target = step.target ? findItem(searchRoot, step.target) : null
        if (!target) {
            hasSpot = false
            return
        }
        bringIntoView(target)
        const r = target.mapToItem(overlay, 0, 0, target.width, target.height)
        const x0 = Math.max(0, r.x - pad), y0 = Math.max(0, r.y - pad)
        const x1 = Math.min(width, r.x + r.width + pad), y1 = Math.min(height, r.y + r.height + pad)
        spot = Qt.rect(x0, y0, Math.max(0, x1 - x0), Math.max(0, y1 - y0))
        hasSpot = spot.width > 0 && spot.height > 0
    }

    Connections {
        target: overlay.tutorial
        function onChanged() {
            if (!overlay.tutorial.running)
                return
            const page = overlay.tutorial.step.page
            if (page)
                overlay.showPage(page)
            overlay.hasSpot = false
            settle.restart()
            overlay.forceActiveFocus()
        }
    }
    // Let the page switch and layouts settle before measuring.
    Timer { id: settle; interval: 150; onTriggered: overlay.locate() }
    onWidthChanged: if (visible) settle.restart()
    onHeightChanged: if (visible) settle.restart()

    // Swallow input outside the callout.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: (wheel) => wheel.accepted = true }

    // ── dim layer with a hole ──
    readonly property color dim: Qt.rgba(0, 0, 0, 170 / 255)
    Rectangle { color: overlay.dim; x: 0; y: 0; width: overlay.width; height: overlay.hasSpot ? overlay.spot.y : overlay.height }
    Rectangle {
        visible: overlay.hasSpot
        color: overlay.dim
        x: 0; y: overlay.spot.y + overlay.spot.height
        width: overlay.width; height: overlay.height - y
    }
    Rectangle {
        visible: overlay.hasSpot
        color: overlay.dim
        x: 0; y: overlay.spot.y
        width: overlay.spot.x; height: overlay.spot.height
    }
    Rectangle {
        visible: overlay.hasSpot
        color: overlay.dim
        x: overlay.spot.x + overlay.spot.width; y: overlay.spot.y
        width: overlay.width - x; height: overlay.spot.height
    }
    Rectangle {
        visible: overlay.hasSpot
        x: overlay.spot.x; y: overlay.spot.y
        width: overlay.spot.width; height: overlay.spot.height
        color: "transparent"
        radius: 6
        border.color: "#00d4ff"
        border.width: 3
    }

    // ── callout ──
    Rectangle {
        id: callout
        readonly property int gap: 16
        readonly property int edge: 16
        width: Math.min(460, overlay.width - 2 * edge)
        height: content.implicitHeight + 28
        color: Theme.panel
        border.color: "#00d4ff"
        border.width: 2
        radius: 8

        function clampX(v) { return Math.max(edge, Math.min(v, overlay.width - width - edge)) }
        function clampY(v) { return Math.max(edge, Math.min(v, overlay.height - height - edge)) }
        readonly property string side: {
            if (!overlay.hasSpot)
                return "center"
            const s = overlay.tutorial.step.side || "auto"
            const room = {
                below: overlay.height - (overlay.spot.y + overlay.spot.height) - gap,
                above: overlay.spot.y - gap,
                right: overlay.width - (overlay.spot.x + overlay.spot.width) - gap,
                left: overlay.spot.x - gap
            }
            const fits = (k) => (k === "below" || k === "above") ? room[k] >= height + edge : room[k] >= width + edge
            if (s !== "auto" && fits(s))
                return s
            for (const k of ["below", "above", "right", "left"])
                if (fits(k))
                    return k
            return "inside"
        }
        x: {
            const sp = overlay.spot
            switch (side) {
            case "right": return clampX(sp.x + sp.width + gap)
            case "left": return clampX(sp.x - width - gap)
            case "below":
            case "above": return clampX(sp.x)
            case "inside": return clampX(sp.x + sp.width - width - gap)
            default: return (overlay.width - width) / 2
            }
        }
        y: {
            const sp = overlay.spot
            switch (side) {
            case "below": return clampY(sp.y + sp.height + gap)
            case "above": return clampY(sp.y - height - gap)
            case "right":
            case "left": return clampY(sp.y)
            case "inside": return clampY(sp.y + sp.height - height - gap)
            default: return (overlay.height - height) / 2
            }
        }

        MouseArea { anchors.fill: parent } // keep clicks off the dim layer

        ColumnLayout {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 8

            Label {
                text: App.fmt(qsTr("coach.step_counter"), { current: overlay.tutorial.index + 1, total: overlay.tutorial.count })
                color: Theme.placeholder
                font.pixelSize: Theme.smallFont
            }
            Label {
                Layout.fillWidth: true
                text: overlay.tutorial.step.title || ""
                color: Theme.title
                font.pixelSize: 16
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: overlay.tutorial.step.description || ""
                color: Theme.text
                wrapMode: Text.WordWrap
                lineHeight: 1.15
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                AppButton { text: qsTr("coach.skip_btn"); onClicked: overlay.tutorial.skip() }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("coach.back_btn")
                    enabled: overlay.tutorial.index > 0
                    onClicked: overlay.tutorial.back()
                }
                AppButton {
                    role: "open"
                    text: overlay.tutorial.index + 1 >= overlay.tutorial.count ? qsTr("coach.finish_btn") : qsTr("coach.next_btn")
                    onClicked: overlay.tutorial.next()
                }
            }
        }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) overlay.tutorial.skip()
        else if (event.key === Qt.Key_Right || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) overlay.tutorial.next()
        else if (event.key === Qt.Key_Left) overlay.tutorial.back()
        else return
        event.accepted = true
    }
}
