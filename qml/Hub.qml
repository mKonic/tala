// tala's window (Flow calls it the Hub): what was dictated, the words and
// snippets it knows, how text should read, and the settings.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ApplicationWindow {
    id: hub
    width: 980
    height: 660
    minimumWidth: 760
    minimumHeight: 480
    visible: false
    title: "tala"
    color: theme.window

    Theme { id: theme }
    // The stock controls (combo boxes, dialogs, check boxes) in tala's colours.
    palette.window: theme.window
    palette.windowText: theme.label
    palette.base: theme.field
    palette.alternateBase: theme.card
    palette.text: theme.label
    palette.button: theme.card
    palette.buttonText: theme.label
    palette.highlight: theme.accent
    palette.highlightedText: theme.labelOnAccent
    palette.light: theme.cardHover
    palette.mid: theme.separator
    palette.dark: theme.separator
    palette.placeholderText: theme.tertiary
    property string page: "home"

    // Material Symbols when the desktop has it (atrium does), else plain glyphs.
    readonly property bool symbols: Qt.fontFamilies().includes("Material Symbols Rounded")
    readonly property var pages: [
        { id: "home", title: qsTr("Home"), icon: "home", glyph: "\u2302" },
        { id: "dictionary", title: qsTr("Dictionary"), icon: "dictionary", glyph: "\u2261" },
        { id: "snippets", title: qsTr("Snippets"), icon: "text_snippet", glyph: "\u2702" },
        { id: "style", title: qsTr("Style"), icon: "edit_note", glyph: "\u270e" },
        { id: "settings", title: qsTr("Settings"), icon: "settings", glyph: "\u2699" },
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar.
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 200
            color: theme.sidebar
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                anchors.topMargin: 18
                spacing: 2
                Text {
                    text: "tala"
                    color: theme.label
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    Layout.leftMargin: 10
                    Layout.bottomMargin: 14
                }
                Repeater {
                    model: hub.pages
                    Rectangle {
                        required property var modelData
                        readonly property bool current: hub.page === modelData.id
                        Layout.fillWidth: true
                        implicitHeight: 34
                        radius: 8
                        color: current ? theme.selection : nav.hovered ? theme.cardHover : "transparent"
                        HoverHandler { id: nav }
                        TapHandler { onTapped: hub.page = modelData.id }
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: 10
                            spacing: 10
                            Text {
                                width: 20
                                anchors.verticalCenter: parent.verticalCenter
                                text: hub.symbols ? modelData.icon : modelData.glyph
                                color: current ? theme.accent : theme.secondary
                                font.family: hub.symbols ? "Material Symbols Rounded" : ""
                                font.pixelSize: hub.symbols ? 20 : 15
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Text {
                                text: modelData.title
                                color: current ? theme.accent : theme.label
                                font.pixelSize: theme.body
                                font.weight: current ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                }
                Item { Layout.fillHeight: true }
                // The engine, while it isn't ready, and anything blocking dictation.
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    visible: !tala.engineReady || tala.problem !== ""
                    text: tala.problem !== "" ? tala.problem : tala.engineStatus
                    color: tala.problem !== "" ? theme.red : theme.secondary
                    font.pixelSize: theme.small
                    wrapMode: Text.Wrap
                }
            }
        }

        Rectangle { Layout.fillHeight: true; width: 1; color: theme.separator }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            source: hub.page === "home" ? "HomePage.qml"
                  : hub.page === "dictionary" ? "DictionaryPage.qml"
                  : hub.page === "snippets" ? "SnippetsPage.qml"
                  : hub.page === "style" ? "StylePage.qml"
                  : "SettingsPage.qml"
            onLoaded: item.theme = theme
        }
    }
}
