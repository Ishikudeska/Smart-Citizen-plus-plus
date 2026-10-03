import QtQuick
import QtQuick.Controls.Basic
import ScApp

// One bundled Markdown document (About, FAQ, Legal, Help).
Item {
    id: page

    property string document: "ABOUT"

    ScrollView {
        id: scroller
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        Text {
            x: 24
            width: Math.min(scroller.availableWidth - 48, 980)
            topPadding: 16
            bottomPadding: 24
            textFormat: Text.MarkdownText
            wrapMode: Text.WordWrap
            color: Theme.text
            linkColor: Theme.link
            font.pixelSize: Theme.fontSize + 1
            text: DocsController.markdown(page.document, App.language)
            onLinkActivated: (link) => Qt.openUrlExternally(link)
            HoverHandler { cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
        }
    }
}
