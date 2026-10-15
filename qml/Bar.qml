// The bar at the bottom of the screen (Wispr Flow's Flow Bar): a small pill
// at rest, a waveform while listening, dots while the model works. A click
// on the resting pill starts hands-free dictation.
import QtQuick
import QtQuick.Window

Window {
    id: win
    width: 320
    height: 96
    visible: false  // shown once it is a layer surface (main.cpp)
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus

    readonly property bool active: tala.state !== "idle"
    property string notice: ""

    // Only the pill (and a notice) take clicks; the rest of the window lets
    // them through to whatever is underneath.
    function updateMask() {
        const r = pill.mapToItem(null, 0, 0, pill.width, pill.height)
        let rects = [Qt.rect(r.x, r.y, r.width, r.height)]
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
    }
    Timer { id: noticeTimer; interval: 4500; onTriggered: win.notice = "" }

    // The last ~40 loudness values, newest on the right.
    property var levels: []
    Connections {
        target: tala
        function onLevelChanged() {
            if (tala.state !== "recording")
                return
            const l = win.levels.slice(-39)
            l.push(tala.level)
            win.levels = l
        }
        function onStateChanged() {
            if (tala.state === "recording")
                win.levels = []
        }
    }

    Rectangle {
        id: bubble
        visible: win.notice !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: pill.top
        anchors.bottomMargin: 8
        width: Math.min(noticeText.implicitWidth + 28, win.width - 8)
        height: noticeText.implicitHeight + 16
        radius: height / 2
        color: "#e6141416"
        border.color: "#33ffffff"
        border.width: 1
        onVisibleChanged: win.updateMask()
        onWidthChanged: win.updateMask()
        Text {
            id: noticeText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, win.width - 36)
            text: win.notice
            color: "white"
            font.pixelSize: 13
            elide: Text.ElideRight
        }
    }

    Rectangle {
        id: pill
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        width: win.active ? (tala.locked ? 168 : 112) : (hover.hovered ? 64 : 44)
        height: win.active ? 34 : (hover.hovered ? 14 : 8)
        radius: height / 2
        color: win.active || hover.hovered ? "#f0141416" : "#99141416"
        border.color: "#40ffffff"
        border.width: 1
        onWidthChanged: win.updateMask()
        onHeightChanged: win.updateMask()
        Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        HoverHandler { id: hover }
        TapHandler {
            enabled: !win.active
            onTapped: tala.toggle()
        }

        // Listening: bars from the recent loudness.
        Row {
            id: wave
            visible: tala.state === "recording"
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: tala.locked ? 0 : 0
            spacing: 2
            Repeater {
                model: 16
                Rectangle {
                    readonly property real v: {
                        const n = win.levels.length
                        const i = n - 16 + index
                        return i >= 0 ? win.levels[i] : 0
                    }
                    width: 3
                    height: Math.max(3, v * 20)
                    radius: 1.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: "white"
                    opacity: 0.55 + v * 0.45
                    Behavior on height { NumberAnimation { duration: 60 } }
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

    Component.onCompleted: updateMask()
}
