// The bar at the bottom of the screen (Wispr Flow's Flow Bar): a small pill
// at rest, a waveform while listening, dots while the model works. A click
// on the resting pill starts hands-free dictation; a right click has
// the microphone to use, "Hide for 1 hour" and "Open Tala".
import QtQuick
import QtQuick.Window

Window {
    id: win
    // Out of a quick note's screenshot.
    Binding { target: win.contentItem; property: "opacity"; value: tala.capturing ? 0 : 1 }
    width: 360
    // Taller while the menu is open (its microphones); the surface is
    // anchored at the bottom, so it grows upward.
    height: menu ? Math.max(104, menuRow.implicitHeight + 72) : 104
    visible: false  // shown once it is a layer surface (main.cpp)
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus

    readonly property bool active: tala.state !== "idle"
    property string notice: ""
    property bool menu: false
    property double hiddenUntil: 0  // ms; the resting pill is away until then
    readonly property bool resting: !active && hiddenUntil === 0

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
            win.menu = false
            win.notice = text
            noticeTimer.restart()
        }
        function onStateChanged() {
            win.menu = false
            if (tala.state === "recording")
                win.levels = []
        }
    }
    Timer { id: noticeTimer; interval: 4500; onTriggered: win.notice = "" }
    // When "Hide for 1 hour" runs out.
    Timer { id: hideTimer; interval: 3600000; onTriggered: win.hiddenUntil = 0 }

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

    readonly property string bubbleText: notice !== "" ? notice
        : hover.hovered && resting && !menu
          ? (tala.triggers.dictate ? qsTr("Click, or hold %1, to dictate").arg(tala.triggers.dictate) : qsTr("Click to dictate"))
          : ""

    Rectangle {
        id: bubble
        visible: win.bubbleText !== "" || win.menu
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: pill.top
        anchors.bottomMargin: 8
        width: win.menu ? menuRow.implicitWidth + 12 : Math.min(noticeText.implicitWidth + 28, win.width - 8)
        height: win.menu ? menuRow.implicitHeight + 12 : noticeText.implicitHeight + 16
        radius: win.menu ? 12 : height / 2
        color: "#ee141416"
        border.color: "#33ffffff"
        border.width: 1
        onVisibleChanged: win.maskLater()
        onWidthChanged: win.maskLater()
        onHeightChanged: win.maskLater()
        Text {
            id: noticeText
            visible: !win.menu
            anchors.centerIn: parent
            width: Math.min(implicitWidth, win.width - 36)
            text: win.bubbleText
            color: "white"
            font.pixelSize: 13
            elide: Text.ElideRight
        }
        Column {
            id: menuRow
            visible: win.menu
            anchors.centerIn: parent
            spacing: 2
            readonly property string mic: { tala.revision; return tala.setting("mic", "") }
            readonly property var items: [{ kind: "head", text: qsTr("Microphone") },
                                          { kind: "mic", name: "", text: qsTr("Auto") }]
                .concat(tala.devices.map(d => ({ kind: "mic", name: d.name, text: d.description })))
                .concat([{ kind: "line" },
                         { kind: "hide", text: qsTr("Hide for 1 hour") },
                         { kind: "open", text: qsTr("Open Tala") }])
            Repeater {
                model: menuRow.items
                Rectangle {
                    required property var modelData
                    readonly property bool action: modelData.kind !== "head" && modelData.kind !== "line"
                    width: 240
                    height: modelData.kind === "line" ? 9 : modelData.kind === "head" ? 24 : 28
                    radius: 8
                    color: action && itemHover.hovered ? "#33ffffff" : "transparent"
                    Rectangle {
                        visible: modelData.kind === "line"
                        anchors.centerIn: parent
                        width: parent.width - 16
                        height: 1
                        color: "#33ffffff"
                    }
                    Text {
                        visible: modelData.kind === "mic" && modelData.name === menuRow.mic
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: "✓"
                        color: "white"
                        font.pixelSize: 13
                    }
                    HoverHandler { id: itemHover; enabled: parent.action }
                    TapHandler {
                        enabled: parent.action
                        onTapped: {
                            win.menu = false
                            const k = modelData.kind
                            if (k === "mic") {
                                tala.setSetting("mic", modelData.name)
                            } else if (k === "hide") {
                                win.hiddenUntil = Date.now() + hideTimer.interval
                                hideTimer.restart()
                            } else if (k === "open") {
                                tala.openHub()
                            }
                        }
                    }
                    Text {
                        id: label
                        x: 28
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width - 38)
                        elide: Text.ElideRight
                        text: modelData.text ?? ""
                        color: modelData.kind === "head" ? "#99ffffff" : "white"
                        font.pixelSize: modelData.kind === "head" ? 11 : 13
                    }
                }
            }
        }
        HoverHandler { id: bubbleHover }
    }
    // The menu goes when the pointer has left it a moment.
    Timer {
        running: win.menu && !bubbleHover.hovered && !hover.hovered
        interval: 1500
        onTriggered: win.menu = false
    }

    Rectangle {
        id: pill
        visible: win.active || win.resting
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 4
        width: win.active ? (tala.locked ? 176 : 120) : (hover.hovered ? 64 : 44)
        height: win.active ? 34 : (hover.hovered ? 14 : 8)
        radius: height / 2
        color: win.active || hover.hovered ? "#f0141416" : "#99141416"
        border.color: "#40ffffff"
        border.width: 1
        onVisibleChanged: win.maskLater()
        onWidthChanged: win.maskLater()
        onHeightChanged: win.maskLater()
        onXChanged: win.maskLater()
        onYChanged: win.maskLater()
        Behavior on width { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        HoverHandler { id: hover }
        TapHandler {
            enabled: !win.active
            acceptedButtons: Qt.LeftButton
            onTapped: tala.toggle()
        }
        TapHandler {
            enabled: !win.active
            acceptedButtons: Qt.RightButton
            onTapped: {
                win.notice = ""
                win.menu = !win.menu
            }
        }

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
    onRestingChanged: maskLater()
    Component.onCompleted: maskLater()
}
