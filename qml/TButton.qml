import QtQuick
import QtQuick.Controls.Basic

Button {
    id: b
    property bool primary: false
    property bool destructive: false
    property Theme theme
    implicitHeight: 30
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: theme.body
    contentItem: Text {
        text: b.text
        font: b.font
        color: b.primary ? theme.labelOnAccent : b.destructive ? theme.red : theme.label
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        opacity: b.enabled ? 1 : 0.45
    }
    background: Rectangle {
        radius: 7
        color: b.primary ? (b.down ? Qt.darker(theme.accent, 1.15) : theme.accent)
                         : (b.down ? theme.separator : b.hovered ? theme.cardHover : theme.card)
        border.width: b.primary ? 0 : 1
        border.color: theme.separator
        opacity: b.enabled ? 1 : 0.6
    }
}
