pragma Singleton
import QtQuick
import ScApp

// The Light and Dark palettes (from Smart Citizen's theme.py). Everything
// visual reads from here; App.theme picks the palette.
QtObject {
    id: theme

    readonly property string name: App.theme
    readonly property var palettes: ({
        "light": {
            window: "#c8c8c8", text: "#191919", base: "#d7d7d7", alternateBase: "#d0d0d0",
            panel: "#bebebe", button: "#c8c8c8", border: "#a0a0a0", highlight: "#1565c0",
            highlightedText: "#ffffff", link: "#0066cc", placeholder: "#5a5a5a", dim: "#2a2a2a",
            disabled: "#646464", title: "#1565c0", tagline: "#555555", groove: "#8a8a8a",
            chunk: "#1565c0", favoriteRow: "#fff4c4", tooltip: "#f0f0da",
            load: "#2196f3", restore: "#ff5722", apply: "#4caf50", clear: "#9e9e9e",
            open: "#2196f3", needsApply: "#f44336", buttonText: "#ffffff"
        },
        "dark": {
            window: "#1e1e1e", text: "#e8e8e8", base: "#1e1e1e", alternateBase: "#2d2d30",
            panel: "#252528", button: "#37373a", border: "#3c3c3f", highlight: "#3b82f6",
            highlightedText: "#ffffff", link: "#64aaff", placeholder: "#afafaf", dim: "#d5d5d5",
            disabled: "#969696", title: "#64b5f6", tagline: "#a0a0a0", groove: "#3c3c3f",
            chunk: "#3b82f6", favoriteRow: "#3a3000", tooltip: "#2d2d30",
            load: "#64b5f6", restore: "#ff8a65", apply: "#81c784", clear: "#bdbdbd",
            open: "#64b5f6", needsApply: "#e57373", buttonText: "#000000"
        }
    })
    readonly property var p: palettes[name] !== undefined ? palettes[name] : palettes["dark"]

    readonly property color window: p.window
    readonly property color text: p.text
    readonly property color base: p.base
    readonly property color alternateBase: p.alternateBase
    readonly property color panel: p.panel
    readonly property color button: p.button
    readonly property color border: p.border
    readonly property color highlight: p.highlight
    readonly property color highlightedText: p.highlightedText
    readonly property color link: p.link
    readonly property color placeholder: p.placeholder
    readonly property color dim: p.dim
    readonly property color disabled: p.disabled
    readonly property color title: p.title
    readonly property color tagline: p.tagline
    readonly property color groove: p.groove
    readonly property color chunk: p.chunk
    readonly property color favoriteRow: p.favoriteRow
    readonly property color tooltip: p.tooltip
    readonly property color buttonText: p.buttonText

    // Action colours (load / restore / apply / clear / open / needs_apply).
    function action(role) {
        switch (role) {
        case "load": return p.load
        case "restore": return p.restore
        case "apply": return p.apply
        case "clear": return p.clear
        case "open": return p.open
        case "needsApply": return p.needsApply
        }
        return p.button
    }

    readonly property var statusColors: ({
        "Modified": "#4caf50", "Enhanced": "#2196f3", "Unmodified": "#999999", "New": "#ff9800"
    })
    readonly property color gold: "#ffd700"
    readonly property color starOff: "#666666"
    readonly property color em4: "#4a9eff"

    readonly property int spacing: 8
    readonly property int radius: 4
    readonly property int fontSize: 13
    readonly property int smallFont: 11
    readonly property string monoFont: "Consolas"
}
