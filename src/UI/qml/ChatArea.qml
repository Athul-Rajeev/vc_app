import QtQuick
import QtQuick.Controls

Rectangle {
    id: chatArea
    property alias msgList: msgList
    width: parent ? parent.width - 72 - 240 : 600
    height: parent.height
    color: "#313338"

    Column {
        anchors.fill: parent
        spacing: 0

        // Channel header
        Rectangle {
            id: channelHeader
            width: parent.width
            height: 48
            color: "#313338"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#3F4147" }
            Row {
                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; spacing: 8
                Text { anchors.verticalCenter: parent.verticalCenter; text: "#"; color: "#80848E"; font.pixelSize: 22; font.weight: Font.Black }
                Text { anchors.verticalCenter: parent.verticalCenter; text: backend.channelNameForId(backend.selectedTextChannelId); color: "#F2F3F5"; font.pixelSize: 15; font.weight: Font.DemiBold }
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 1; height: 20; color: "#3F4147" }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "Text chat channel"; color: "#80848E"; font.pixelSize: 14 }
            }
        }

        // Messages list
        ListView {
            id: msgList
            width: parent.width
            height: parent.height - channelHeader.height - inputBar.height
            clip: true
            model: backend.chatMessages
            spacing: 0
            cacheBuffer: 800
            property int timeGapThresholdMs: 5 * 60 * 1000

            ScrollBar.vertical: ScrollBar {
                width: 8
                policy: ScrollBar.AsNeeded
                contentItem: Rectangle { radius: 4; color: "#1A1B1E"; opacity: parent.active ? 1.0 : 0.6 }
                background: Rectangle { color: "transparent" }
            }

            header: Item {
                width: msgList.width; height: welcomeCol.height + 32
                Column {
                    id: welcomeCol
                    anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 16; anchors.rightMargin: 16; anchors.top: parent.top; anchors.topMargin: 16
                    spacing: 8
                    Rectangle { width: 68; height: 68; radius: 34; color: "#80848E"; Text { anchors.centerIn: parent; text: "#"; color: "white"; font.pixelSize: 36; font.weight: Font.Black } }
                    Text { text: "Welcome to #" + backend.channelNameForId(backend.selectedTextChannelId) + "!"; color: "#F2F3F5"; font.pixelSize: 28; font.weight: Font.Bold }
                    Text { text: "This is the start of the #" + backend.channelNameForId(backend.selectedTextChannelId) + " channel."; color: "#80848E"; font.pixelSize: 15; wrapMode: Text.WordWrap; width: parent.width }
                    Item { width: 1; height: 8 }
                    Rectangle { width: parent.width; height: 1; color: "#3F4147" }
                    Item { width: 1; height: 4 }
                }
            }

            delegate: Item {
                id: container
                width: ListView.view.width
                height: timeDivider.visible ? timeDivider.height + msgDelegate.height : msgDelegate.height

                property var prevMsg: index > 0 ? backend.chatMessages[index - 1] : null
                property int timeDiff: (prevMsg && prevMsg.timestampMs && modelData.timestampMs)
                                       ? modelData.timestampMs - prevMsg.timestampMs : 0
                property bool showDivider: timeDiff > msgList.timeGapThresholdMs

                Rectangle {
                    id: timeDivider
                    visible: container.showDivider
                    width: parent.width; height: visible ? 28 : 0; color: "transparent"
                    Component.onCompleted: {
                    console.log("msg", index, modelData.timestampMs, modelData.timestamp,
                                "prevTs", prevMsg ? prevMsg.timestampMs : "none",
                                "diff", timeDiff, "showDivider", showDivider)
                    }
                    Rectangle { anchors.verticalCenter: parent.verticalCenter; width: parent.width - 32; height: 1; color: "#3F4147"; anchors.horizontalCenter: parent.horizontalCenter }
                    Rectangle { anchors.centerIn: parent; width: timestampText.width + 16; height: 20; radius: 10; color: "#3F4147"
                        Text { id: timestampText; anchors.centerIn: parent; text: modelData.timestamp; color: "#B5BAC1"; font.pixelSize: 11 }
                    }
                }

                MessageItem {
                    id: msgDelegate
                    anchors.top: timeDivider.visible ? timeDivider.bottom : parent.top
                    msgAuthor: modelData.author
                    msgContent: modelData.content
                    msgTimestamp: modelData.timestamp
                    msgAvatarColor: modelData.avatarColor
                    msgAvatarLetter: modelData.avatarLetter
                    msgListWidth: msgList.width
                    isGrouped: !container.showDivider && index > 0 && prevMsg && prevMsg.author === modelData.author
                }
            }
        }

        // Input bar
        Rectangle {
            id: inputBar
            width: parent.width; height: 68; color: "#313338"
            Rectangle {
                anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16; anchors.topMargin: 0; anchors.bottomMargin: 24
                color: "#383A40"; radius: 8
                Row {
                    anchors.fill: parent; spacing: 0
                    Item {
                        width: 44; height: parent.height
                        Text { anchors.centerIn: parent; text: "\uFF0B"; font.pixelSize: 20; color: attachHover.containsMouse ? "#DBDEE1" : "#80848E" }
                        MouseArea { id: attachHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
                    }
                    TextInput {
                        id: chatInput
                        width: parent.width - 44 - 44; height: parent.height; verticalAlignment: TextInput.AlignVCenter
                        color: "#DBDEE1"; font.pixelSize: 15; selectionColor: "#5865F2"; clip: true
                        Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: "Message #" + backend.channelNameForId(backend.selectedTextChannelId); color: "#6D6F78"; font.pixelSize: 15; visible: chatInput.text.length === 0 && !chatInput.activeFocus }
                        Keys.onReturnPressed: function(event) {
                            if (text.trim().length > 0) { backend.sendMessage(text); text = "" }
                            event.accepted = true
                        }
                    }
                    Item {
                        width: 44; height: parent.height
                        Text { anchors.centerIn: parent; text: "\uD83D\uDE0A"; font.pixelSize: 20; opacity: emojiHover.containsMouse ? 1.0 : 0.7 }
                        MouseArea {
                            id: emojiHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                            onClicked: { emojiPicker.visible = !emojiPicker.visible; if (emojiPicker.visible) emojiPicker.connectInput(chatInput) }
                        }
                    }
                }
            }
        }
    }
}