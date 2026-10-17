import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: emojiPicker
    visible: false
    width: 300; height: 250
    x: parent.width - 72 - 240 - width - 16
    y: parent.height - 68 - height - 8
    color: "#2B2D31"; radius: 8; z: 100

    property var targetInput: null

    function connectInput(input) {
        targetInput = input
    }

    Rectangle { anchors.fill: parent; color: "transparent"; border.color: "#1E1F22"; border.width: 1; radius: 8 }

    Column {
        anchors.fill: parent; anchors.margins: 8; spacing: 6
        Text { text: "Emoji"; color: "#80848E"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.5; topPadding: 4 }

        GridView {
            id: emojiGrid
            width: parent.width; height: parent.height - 30
            clip: true
            cellWidth: 36; cellHeight: 36
            model: ["\uD83D\uDE00","\uD83D\uDE01","\uD83D\uDE02","\uD83E\uDD23","\uD83D\uDE03","\uD83D\uDE04","\uD83D\uDE05","\uD83D\uDE06","\uD83D\uDE09","\uD83D\uDE0A","\uD83D\uDE0B","\uD83D\uDE0E","\uD83D\uDE0D","\uD83D\uDE18","\uD83E\uDD70","\uD83D\uDE17","\uD83D\uDE19","\uD83D\uDE1A","\uD83D\uDE42","\uD83E\uDD17","\uD83E\uDD29","\uD83E\uDD14","\uD83E\uDEE1","\uD83E\uDD28","\uD83D\uDE10","\uD83D\uDE11","\uD83D\uDE36","\uD83D\uDE0F","\uD83D\uDE12","\uD83D\uDE44","\uD83D\uDE2C","\uD83E\uDD25","\uD83D\uDE14","\uD83D\uDE2A","\uD83E\uDD24","\uD83D\uDE34","\uD83D\uDE37","\uD83D\uDE24","\uD83D\uDE20","\uD83D\uDE21","\uD83E\uDD2C","\uD83D\uDE08","\uD83D\uDC7F","\uD83D\uDC80","\u2620\uFE0F","\uD83D\uDCA9","\uD83E\uDD21","\uD83D\uDC79","\uD83D\uDC7A","\uD83D\uDC7B","\uD83D\uDC4D","\uD83D\uDC4E","\u2764\uFE0F","\uD83D\uDC94","\u2B50","\uD83D\uDD25","\u26A1","\uD83C\uDFB5","\uD83D\uDE80","\uD83C\uDF89","\uD83D\uDC31","\uD83D\uDC36","\uD83E\uDD81","\uD83D\uDC38","\uD83E\uDD8A","\uD83D\uDC3A","\uD83E\uDD9D","\uD83D\uDC35","\uD83D\uDE48","\uD83E\uDD84","\uD83C\uDF08","\u2744\uFE0F","\u2600\uFE0F","\uD83C\uDF19","\u26C5","\uD83C\uDF38","\uD83C\uDF3A","\uD83C\uDF3B","\uD83C\uDF55","\uD83C\uDF54"]
            delegate: Rectangle {
                width: emojiGrid.cellWidth; height: emojiGrid.cellHeight
                color: emojiMa.containsMouse ? "#35373C" : "transparent"
                Text { anchors.centerIn: parent; text: modelData; font.pixelSize: 20 }
                MouseArea {
                    id: emojiMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (emojiPicker.targetInput) {
                            let cur = emojiPicker.targetInput.text
                            if (cur.length > 0 && cur[cur.length-1] !== ' ') cur += ' '
                            cur += modelData + ' '
                            emojiPicker.targetInput.text = cur
                            emojiPicker.targetInput.cursorPosition = cur.length
                            emojiPicker.targetInput.forceActiveFocus()
                        }
                        emojiPicker.visible = false
                    }
                }
            }
        }
    }

    // Close when clicking outside
    MouseArea {
        anchors.fill: parent; propagateComposedEvents: true; onClicked: function(mouse) { mouse.accepted = false }
    }
    MouseArea {
        anchors.fill: parent; z: -1; visible: emojiPicker.visible; onClicked: emojiPicker.visible = false
    }
}