// Snippets: say the trigger, get the saved text (an address, a link, a
// signature), alone or inside a sentence.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: page
    property Theme theme: Theme {}
    property string query: ""
    property int editing: 0
    property string error: ""

    readonly property var snippets: {
        tala.revision
        const q = query.toLowerCase()
        return tala.snippetList().filter(s => q === "" || s.trigger.toLowerCase().includes(q) || s.expansion.toLowerCase().includes(q))
    }

    function edit(s) {
        editing = s ? s.id : 0
        trigger.text = s ? s.trigger : ""
        expansion.text = s ? s.expansion : ""
        error = ""
        form.visible = true
        trigger.forceActiveFocus()
    }
    function save() {
        error = tala.saveSnippet(editing, trigger.text, expansion.text)
        if (error === "")
            form.visible = false
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Snippets"); color: theme.label; font.pixelSize: theme.title; font.weight: Font.DemiBold; Layout.fillWidth: true }
            TField { theme: page.theme; Layout.preferredWidth: 220; placeholderText: qsTr("Search"); onTextChanged: page.query = text }
            TButton { theme: page.theme; primary: true; text: qsTr("Add new"); onClicked: page.edit(null) }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Say the trigger and Tala types what you saved. Pick something you wouldn't say by accident, like “my email signature”.")
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
                    id: trigger
                    theme: page.theme
                    Layout.fillWidth: true
                    maximumLength: 60
                    placeholderText: qsTr("Trigger: what you say")
                }
                TArea {
                    id: expansion
                    theme: page.theme
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    placeholderText: qsTr("Expansion: what gets typed")
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: page.error; color: theme.red; font.pixelSize: theme.small; Layout.fillWidth: true }
                    Text { text: expansion.text.length + "/4000"; color: expansion.text.length > 4000 ? theme.red : theme.tertiary; font.pixelSize: theme.small }
                    TButton { theme: page.theme; text: qsTr("Cancel"); onClicked: form.visible = false }
                    TButton { theme: page.theme; primary: true; text: page.editing ? qsTr("Save changes") : qsTr("Add snippet"); onClicked: page.save() }
                }
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: page.snippets
            ScrollBar.vertical: ScrollBar {}
            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: page.query !== "" ? qsTr("Nothing matches") : qsTr("No snippets yet")
                color: theme.tertiary
                font.pixelSize: theme.body
            }
            delegate: Card {
                required property var modelData
                theme: page.theme
                width: ListView.view.width - 12
                implicitHeight: Math.max(52, content.implicitHeight + 22)
                color: hover.hovered ? theme.cardHover : theme.card
                HoverHandler { id: hover }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 10
                    ColumnLayout {
                        id: content
                        Layout.fillWidth: true
                        spacing: 3
                        Text { text: "“" + modelData.trigger + "”"; color: theme.label; font.pixelSize: theme.body; font.weight: Font.DemiBold }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.expansion
                            color: theme.secondary
                            font.pixelSize: theme.small
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                        }
                    }
                    Row {
                        spacing: 4
                        opacity: hover.hovered ? 1 : 0
                        TButton { theme: page.theme; text: qsTr("Edit"); onClicked: page.edit(modelData) }
                        TButton { theme: page.theme; destructive: true; text: qsTr("Delete"); onClicked: tala.removeSnippet(modelData.id) }
                    }
                }
            }
        }
    }
}
