import QtQuick

Rectangle {
    property Theme theme
    radius: theme.radius
    color: theme.card
    border.width: theme.dark ? 0 : 1
    border.color: theme.separator
}
