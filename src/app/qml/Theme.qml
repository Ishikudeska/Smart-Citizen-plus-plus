pragma Singleton
import QtQuick
import ScApp

// The four palettes Smart Citizen shipped (theme.py): the navy "scle"
// default, Light, Dark and the gold "odw". Everything visual reads from
// here; App.theme picks the palette.
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
        },
        "scle": {
            window: "#0d1826", text: "#d8e8f0", base: "#0d1826", alternateBase: "#152538",
            panel: "#11202f", button: "#1a2d44", border: "#22405e", highlight: "#0099cc",
            highlightedText: "#0a1220", link: "#4fd7e8", placeholder: "#6fb5d0", dim: "#d5d5d5",
            disabled: "#587890", title: "#4fd7e8", tagline: "#6fb5d0", groove: "#152538",
            chunk: "#4fd7e8", favoriteRow: "#3a3000", tooltip: "#152538",
            load: "#4fd7e8", restore: "#ff8a42", apply: "#4ade80", clear: "#5f7a95",
            open: "#4fd7e8", needsApply: "#ff5c5c", buttonText: "#000000"
        },
        "odw": {
            window: "#1a1f2e", text: "#f0e6cf", base: "#1a1f2e", alternateBase: "#242938",
            panel: "#1f2433", button: "#242938", border: "#3a3f50", highlight: "#d4a017",
            highlightedText: "#1a1f2e", link: "#d4b876", placeholder: "#a08c5a", dim: "#d4b876",
            disabled: "#645a46", title: "#c9a961", tagline: "#a08c5a", groove: "#242938",
            chunk: "#d4a017", favoriteRow: "#3a3000", tooltip: "#242938",
            load: "#d4b876", restore: "#c77a4d", apply: "#a5b989", clear: "#7a7d87",
            open: "#d4b876", needsApply: "#c0392b", buttonText: "#000000"
        }
    })
    readonly property var p: palettes[name] !== undefined ? palettes[name] : palettes["scle"]

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
