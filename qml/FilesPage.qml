// Files: an audio or video recording to text (Detta's file transcription),
// with what was transcribed before.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: page
    property Theme theme: Theme {}
    readonly property var rows: { tala.revision; return tala.files() }
    property string saving: ""  // the text Save as… writes

    FileDialog {
        id: open
        title: qsTr("Transcribe a recording")
        nameFilters: [qsTr("Audio and video (*.wav *.mp3 *.m4a *.aac *.ogg *.opus *.flac *.webm *.mp4 *.mkv *.mov *.avi)"),
                      qsTr("All files (*)")]
        onAccepted: tala.transcribeFile(selectedFile)
    }
    FileDialog {
        id: save
        title: qsTr("Save the transcript")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Text (*.txt)")]
        onAccepted: {
            const why = tala.saveText(selectedFile, page.saving)
            if (why !== "")
                console.warn(why)
        }
    }

    // Dropped from the file manager.
    DropArea {
        anchors.fill: parent
        onDropped: drop => {
            if (drop.hasUrls && tala.fileStatus === "")
                tala.transcribeFile(drop.urls[0])
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        Text { text: qsTr("Files"); color: theme.label; font.pixelSize: theme.title; font.weight: Font.DemiBold }
        Text {
            Layout.fillWidth: true
            text: qsTr("A recording or a video to text: a meeting, a voice memo, an interview. It's read and transcribed on this computer, at any length.")
            color: theme.secondary
            font.pixelSize: theme.body
            wrapMode: Text.Wrap
        }

        Card {
            theme: page.theme
            Layout.fillWidth: true
            implicitHeight: pick.implicitHeight + 28
            ColumnLayout {
                id: pick
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: tala.fileStatus !== "" ? tala.fileStatus + "…" : qsTr("Choose a file, or drop one here.")
                        color: theme.label
                        font.pixelSize: theme.body
                        elide: Text.ElideMiddle
                    }
                    TButton {
                        theme: page.theme
                        visible: tala.fileStatus !== ""
                        text: qsTr("Cancel")
                        onClicked: tala.cancelFile()
                    }
                    TButton {
                        theme: page.theme
                        visible: tala.fileStatus === ""
                        primary: true
                        text: qsTr("Choose a file…")
                        onClicked: open.open()
                    }
                }
                Rectangle {
                    visible: tala.fileStatus !== ""
                    Layout.fillWidth: true
                    implicitHeight: 4
                    radius: 2
                    color: theme.separator
                    Rectangle {
                        width: parent.width * Math.max(0.02, tala.fileProgress)
                        height: parent.height
                        radius: 2
                        color: theme.accent
                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }
            }
        }

        Text {
            visible: page.rows.length === 0
            text: qsTr("Nothing transcribed yet.")
            color: theme.tertiary
            font.pixelSize: theme.body
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: page.rows
            ScrollBar.vertical: ScrollBar {}
            delegate: Card {
                id: row
                required property var modelData
                theme: page.theme
                width: ListView.view.width - 12
                implicitHeight: column.implicitHeight + 24
                color: hover.hovered ? theme.cardHover : theme.card
                HoverHandler { id: hover }
                ColumnLayout {
                    id: column
                    anchors.fill: parent
                    anchors.margins: 12
                    anchors.leftMargin: 14
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.app.replace(/^file:/, "")
                            color: theme.label
                            font.pixelSize: theme.body
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: new Date(row.modelData.time).toLocaleString(Qt.locale(), "d MMM HH:mm") + " · "
                                  + Math.max(1, Math.round(row.modelData.seconds / 60)) + qsTr(" min")
                            color: theme.tertiary
                            font.pixelSize: theme.small
                        }
                    }
                    Text {
                        id: body
                        Layout.fillWidth: true
                        text: row.modelData.text
                        color: theme.label
                        font.pixelSize: theme.body
                        wrapMode: Text.Wrap
                        maximumLineCount: expanded ? 100000 : 4
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        property bool expanded: false
                        TapHandler { onTapped: body.expanded = !body.expanded }
                    }
                    Row {
                        spacing: 4
                        TButton { theme: page.theme; text: qsTr("Copy"); onClicked: tala.copy(row.modelData.text) }
                        TButton {
                            theme: page.theme
                            text: qsTr("Save as…")
                            onClicked: {
                                page.saving = row.modelData.text
                                save.selectedFile = row.modelData.app.replace(/^file:/, "").replace(/\.[^.]*$/, "") + ".txt"
                                save.open()
                            }
                        }
                        TButton {
                            theme: page.theme
                            destructive: true
                            text: qsTr("Delete")
                            onClicked: tala.removeDictation(row.modelData.id)
                        }
                    }
                }
            }
        }
    }
}
