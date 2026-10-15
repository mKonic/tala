import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: f
    property Theme theme
    implicitHeight: 32
    font.pixelSize: theme.body
    color: theme.label
    placeholderTextColor: theme.tertiary
    selectionColor: theme.selection
    selectedTextColor: theme.label
    leftPadding: 10
    rightPadding: 10
    background: Rectangle {
        radius: 7
        color: theme.field
        border.width: f.activeFocus ? 2 : 1
        border.color: f.activeFocus ? theme.accent : theme.separator
    }
}
