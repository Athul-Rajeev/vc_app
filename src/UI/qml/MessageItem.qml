import QtQuick

Item {
    id: root
    property string msgAuthor: ""
    property string msgContent: ""
    property string msgTimestamp: ""
    property string msgAvatarColor: "#5865F2"
    property string msgAvatarLetter: "?"
    property bool isGrouped: false
    property int msgListWidth: 600

    height: isGrouped ? groupedContentHeight : fullContentHeight
    readonly property int fullContentHeight: fullRow.height + 4   // compact vertical padding
    readonly property int groupedContentHeight: groupedText.height + 2

    // Hover background
    Rectangle {
        anchors.fill: parent
        color: msgHover.containsMouse ? "#2E3035" : "transparent"
        Behavior on color { ColorAnimation { duration: 50 } }
    }

    // Full message (with avatar)
    Row {
        id: fullRow
        visible: !isGrouped
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.leftMargin: 16; anchors.rightMargin: 16
        anchors.topMargin: 2
        spacing: 12

        Rectangle {
            width: 40; height: 40; radius: 20; color: msgAvatarColor
            Text { anchors.centerIn: parent; text: msgAvatarLetter; color: "white"; font.pixelSize: 16; font.weight: Font.Bold }
        }

        Column {
            width: parent.width - 52; spacing: 1
            Row { spacing: 6
                Text { text: msgAuthor; color: msgAvatarColor; font.pixelSize: 15; font.weight: Font.DemiBold }
                Text { anchors.baseline: parent.baseline; text: msgTimestamp; color: "#4E5058"; font.pixelSize: 11 }
            }
            Text { width: parent.width; text: msgContent; color: "#DBDEE1"; font.pixelSize: 15; wrapMode: Text.WordWrap; lineHeight: 1.3 }
        }
    }

    // Grouped message (no avatar, indented)
    Text {
        id: groupedText
        visible: isGrouped
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: 68; anchors.rightMargin: 16; anchors.top: parent.top
        anchors.topMargin: 1
        text: msgContent; color: "#DBDEE1"; font.pixelSize: 15; wrapMode: Text.WordWrap; lineHeight: 1.3
    }

    // Hover timestamp for grouped
    Text {
        visible: isGrouped && msgHover.containsMouse
        anchors.left: parent.left; anchors.leftMargin: 16; anchors.verticalCenter: parent.verticalCenter
        text: msgTimestamp; color: "#4E5058"; font.pixelSize: 10; width: 48; horizontalAlignment: Text.AlignRight
    }

    MouseArea { id: msgHover; anchors.fill: parent; hoverEnabled: true }
}