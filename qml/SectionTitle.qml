import QtQuick

Text {
    property Theme theme
    color: theme.secondary
    font.pixelSize: theme.small
    font.weight: Font.DemiBold
    font.capitalization: Font.AllUppercase
    font.letterSpacing: 0.4
}
