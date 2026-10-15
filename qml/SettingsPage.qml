// Settings: the microphone, the keys, sounds, starting at login, how long
// recordings are kept, and the speech engine.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Flickable {
    id: page
    property Theme theme: Theme {}
    contentHeight: column.implicitHeight + 56
    clip: true
    ScrollBar.vertical: ScrollBar {}

    readonly property var shortcutNames: ({
        "dictate": qsTr("Dictate (hold)"),
        "hands-free": qsTr("Hands-free"),
        "cancel": qsTr("Cancel"),
        "paste-last": qsTr("Paste last transcript"),
        "copy-last": qsTr("Copy last transcript"),
    })

    component Row2: RowLayout {
        property string title
        property string detail
        default property alias control: holder.data
        Layout.fillWidth: true
        spacing: 16
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text { text: title; color: theme.label; font.pixelSize: theme.body }
            Text { visible: detail !== ""; text: detail; color: theme.secondary; font.pixelSize: theme.small; Layout.fillWidth: true; wrapMode: Text.Wrap }
        }
        Item { id: holder; implicitWidth: childrenRect.width; implicitHeight: childrenRect.height }
    }
    component Line: Rectangle { Layout.fillWidth: true; height: 1; color: theme.separator }

    ColumnLayout {
        id: column
        x: 28
        y: 28
        width: page.width - 56
        spacing: 12

        Text { text: qsTr("Settings"); color: theme.label; font.pixelSize: theme.title; font.weight: Font.DemiBold; Layout.bottomMargin: 6 }

        SectionTitle { theme: page.theme; text: qsTr("Microphone") }
        Card {
            theme: page.theme
            Layout.fillWidth: true
            implicitHeight: mic.implicitHeight + 28
            ColumnLayout {
                id: mic
                anchors.fill: parent
                anchors.margins: 14
                Row2 {
                    title: qsTr("Input")
                    detail: qsTr("A wired or built-in microphone close to your mouth works best; Bluetooth headsets add delay.")
                    TCombo {
                        id: micBox
                        theme: page.theme
                        width: 280
                        readonly property var options: [{ name: "", description: qsTr("Auto (the system's default)") }].concat(tala.devices)
                        model: options.map(o => o.description)
                        currentIndex: { tala.revision; const n = tala.setting("mic", ""); return Math.max(0, options.findIndex(o => o.name === n)) }
                        onActivated: i => tala.setSetting("mic", options[i].name)
                    }
                }
            }
        }

        SectionTitle { theme: page.theme; text: qsTr("Keyboard shortcuts"); Layout.topMargin: 8 }
        Card {
            theme: page.theme
            Layout.fillWidth: true
            implicitHeight: keys.implicitHeight + 28
            ColumnLayout {
                id: keys
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                Repeater {
                    model: ["dictate", "hands-free", "cancel", "paste-last", "copy-last"]
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        Text { text: page.shortcutNames[modelData]; color: theme.label; font.pixelSize: theme.body; Layout.fillWidth: true }
                        Rectangle {
                            implicitWidth: keyText.implicitWidth + 16
                            implicitHeight: 24
                            radius: 6
                            color: theme.window
                            border.width: 1
                            border.color: theme.separator
                            Text {
                                id: keyText
                                anchors.centerIn: parent
                                text: tala.triggers[modelData] || qsTr("not set")
                                color: tala.triggers[modelData] ? theme.label : theme.tertiary
                                font.pixelSize: theme.small
                            }
                        }
                    }
                }
                Line {}
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Your desktop keeps these keys; change them in its shortcut settings.")
                        color: theme.secondary
                        font.pixelSize: theme.small
                        wrapMode: Text.Wrap
                    }
                    TButton { theme: page.theme; text: qsTr("Change…"); onClicked: tala.configureShortcuts() }
                }
            }
        }

        SectionTitle { theme: page.theme; text: qsTr("General"); Layout.topMargin: 8 }
        Card {
            theme: page.theme
            Layout.fillWidth: true
            implicitHeight: general.implicitHeight + 28
            ColumnLayout {
                id: general
                anchors.fill: parent
                anchors.margins: 14
                spacing: 14
                TSwitch {
                    theme: page.theme
                    Layout.fillWidth: true
                    title: qsTr("Sounds")
                    detail: qsTr("A soft chime when dictation starts and stops.")
                    checked: { tala.revision; return tala.setting("sounds", "1") === "1" }
                    onToggled: on => tala.setSetting("sounds", on ? "1" : "0")
                }
                Line {}
                TSwitch {
                    theme: page.theme
                    Layout.fillWidth: true
                    title: qsTr("Context awareness")
                    detail: qsTr("Reads the few words before the cursor, so dictating into the middle of a sentence gets its spacing and capitals right. It turns on the desktop's accessibility, which apps use to show their text to screen readers; nothing leaves this computer.")
                    checked: { tala.revision; return tala.setting("context", "0") === "1" }
                    onToggled: on => tala.setSetting("context", on ? "1" : "0")
                }
                Line {}
                TSwitch {
                    theme: page.theme
                    Layout.fillWidth: true
                    title: qsTr("Start at login")
                    detail: qsTr("tala waits in the background for its key.")
                    checked: { tala.revision; return tala.autostart() }
                    onToggled: on => tala.setAutostart(on)
                }
                Line {}
                Row2 {
                    title: qsTr("Keep recordings")
                    detail: qsTr("The audio behind each dictation, for a retry. The text stays either way.")
                    TCombo {
                        theme: page.theme
                        width: 180
                        readonly property var days: [0, 1, 7, 14, 30]
                        model: [qsTr("Don't keep"), qsTr("1 day"), qsTr("7 days"), qsTr("14 days"), qsTr("30 days")]
                        currentIndex: { tala.revision; return Math.max(0, days.indexOf(parseInt(tala.setting("keep_audio_days", "14")))) }
                        onActivated: i => tala.setSetting("keep_audio_days", String(days[i]))
                    }
                }
                Line {}
                Row2 {
                    title: qsTr("History")
                    detail: qsTr("Every dictation and its recording on this computer.")
                    TButton { theme: page.theme; destructive: true; text: qsTr("Clear history…"); onClicked: confirmClear.open() }
                }
            }
        }

        SectionTitle { theme: page.theme; text: qsTr("Speech engine"); Layout.topMargin: 8 }
        Card {
            theme: page.theme
            Layout.fillWidth: true
            implicitHeight: engine.implicitHeight + 28
            ColumnLayout {
                id: engine
                anchors.fill: parent
                anchors.margins: 14
                spacing: 6
                Row2 {
                    title: "Phonon-2 · " + tala.engineStatus
                    detail: qsTr("The model loads in a fraction of a second when you start dictating, and leaves memory once it has been idle this long.")
                    TCombo {
                        theme: page.theme
                        width: 180
                        readonly property var minutes: [1, 5, 30, 0]
                        model: [qsTr("1 minute"), qsTr("5 minutes"), qsTr("30 minutes"), qsTr("Always loaded")]
                        currentIndex: { tala.revision; return Math.max(0, minutes.indexOf(parseInt(tala.setting("engine_idle_minutes", "5")))) }
                        onActivated: i => tala.setSetting("engine_idle_minutes", String(minutes[i]))
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Fermion Research's speech model (CC-BY-4.0, from NVIDIA's Parakeet TDT 0.6B v3), running on this computer. Nothing you say leaves it.")
                    color: theme.secondary
                    font.pixelSize: theme.small
                    wrapMode: Text.Wrap
                }
                Text { text: "tala " + tala.version; color: theme.tertiary; font.pixelSize: theme.small; Layout.topMargin: 6 }
            }
        }
    }

    Dialog {
        id: confirmClear
        anchors.centerIn: parent
        modal: true
        title: qsTr("Clear history?")
        standardButtons: Dialog.Cancel | Dialog.Ok
        Text { text: qsTr("Every dictation and its recording is deleted. This can't be undone."); color: theme.label; wrapMode: Text.Wrap; width: 320 }
        onAccepted: tala.clearHistory()
    }
}
