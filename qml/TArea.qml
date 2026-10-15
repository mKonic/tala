import QtQuick
import QtQuick.Controls.Basic

ScrollView {
    id: s
    property Theme theme
    property alias text: area.text
    property alias placeholderText: area.placeholderText
    property alias area: area
    implicitHeight: 96
    TextArea {
        id: area
        font.pixelSize: s.theme.body
        color: s.theme.label
        placeholderTextColor: s.theme.tertiary
        selectionColor: s.theme.selection
        selectedTextColor: s.theme.label
        wrapMode: TextEdit.Wrap
        padding: 10
        background: Rectangle {
            radius: 7
            color: s.theme.field
            border.width: area.activeFocus ? 2 : 1
            border.color: area.activeFocus ? s.theme.accent : s.theme.separator
        }
    }
}
