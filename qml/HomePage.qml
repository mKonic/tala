// Home: totals, then every dictation by day, newest first, with copy, retry
// and delete on each.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: home
    property Theme theme: Theme {}
    property string query: ""

    readonly property var stats: { tala.revision; return tala.stats() }
    readonly property var rows: { tala.revision; return tala.history(home.query) }

    function day(ms) {
        const d = new Date(ms), now = new Date()
        const midnight = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime()
        if (ms >= midnight) return qsTr("Today")
        if (ms >= midnight - 86400000) return qsTr("Yesterday")
        return d.toLocaleDateString(Qt.locale(), "dddd, d MMMM")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 18

        Text {
            text: qsTr("Welcome back")
            color: theme.label
            font.pixelSize: theme.title
            font.weight: Font.DemiBold
        }
        Text {
            Layout.fillWidth: true
            text: tala.triggers.dictate
                  ? qsTr("Hold %1 and speak; let go and the text is typed where your cursor is. Double-tap it to keep going hands-free.").arg(tala.triggers.dictate)
                  : qsTr("Waiting for the desktop to give tala its keys.")
            color: theme.secondary
            font.pixelSize: theme.body
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Repeater {
                model: [
                    { value: Math.round(home.stats.words).toLocaleString(), label: qsTr("words dictated") },
                    { value: Math.round(home.stats.wpm), label: qsTr("words per minute") },
                    { value: home.stats.dictations, label: qsTr("dictations") },
                ]
                Card {
                    required property var modelData
                    theme: home.theme
                    Layout.fillWidth: true
                    implicitHeight: 74
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 16
                        spacing: 2
                        Text { text: modelData.value; color: theme.label; font.pixelSize: 24; font.weight: Font.DemiBold }
                        Text { text: modelData.label; color: theme.secondary; font.pixelSize: theme.small }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            SectionTitle { theme: home.theme; text: qsTr("History"); Layout.fillWidth: true }
            TField {
                theme: home.theme
                Layout.preferredWidth: 240
                placeholderText: qsTr("Search")
                onTextChanged: home.query = text
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: home.rows
            ScrollBar.vertical: ScrollBar {}

            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: home.query !== "" ? qsTr("Nothing matches") : qsTr("Your dictations will show up here")
                color: theme.tertiary
                font.pixelSize: theme.body
            }

            delegate: Column {
                id: entry
                required property var modelData
                required property int index
                readonly property string dayLabel: home.day(modelData.time)
                // A heading where the day changes.
                readonly property bool firstOfDay: index === 0 || home.day(home.rows[index - 1].time) !== dayLabel
                width: ListView.view.width - 12
                spacing: 6
                Text {
                    visible: entry.firstOfDay
                    topPadding: entry.index === 0 ? 0 : 10
                    text: entry.dayLabel
                    color: theme.secondary
                    font.pixelSize: theme.small
                    font.weight: Font.DemiBold
                }
            Card {
                id: row
                readonly property var modelData: entry.modelData
                theme: home.theme
                width: parent.width
                implicitHeight: Math.max(52, body.implicitHeight + 24)
                color: rowHover.hovered ? theme.cardHover : theme.card
                HoverHandler { id: rowHover }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 14
                    Text {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 12
                        Layout.preferredWidth: 58
                        text: new Date(row.modelData.time).toLocaleTimeString(Qt.locale(), "HH:mm")
                        color: theme.tertiary
                        font.pixelSize: theme.small
                    }
                    Text {
                        id: body
                        Layout.fillWidth: true
                        text: row.modelData.status === "failed" ? qsTr("Transcription failed. Retry to try again.")
                            : row.modelData.status === "empty" ? qsTr("Nothing was heard")
                            : row.modelData.text
                        color: row.modelData.status === "failed" ? theme.red
                             : row.modelData.status === "empty" ? theme.tertiary : theme.label
                        font.pixelSize: theme.body
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }
                    Row {
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 4
                        opacity: rowHover.hovered ? 1 : 0
                        TButton {
                            theme: home.theme
                            visible: row.modelData.text !== ""
                            text: qsTr("Copy")
                            onClicked: tala.copy(row.modelData.text)
                        }
                        TButton {
                            theme: home.theme
                            visible: row.modelData.hasAudio
                            text: qsTr("Retry")
                            onClicked: tala.retry(row.modelData.id)
                        }
                        TButton {
                            theme: home.theme
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
}
