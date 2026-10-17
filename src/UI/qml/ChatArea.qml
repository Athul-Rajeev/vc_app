import QtQuick
import QtQuick.Controls

Rectangle {
    id: chatArea
    property alias msgList: msgList
    width: parent ? parent.width - 72 - 240 : 600
    height: parent.height
    color: "#313338"

    // Scroll-up paging anchor: while a page loads we remember which row was the
    // first visible one; when the page lands we re-view that row at the same
    // screen position, so the content below the fold does not move a pixel.
    property int pendingAnchorIndex: -1
    property int pendingAnchorRowCount: 0

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
            model: backend.chatMessageModel
            spacing: 0
            cacheBuffer: 800

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
                    Text { text: "This is the start of the #" + backend.channelNameForId(backend.selectedTextChannelId) + " channel." + (backend.hasMoreHistory ? " Scroll up to load older messages." : ""); color: "#80848E"; font.pixelSize: 15; wrapMode: Text.WordWrap; width: parent.width }
                    Item { width: 1; height: 8 }
                    Rectangle { width: parent.width; height: 1; color: "#3F4147" }
                    Item { width: 1; height: 4 }
                }
            }

            delegate: MessageItem {
                width: msgList.width
                msgAuthor: model.author
                msgContent: model.content
                msgTimestamp: model.timestamp
                msgAvatarColor: model.avatarColor
                msgAvatarLetter: model.avatarLetter
                msgListWidth: msgList.width
                isGrouped: model.isGrouped
                showTimeDivider: model.showDivider
            }

            onContentYChanged: {
                // Reached (or pinned at) the top with older history available →
                // request the next page. requestOlderMessages() is a no-op while
                // a page is already in flight or the channel page isn't current.
                if (msgList.count > 0
                    && msgList.contentY <= 1
                    && backend.hasMoreHistory
                    && !backend.historyLoading)
                {
                    backend.requestOlderMessages()
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

    // Floating "loading older messages" pill. Deliberately an overlay (never a
    // list header): resizing the header on load completion would shift every
    // row and spoil the scroll-anchor trick above.
    Rectangle {
        id: historySpinnerPill
        visible: backend.historyLoading
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: chatArea.top; anchors.topMargin: channelHeader.height + 10
        z: 20; width: historySpinnerRow.width + 28; height: 30; radius: 15
        color: "#111214CC"; border.color: "#3F4147"; border.width: 1

        Row {
            id: historySpinnerRow
            anchors.centerIn: parent; spacing: 8
            Item {
                width: 16; height: 16; anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    anchors.fill: parent; radius: 8; color: "transparent"
                    border.color: "#5865F2"; border.width: 2
                }
                NumberAnimation on rotation { from: 0; to: 360; duration: 900; loops: Animation.Infinite }
                Rectangle { width: 3; height: 3; radius: 1.5; anchors.centerIn: parent; color: "#F2F3F5" }
            }
            Text { anchors.verticalCenter: parent.verticalCenter; text: "Loading older messages…"; color: "#B5BAC1"; font.pixelSize: 12 }
        }
    }

    Connections {
        target: backend
        function onHistoryLoadingChanged() {
            if (backend.historyLoading)
            {
                // Page request going out: remember the first visible row.
                chatArea.pendingAnchorIndex = msgList.indexAt(0, msgList.contentY + 2)
                chatArea.pendingAnchorRowCount = msgList.count
            }
            else
            {
                // Page landed (or was discarded). Reposition so the row that was
                // first-visible sits exactly where it was — seamless prepend.
                if (chatArea.pendingAnchorIndex >= 0)
                {
                    let addedRows = msgList.count - chatArea.pendingAnchorRowCount
                    if (addedRows > 0)
                        msgList.positionViewAtIndex(chatArea.pendingAnchorIndex + addedRows, ListView.Beginning)
                    chatArea.pendingAnchorIndex = -1
                    chatArea.pendingAnchorRowCount = 0
                }
            }
        }
        // Late message arrivals while a page is in flight shift the anchor;
        // account for them so the reposition offset stays correct.
        function onRequestScrollToBottom() {
            if (chatArea.pendingAnchorIndex >= 0 && backend.historyLoading)
                chatArea.pendingAnchorRowCount += 1
        }
        // Switching channels resets everything about the anchor.
        function onSelectedTextChannelIdChanged() {
            chatArea.pendingAnchorIndex = -1
            chatArea.pendingAnchorRowCount = 0
        }
    }
}
