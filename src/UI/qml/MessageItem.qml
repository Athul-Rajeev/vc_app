import QtQuick

/* One chat row: an optional day/time divider + either a full message
   (avatar row) or a compact grouped continuation. Grouping/divider decisions
   come precomputed from the ChatMessageModel (ShowDividerRole/IsGroupedRole);
   the QML side stays a dumb renderer. */
Item {
    id: root
    property string msgAuthor: ""
    property string msgContent: ""
    property string msgTimestamp: ""
    property string msgAvatarColor: "#5865F2"
    property string msgAvatarLetter: "?"
    property bool isGrouped: false
    property bool showTimeDivider: false
    property int msgListWidth: 600

    readonly property int fullContentHeight: fullRow.height + 4   // compact vertical padding
    readonly property int groupedContentHeight: groupedText.height + 2
    readonly property int timeDividerHeight: showTimeDivider ? 28 : 0

    height: timeDividerHeight + (isGrouped ? groupedContentHeight : fullContentHeight)

    // Hover background
    Rectangle {
        anchors.fill: parent
        color: msgHover.containsMouse ? "#2E3035" : "transparent"
        Behavior on color { ColorAnimation { duration: 50 } }
    }

    // Day/time divider (pill between hairline rules)
    Item {
        id: timeDivider
        visible: root.showTimeDivider
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: root.timeDividerHeight
        Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width - 32; height: 1; color: "#3F4147"; anchors.horizontalCenter: parent.horizontalCenter }
        Rectangle { anchors.centerIn: parent; width: timeDividerText.width + 16; height: 20; radius: 10; color: "#3F4147"
            Text { id: timeDividerText; anchors.centerIn: parent; text: root.msgTimestamp; color: "#B5BAC1"; font.pixelSize: 11 }
        }
    }

    // Full message (with avatar)
    Row {
        id: fullRow
        visible: !isGrouped
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: showTimeDivider ? timeDivider.bottom : parent.top
        anchors.leftMargin: 16; anchors.rightMargin: 16
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
        anchors.leftMargin: 68; anchors.rightMargin: 16
        anchors.top: showTimeDivider ? timeDivider.bottom : parent.top
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
