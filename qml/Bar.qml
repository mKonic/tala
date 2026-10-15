// The bar at the bottom of the screen (Wispr Flow's Flow Bar): a waveform
// while listening, dots while the model works, and Tala's notices. Nothing
// at rest; the tray says Tala is there.
import QtQuick
import QtQuick.Window

Window {
    id: win
    // Out of a quick note's screenshot.
    Binding { target: win.contentItem; property: "opacity"; value: tala.capturing ? 0 : 1 }
    width: 360
    height: 104
    visible: false  // shown once it is a layer surface (main.cpp)
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus

    readonly property bool active: tala.state !== "idle"
    property string notice: ""

    // Only the pill and the bubble take clicks; the rest of the window lets
    // them through to whatever is underneath.
    function updateMask() {
        let rects = []
        if (pill.visible) {
            const r = pill.mapToItem(null, 0, 0, pill.width, pill.height)
            rects.push(Qt.rect(r.x, r.y, r.width, r.height))
        }
        if (bubble.visible) {
            const b = bubble.mapToItem(null, 0, 0, bubble.width, bubble.height)
            rects.push(Qt.rect(b.x, b.y, b.width, b.height))
        }
        ui.setMask(win, rects)
    }

    Connections {
        target: tala
        function onNotice(text) {
            win.notice = text
            noticeTimer.restart()
        }
        function onStateChanged() {
            if (tala.state === "recording")
                win.levels = []
        }
    }
    Timer { id: noticeTimer; interval: 4500; onTriggered: win.notice = "" }

    // Loudness sampled at a steady pace, so the bars move like speech
    // rather than flicker at the audio's block rate.
    property var levels: []
    Timer {
        interval: 60
        repeat: true
        running: tala.state === "recording"
        onTriggered: {
            const l = win.levels.slice(-19)
            l.push(tala.level)
            win.levels = l
        }
    }

    readonly property string bubbleText: notice

    Rectangle {
        id: bubble
        visible: win.bubbleText !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: pill.top
        anchors.bottomMargin: 8
        width: Math.min(noticeText.implicitWidth + 28, win.width - 8)
        height: noticeText.implicitHeight + 16
        radius: height / 2
        color: "#ee141416"
        border.color: "#33ffffff"
        border.width: 1
        onVisibleChanged: win.maskLater()
        onWidthChanged: win.maskLater()
        onHeightChanged: win.maskLater()
        Text {
            id: noticeText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, win.width - 36)
            text: win.bubbleText
            color: "white"
            font.pixelSize: 13
            elide: Text.ElideRight
        }
    }

    Rectangle {
        id: pill
        visible: win.active
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        width: tala.locked ? 176 : 120
        height: 34
        radius: height / 2
        color: "#f0141416"
        border.color: "#40ffffff"
        border.width: 1
        onVisibleChanged: win.maskLater()
        onWidthChanged: win.maskLater()
        onHeightChanged: win.maskLater()
        onXChanged: win.maskLater()
        onYChanged: win.maskLater()
        Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }


        // Listening: bars from the recent loudness, newest on the right.
        Row {
            visible: tala.state === "recording"
            anchors.centerIn: parent
            spacing: 2
            Repeater {
                model: 20
                Rectangle {
                    required property int index
                    readonly property real v: {
                        const i = win.levels.length - 20 + index
                        return i >= 0 ? win.levels[i] : 0
                    }
                    width: 3
                    height: 3 + v * 19
                    radius: 1.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: "white"
                    opacity: 0.5 + v * 0.5
                    Behavior on height { NumberAnimation { duration: 80 } }
                }
            }
        }

        // Hands-free: cancel on the left, stop on the right.
        Rectangle {
            visible: tala.state === "recording" && tala.locked
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 24; height: 24; radius: 12
            color: cancelHover.hovered ? "#40ffffff" : "#26ffffff"
            HoverHandler { id: cancelHover }
            TapHandler { onTapped: tala.cancel() }
            Text { anchors.centerIn: parent; text: "✕"; color: "white"; font.pixelSize: 11 }
        }
        Rectangle {
            visible: tala.state === "recording" && tala.locked
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            width: 24; height: 24; radius: 12
            color: "#ff453a"
            TapHandler { onTapped: tala.stop() }
            Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 1.5; color: "white" }
        }

        // Working: three dots.
        Row {
            visible: tala.state === "processing"
            anchors.centerIn: parent
            spacing: 5
            Repeater {
                model: 3
                Rectangle {
                    required property int index
                    width: 6; height: 6; radius: 3
                    color: "white"
                    SequentialAnimation on opacity {
                        running: tala.state === "processing"
                        loops: Animation.Infinite
                        PauseAnimation { duration: index * 150 }
                        NumberAnimation { from: 0.25; to: 1; duration: 300 }
                        NumberAnimation { from: 1; to: 0.25; duration: 300 }
                        PauseAnimation { duration: (2 - index) * 150 }
                    }
                }
            }
        }
    }

    // Batched: several geometry changes land in the same frame.
    function maskLater() { Qt.callLater(updateMask) }
    onWidthChanged: maskLater()
    onHeightChanged: maskLater()
    onActiveChanged: maskLater()
    Component.onCompleted: maskLater()
}
