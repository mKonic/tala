import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: c
    property Theme theme
    implicitHeight: 32
    font.pixelSize: theme.body
    contentItem: Text {
        leftPadding: 10
        rightPadding: c.indicator.width + 6
        text: c.displayText
        font: c.font
        color: theme.label
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Text {
        x: c.width - width - 10
        y: (c.height - height) / 2
        text: "⌄"
        color: theme.secondary
        font.pixelSize: 14
    }
    background: Rectangle {
        radius: 7
        color: c.hovered ? theme.cardHover : theme.card
        border.width: c.visualFocus ? 2 : 1
        border.color: c.visualFocus ? theme.accent : theme.separator
    }
    delegate: ItemDelegate {
        required property var modelData
        required property int index
        width: c.width
        highlighted: c.highlightedIndex === index
        contentItem: Text { text: modelData; color: theme.label; font: c.font; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
        background: Rectangle { radius: 5; color: highlighted ? theme.selection : "transparent" }
    }
    popup: Popup {
        y: c.height + 4
        width: c.width
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: c.popup.visible ? c.delegateModel : null
            currentIndex: c.highlightedIndex
        }
        background: Rectangle { radius: 8; color: theme.card; border.width: 1; border.color: theme.separator }
    }
}
