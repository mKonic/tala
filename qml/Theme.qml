// Colours by role, light and dark following the desktop; the accent is the
// desktop's too (Qt reads it from the portal).
import QtQuick

QtObject {
    // A platform theme that doesn't say (qtengine, under atrium) still gives
    // the palette: a dark window means dark.
    readonly property bool dark: Qt.styleHints.colorScheme === Qt.ColorScheme.Dark
        || (Qt.styleHints.colorScheme === Qt.ColorScheme.Unknown && system.window.hslLightness < 0.5)
    readonly property SystemPalette system: SystemPalette {}

    readonly property color window: dark ? "#1c1c1e" : "#f5f5f7"
    readonly property color sidebar: dark ? "#222225" : "#ebebee"
    readonly property color card: dark ? "#2a2a2d" : "#ffffff"
    readonly property color cardHover: dark ? "#323236" : "#f2f2f5"
    readonly property color field: dark ? "#1f1f22" : "#ffffff"
    readonly property color separator: dark ? "#3a3a3e" : "#dcdce0"
    readonly property color label: dark ? "#f2f2f7" : "#1c1c1e"
    readonly property color secondary: dark ? "#9a9aa1" : "#6e6e73"
    readonly property color tertiary: dark ? "#6c6c72" : "#a1a1a6"
    readonly property color accent: system.accent.valid ? system.accent : "#0a84ff"
    readonly property color labelOnAccent: "#ffffff"
    readonly property color red: dark ? "#ff453a" : "#ff3b30"
    readonly property color selection: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.22 : 0.14)

    readonly property int radius: 10
    readonly property int small: 12
    readonly property int body: 14
    readonly property int title: 22
}
