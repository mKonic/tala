// Style: how dictated text should read, per kind of app (Flow's tabs), with
// a live preview of the rules.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Flickable {
    id: page
    property Theme theme: Theme {}
    contentHeight: column.implicitHeight + 56
    clip: true
    ScrollBar.vertical: ScrollBar {}

    property string category: "personal"
    readonly property var categories: [
        { id: "personal", title: qsTr("Personal messages"), detail: qsTr("Signal, WhatsApp, Telegram, Discord, Messenger and the like, in their apps or a browser.") },
        { id: "work", title: qsTr("Work messages"), detail: qsTr("Slack, Teams, Mattermost, Google Chat, Zoom.") },
        { id: "email", title: qsTr("Email"), detail: qsTr("Thunderbird, Evolution, Geary, and Gmail, Outlook or Proton Mail in a browser.") },
        { id: "other", title: qsTr("Other"), detail: qsTr("Everything else: documents, notes, code, the web.") }
    ]
    readonly property string style: { tala.revision; return tala.setting("style." + category, tala.setting("style", "formal")) }
    readonly property var styles: [
        { id: "formal", title: qsTr("Formal."), detail: qsTr("Capitals and full punctuation."),
          sample: "Hey, are you free for lunch tomorrow? Let's do 12 if that works for you." },
        { id: "casual", title: qsTr("Casual"), detail: qsTr("Capitals, no final period."),
          sample: "Hey, are you free for lunch tomorrow? Let's do 12 if that works for you" },
        { id: "very_casual", title: qsTr("very casual"), detail: qsTr("No capitals at sentence starts, no final period."),
          sample: "hey, are you free for lunch tomorrow? let's do 12 if that works for you" },
    ]

    ColumnLayout {
        id: column
        x: 28
        y: 28
        width: page.width - 56
        spacing: 16

        Text { text: qsTr("Style"); color: theme.label; font.pixelSize: theme.title; font.weight: Font.DemiBold }

        // One style per kind of app.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 36
            radius: 10
            color: theme.window
            border.width: 1
            border.color: theme.separator
            RowLayout {
                anchors.fill: parent
                anchors.margins: 3
                spacing: 3
                Repeater {
                    model: page.categories
                    Rectangle {
                        required property var modelData
                        readonly property bool current: page.category === modelData.id
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: current ? theme.card : tabHover.hovered ? theme.cardHover : "transparent"
                        border.width: current && !theme.dark ? 1 : 0
                        border.color: theme.separator
                        HoverHandler { id: tabHover }
                        TapHandler { onTapped: page.category = modelData.id }
                        Text {
                            anchors.centerIn: parent
                            text: modelData.title
                            color: current ? theme.label : theme.secondary
                            font.pixelSize: theme.small
                            font.weight: current ? Font.DemiBold : Font.Normal
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: page.categories.find(c => c.id === page.category).detail
            color: theme.tertiary
            font.pixelSize: theme.small
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("How your dictation is written. Phonon-2 already punctuates and writes numbers as digits; these are the finishing touches.")
            color: theme.secondary
            font.pixelSize: theme.body
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Repeater {
                model: page.styles
                Card {
                    required property var modelData
                    readonly property bool current: page.style === modelData.id
                    theme: page.theme
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    implicitHeight: styleColumn.implicitHeight + 28
                    border.width: current ? 2 : (theme.dark ? 0 : 1)
                    border.color: current ? theme.accent : theme.separator
                    color: hover.hovered && !current ? theme.cardHover : theme.card
                    HoverHandler { id: hover }
                    TapHandler { onTapped: tala.setSetting("style." + page.category, modelData.id) }
                    ColumnLayout {
                        id: styleColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        spacing: 6
                        Text { text: modelData.title; color: theme.label; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Text { text: modelData.detail; color: theme.secondary; font.pixelSize: theme.small; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            implicitHeight: sample.implicitHeight + 16
                            radius: 8
                            color: theme.window
                            Text {
                                id: sample
                                anchors.fill: parent
                                anchors.margins: 8
                                text: modelData.sample
                                color: theme.label
                                font.pixelSize: theme.small
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }

        SectionTitle { theme: page.theme; text: qsTr("Auto Cleanup"); Layout.topMargin: 8 }
        Text {
            Layout.fillWidth: true
            text: qsTr("A language model on this computer edits each dictation, in every app. Your words as spoken stay in History.")
            color: theme.secondary
            font.pixelSize: theme.small
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            readonly property string level: { tala.revision; return tala.setting("cleanup", "none") }
            id: levels
            Repeater {
                model: [
                    { id: "none", title: qsTr("None"), detail: qsTr("The rules only: fillers, commands, your dictionary."),
                      sample: "So we could meet Tuesday, actually no, Wednesday at three." },
                    { id: "light", title: qsTr("Light"), detail: qsTr("Self-corrections applied, grammar fixed, emails and numbers written out."),
                      sample: "Let's meet Wednesday at three." },
                    { id: "medium", title: qsTr("Medium"), detail: qsTr("May also reword for clarity, keeping your meaning."),
                      sample: "Let's meet Wednesday at three instead." }
                ]
                Card {
                    required property var modelData
                    readonly property bool current: levels.level === modelData.id
                    theme: page.theme
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    implicitHeight: levelColumn.implicitHeight + 28
                    border.width: current ? 2 : (theme.dark ? 0 : 1)
                    border.color: current ? theme.accent : theme.separator
                    color: levelHover.hovered && !current ? theme.cardHover : theme.card
                    HoverHandler { id: levelHover }
                    TapHandler { onTapped: tala.setSetting("cleanup", modelData.id) }
                    ColumnLayout {
                        id: levelColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        spacing: 6
                        Text { text: modelData.title; color: theme.label; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Text { text: modelData.detail; color: theme.secondary; font.pixelSize: theme.small; Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Text { text: modelData.sample; color: theme.label; font.pixelSize: theme.small; font.italic: true; Layout.fillWidth: true; wrapMode: Text.Wrap }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: levels.level !== "none" && tala.cleanupStatus !== ""
            text: "Qwen3-4B · " + tala.cleanupStatus
            color: theme.tertiary
            font.pixelSize: theme.small
            wrapMode: Text.Wrap
        }

        SectionTitle { theme: page.theme; text: qsTr("Cleanup"); Layout.topMargin: 8 }
        Card {
            theme: page.theme
            Layout.fillWidth: true
            implicitHeight: cleanup.implicitHeight + 28
            ColumnLayout {
                id: cleanup
                anchors.fill: parent
                anchors.margins: 14
                spacing: 14
                TSwitch {
                    theme: page.theme
                    Layout.fillWidth: true
                    title: qsTr("Remove fillers and false starts")
                    detail: qsTr("Drops um and uh, repeated words, and what comes before “scratch that”.")
                    checked: { tala.revision; return tala.setting("fillers", "1") === "1" }
                    onToggled: on => tala.setSetting("fillers", on ? "1" : "0")
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: theme.separator }
                TSwitch {
                    theme: page.theme
                    Layout.fillWidth: true
                    title: qsTr("Spoken commands")
                    detail: qsTr("“New line”, “new paragraph”, and punctuation said by name: “comma”, “period”, “question mark”.")
                    checked: { tala.revision; return tala.setting("commands", "1") === "1" }
                    onToggled: on => tala.setSetting("commands", on ? "1" : "0")
                }
            }
        }

        SectionTitle { theme: page.theme; text: qsTr("Try it"); Layout.topMargin: 8 }
        TField {
            id: tryIt
            theme: page.theme
            Layout.fillWidth: true
            text: "Um, so I'll be there at five. Scratch that. I'll be there at six comma see you then."
        }
        Card {
            theme: page.theme
            Layout.fillWidth: true
            implicitHeight: result.implicitHeight + 24
            Text {
                id: result
                anchors.fill: parent
                anchors.margins: 12
                text: { tala.revision; return tala.preview(tryIt.text, page.category) }
                color: theme.label
                font.pixelSize: theme.body
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
        }
    }
}
