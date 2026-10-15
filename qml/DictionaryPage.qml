// Dictionary: names and terms tala should know. A plain word is favoured
// while listening (Phonon-2 hotwords); a correction also replaces a
// misspelling that keeps coming out.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: page
    property Theme theme: Theme {}
    property string query: ""
    property int editing: 0  // id being edited, 0 for a new word
    property string error: ""

    readonly property var words: {
        tala.revision
        const q = query.toLowerCase()
        return tala.words().filter(w => q === "" || w.word.toLowerCase().includes(q) || w.misspelling.toLowerCase().includes(q))
    }

    function edit(w) {
        editing = w ? w.id : 0
        word.text = w ? w.word : ""
        misspelling.text = w ? w.misspelling : ""
        correct.checked = w ? w.misspelling !== "" : false
        error = ""
        form.visible = true
        word.forceActiveFocus()
    }
    function save() {
        error = tala.saveWord(editing, word.text, correct.checked ? misspelling.text : "")
        if (error === "")
            form.visible = false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Dictionary"); color: theme.label; font.pixelSize: theme.title; font.weight: Font.DemiBold; Layout.fillWidth: true }
            TField { theme: page.theme; Layout.preferredWidth: 220; placeholderText: qsTr("Search"); onTextChanged: page.query = text }
            TButton { theme: page.theme; primary: true; text: qsTr("Add new"); onClicked: page.edit(null) }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Names, products and jargon. tala listens for them, and a correction fixes a spelling that keeps coming out wrong.")
            color: theme.secondary
            font.pixelSize: theme.body
            wrapMode: Text.Wrap
        }

        Card {
            id: form
            theme: page.theme
            visible: false
            Layout.fillWidth: true
            implicitHeight: formColumn.implicitHeight + 32
            ColumnLayout {
                id: formColumn
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10
                TField {
                    id: word
                    theme: page.theme
                    Layout.fillWidth: true
                    maximumLength: 60
                    placeholderText: qsTr("The word, as it should be written")
                    onAccepted: page.save()
                }
                CheckBox {
                    id: correct
                    text: qsTr("Correct a misspelling")
                    contentItem: Text { leftPadding: correct.indicator.width + 8; text: correct.text; color: theme.label; font.pixelSize: theme.body; verticalAlignment: Text.AlignVCenter }
                }
                TField {
                    id: misspelling
                    theme: page.theme
                    visible: correct.checked
                    Layout.fillWidth: true
                    maximumLength: 60
                    placeholderText: qsTr("What comes out instead")
                    onAccepted: page.save()
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: page.error; color: theme.red; font.pixelSize: theme.small; Layout.fillWidth: true }
                    TButton { theme: page.theme; text: qsTr("Cancel"); onClicked: form.visible = false }
                    TButton { theme: page.theme; primary: true; text: page.editing ? qsTr("Save changes") : qsTr("Add word"); onClicked: page.save() }
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: page.words
            ScrollBar.vertical: ScrollBar {}
            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: page.query !== "" ? qsTr("Nothing matches") : qsTr("No words yet")
                color: theme.tertiary
                font.pixelSize: theme.body
            }
            delegate: Card {
                required property var modelData
                theme: page.theme
                width: ListView.view.width - 12
                implicitHeight: 46
                color: hover.hovered ? theme.cardHover : theme.card
                HoverHandler { id: hover }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 10
                    Text {
                        visible: modelData.misspelling !== ""
                        text: modelData.misspelling + "  →"
                        color: theme.secondary
                        font.pixelSize: theme.body
                    }
                    Text { text: modelData.word; color: theme.label; font.pixelSize: theme.body; Layout.fillWidth: true; elide: Text.ElideRight }
                    Row {
                        spacing: 4
                        opacity: hover.hovered ? 1 : 0
                        TButton { theme: page.theme; text: qsTr("Edit"); onClicked: page.edit(modelData) }
                        TButton { theme: page.theme; destructive: true; text: qsTr("Delete"); onClicked: tala.removeWord(modelData.id) }
                    }
                }
            }
        }
    }
}
