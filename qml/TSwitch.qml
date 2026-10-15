// A setting row's switch: label and one line of explanation on the left.
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

RowLayout {
    id: row
    property Theme theme
    property string title
    property string detail
    property bool checked
    signal toggled(bool on)
    spacing: 16
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Text { text: row.title; color: theme.label; font.pixelSize: theme.body; Layout.fillWidth: true; wrapMode: Text.Wrap }
        Text { visible: row.detail !== ""; text: row.detail; color: theme.secondary; font.pixelSize: theme.small; Layout.fillWidth: true; wrapMode: Text.Wrap }
    }
    Switch {
        id: sw
        checked: row.checked
        onToggled: row.toggled(checked)
        indicator: Rectangle {
            implicitWidth: 38; implicitHeight: 22; radius: 11
            color: sw.checked ? theme.accent : theme.separator
            Rectangle {
                width: 18; height: 18; radius: 9
                y: 2; x: sw.checked ? parent.width - width - 2 : 2
                color: "white"
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
    }
}
