import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: hub
    width: 900
    height: 620
    visible: true
    title: "tala"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        Label { text: tala.engineStatus + (tala.problem ? " — " + tala.problem : "") }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: { tala.revision; return tala.history() }
            delegate: Label {
                required property var modelData
                width: ListView.view.width
                wrapMode: Text.Wrap
                text: modelData.status + ": " + modelData.text
            }
        }
    }
}
