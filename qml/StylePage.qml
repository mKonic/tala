// Style: how dictated text should read, with a live preview of the rules.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Flickable {
    id: page
    property Theme theme: Theme {}
    contentHeight: column.implicitHeight + 56
    clip: true
    ScrollBar.vertical: ScrollBar {}

    readonly property string style: { tala.revision; return tala.setting("style", "formal") }
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
                    TapHandler { onTapped: tala.setSetting("style", modelData.id) }
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
                text: { tala.revision; return tala.preview(tryIt.text) }
                color: theme.label
                font.pixelSize: theme.body
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
        }
    }
}
