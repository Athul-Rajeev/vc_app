import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Basic

ApplicationWindow {
    id: root
    visible: true
    width: 1100
    height: 720
    minimumWidth: 800
    minimumHeight: 500
    title: "VoiceChatApp"
    color: "#313338"

    // ── Fonts ──────────────────────────────────────────────────
    FontLoader { id: fontNormal; source: "qrc:/fonts/gg-sans-normal.ttf" }

    // ── Global connection to backend scroll signal ─────────────
    Connections {
        target: backend
        function onRequestScrollToBottom() {
            Qt.callLater(function() { msgList.positionViewAtEnd() })
        }
        function onIsLoggedInChanged() {
            if (backend.isLoggedIn) loginOverlay.opacity = 0
        }
    }

    // ════════════════════════════════════════════════════════════
    // Root layout: [server strip] [sidebar] [chat area]
    // ════════════════════════════════════════════════════════════
    Row {
        anchors.fill: parent
        spacing: 0

        // ── Server Strip ───────────────────────────────────────
        Rectangle {
            id: serverStrip
            width: 72
            height: parent.height
            color: "#1E1F22"

            Column {
                id: serverStripCol
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 12
                spacing: 8

                // Home button
                Item {
                    width: 48; height: 48
                    anchors.horizontalCenter: parent.horizontalCenter

                    Rectangle {
                        id: homePill
                        anchors.left: parent.left
                        anchors.leftMargin: -4
                        width: 4
                        height: homeSelected ? 40 : (homeHover.containsMouse ? 20 : 0)
                        anchors.verticalCenter: parent.verticalCenter
                        color: "white"
                        radius: 2
                        Behavior on height { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                        property bool homeSelected: false
                    }

                    Rectangle {
                        id: homeBtn
                        anchors.centerIn: parent
                        width: 48; height: 48
                        radius: homeHover.containsMouse ? 16 : 24
                        color: homeHover.containsMouse ? "#5865F2" : "#5865F2"
                        Behavior on radius { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }

                        Text {
                            anchors.centerIn: parent
                            text: "💬"
                            font.pixelSize: 22
                        }
                        MouseArea {
                            id: homeHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }

                // Separator
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 32; height: 2
                    color: "#3F4147"
                    radius: 1
                }

                // Server icon
                Item {
                    width: 48; height: 48
                    anchors.horizontalCenter: parent.horizontalCenter

                    Rectangle {
                        id: serverPill
                        anchors.left: parent.left
                        anchors.leftMargin: -4
                        width: 4
                        height: 40
                        anchors.verticalCenter: parent.verticalCenter
                        color: "white"
                        radius: 2
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 48; height: 48
                        radius: serverHover.containsMouse ? 16 : 12
                        color: serverHover.containsMouse ? "#5865F2" : "#36393F"
                        border.color: "#5865F2"
                        border.width: serverPill.height > 0 ? 2 : 0
                        Behavior on radius { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }

                        Text {
                            anchors.centerIn: parent
                            text: "VC"
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            color: "white"
                        }
                        MouseArea {
                            id: serverHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }
            }
        }

        // ── Sidebar ────────────────────────────────────────────
        Rectangle {
            id: sidebar
            width: 240
            height: parent.height
            color: "#2B2D31"

            Column {
                anchors.fill: parent
                spacing: 0

                // Server name header
                Rectangle {
                    width: parent.width
                    height: 48
                    color: "#2B2D31"

                    // Bottom border
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: "#1E1F22"
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 12
                        spacing: 0

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "My Server"
                            color: "#F2F3F5"
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            width: parent.width - 28
                            elide: Text.ElideRight
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "✕"
                            color: "#B5BAC1"
                            font.pixelSize: 14
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onContainsMouseChanged: parent.color = containsMouse ? "#32353B" : "#2B2D31"
                    }
                }

                // Channel scroll area
                ScrollView {
                    id: channelScroll
                    width: parent.width
                    height: parent.height - 48 - 52
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    // Custom scrollbar style
                    ScrollBar.vertical: ScrollBar {
                        width: 4
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle {
                            radius: 2
                            color: "#1A1B1E"
                            opacity: parent.active ? 1.0 : 0.7
                        }
                        background: Rectangle { color: "transparent" }
                    }

                    Column {
                        width: channelScroll.width
                        spacing: 0
                        topPadding: 8
                        bottomPadding: 8

                        // ── TEXT CHANNELS section ──────────────
                        SectionHeader {
                            label: "TEXT CHANNELS"
                            onAddClicked: addChannelDialog.open("text")
                        }

                        Repeater {
                            model: backend.textChannels
                            delegate: ChannelRow {
                                required property var modelData
                                channelId:   modelData.id
                                channelName: modelData.name
                                icon:        "#"
                                isSelected:  backend.selectedTextChannelId === modelData.id
                                onClicked:   backend.selectTextChannel(modelData.id)
                            }
                        }

                        // ── VOICE CHANNELS section ─────────────
                        Item { width: 1; height: 8 }

                        SectionHeader {
                            label: "VOICE CHANNELS"
                            onAddClicked: addChannelDialog.open("voice")
                        }

                        Repeater {
                            model: backend.voiceChannels
                            delegate: Column {
                                required property var modelData
                                width: channelScroll.width
                                spacing: 0

                                ChannelRow {
                                    channelId:   modelData.id
                                    channelName: modelData.name
                                    icon:        "🔊"
                                    isVoice:     true
                                    isSelected:  backend.activeVoiceChannelId === modelData.id
                                    isActive:    modelData.isActive
                                    onClicked:   backend.joinVoiceChannel(modelData.id)
                                }

                                // Peers list
                                Repeater {
                                    model: modelData.peers
                                    delegate: VoicePeerRow {
                                        required property var modelData
                                        peerUsername:  modelData.username
                                        peerMuted:     modelData.isMuted
                                        peerDeafened:  modelData.isDeafened
                                        peerSpeaking:  modelData.isSpeaking
                                        peerColor:     modelData.avatarColor
                                        peerLetter:    modelData.avatarLetter
                                    }
                                }

                                // Leave VC button
                                Rectangle {
                                    visible: modelData.isActive
                                    width: parent.width
                                    height: visible ? 52 : 0
                                    color: "transparent"

                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        anchors.topMargin: 4
                                        color: leaveHover.containsMouse ? "#A12D2F" : "transparent"
                                        border.color: "#ED4245"
                                        border.width: 1
                                        radius: 4

                                        Row {
                                            anchors.centerIn: parent
                                            spacing: 6
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: "📵"
                                                font.pixelSize: 14
                                            }
                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: "Disconnect"
                                                color: "#ED4245"
                                                font.pixelSize: 13
                                                font.weight: Font.Medium
                                            }
                                        }

                                        MouseArea {
                                            id: leaveHover
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: backend.leaveVoiceChannel()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ── User Panel ─────────────────────────────────
                Rectangle {
                    id: userPanel
                    width: parent.width
                    height: 52
                    color: "#232428"

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        spacing: 8

                        // Avatar
                        Rectangle {
                            id: userAvatar
                            width: 32; height: 32
                            radius: 16
                            color: backend.isLoggedIn
                                   ? backend.avatarColor(backend.username)
                                   : "#5865F2"
                            anchors.verticalCenter: parent.verticalCenter

                            Text {
                                anchors.centerIn: parent
                                text: backend.isLoggedIn && backend.username.length > 0
                                      ? backend.username[0].toUpperCase()
                                      : "?"
                                color: "white"
                                font.pixelSize: 14
                                font.weight: Font.Bold
                            }

                            // Online indicator dot
                            Rectangle {
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.rightMargin: -1
                                anchors.bottomMargin: -1
                                width: 12; height: 12
                                radius: 6
                                color: "#232428"

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 8; height: 8
                                    radius: 4
                                    color: backend.isMuted ? "#ED4245"
                                         : backend.isLoggedIn ? "#23A55A" : "#80848E"
                                    Behavior on color { ColorAnimation { duration: 200 } }
                                }
                            }
                        }

                        // Name + status
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 32 - 8 - 72
                            spacing: 1

                            Text {
                                width: parent.width
                                text: backend.isLoggedIn ? backend.username : "Logging in…"
                                color: "#F2F3F5"
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width
                                text: backend.activeVoiceChannelId >= 0
                                      ? backend.channelNameForId(backend.activeVoiceChannelId)
                                      : "Voice Disconnected"
                                color: backend.activeVoiceChannelId >= 0 ? "#23A55A" : "#80848E"
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                Behavior on color { ColorAnimation { duration: 200 } }
                            }
                        }

                        // Control buttons
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 0

                            // Mute
                            PanelButton {
                                btnIcon:    backend.isMuted ? "🔇" : "🎤"
                                btnTooltip: backend.isMuted ? "Unmute" : "Mute"
                                isActive:   backend.isMuted
                                onClicked:  backend.toggleMute()
                            }

                            // Deafen
                            PanelButton {
                                btnIcon:    backend.isDeafened ? "🔕" : "🎧"
                                btnTooltip: backend.isDeafened ? "Undeafen" : "Deafen"
                                isActive:   backend.isDeafened
                                onClicked:  backend.toggleDeafen()
                            }

                            // Settings
                            PanelButton {
                                btnIcon:    "⚙️"
                                btnTooltip: "User Settings"
                                onClicked:  {}
                            }
                        }
                    }
                }
            }
        }

        // ── Chat Area ──────────────────────────────────────────
        Rectangle {
            id: chatArea
            width: parent.width - serverStrip.width - sidebar.width
            height: parent.height
            color: "#313338"

            Column {
                anchors.fill: parent
                spacing: 0

                // Channel header bar
                Rectangle {
                    id: channelHeader
                    width: parent.width
                    height: 48
                    color: "#313338"

                    // Bottom border
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: "#3F4147"
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 8

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "#"
                            color: "#80848E"
                            font.pixelSize: 22
                            font.weight: Font.Black
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: backend.channelNameForId(backend.selectedTextChannelId)
                            color: "#F2F3F5"
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 1; height: 20
                            color: "#3F4147"
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Text chat channel"
                            color: "#80848E"
                            font.pixelSize: 14
                        }
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

                    // Custom scrollbar
                    ScrollBar.vertical: ScrollBar {
                        width: 8
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle {
                            radius: 4
                            color: "#1A1B1E"
                            opacity: parent.active ? 1.0 : 0.6
                        }
                        background: Rectangle { color: "transparent" }
                    }

                    // Welcome header at top
                    header: Item {
                        width: msgList.width
                        height: welcomeCol.height + 32

                        Column {
                            id: welcomeCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            anchors.top: parent.top
                            anchors.topMargin: 16
                            spacing: 8

                            Rectangle {
                                width: 68; height: 68
                                radius: 34
                                color: "#80848E"
                                Text {
                                    anchors.centerIn: parent
                                    text: "#"
                                    color: "white"
                                    font.pixelSize: 36
                                    font.weight: Font.Black
                                }
                            }

                            Text {
                                text: "Welcome to #" + backend.channelNameForId(backend.selectedTextChannelId) + "!"
                                color: "#F2F3F5"
                                font.pixelSize: 28
                                font.weight: Font.Bold
                            }

                            Text {
                                text: "This is the start of the #"
                                      + backend.channelNameForId(backend.selectedTextChannelId)
                                      + " channel."
                                color: "#80848E"
                                font.pixelSize: 15
                                wrapMode: Text.WordWrap
                                width: parent.width
                            }

                            Item { width: 1; height: 8 }

                            Rectangle {
                                width: parent.width
                                height: 1
                                color: "#3F4147"
                            }
                            Item { width: 1; height: 4 }
                        }
                    }

                    delegate: MessageItem {
                        required property var modelData
                        required property int index

                        msgAuthor:       modelData.author
                        msgContent:      modelData.content
                        msgTimestamp:    modelData.timestamp
                        msgAvatarColor:  modelData.avatarColor
                        msgAvatarLetter: modelData.avatarLetter
                        msgListWidth:    msgList.width

                        // Group: don't repeat avatar/name if same author
                        isGrouped: index > 0
                                   && backend.chatMessages[index - 1] !== undefined
                                   && backend.chatMessages[index - 1].author === modelData.author
                    }
                }

                // Input bar
                Rectangle {
                    id: inputBar
                    width: parent.width
                    height: 68
                    color: "#313338"

                    Rectangle {
                        id: inputBox
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        anchors.topMargin: 0
                        anchors.bottomMargin: 24
                        color: "#383A40"
                        radius: 8

                        Row {
                            anchors.fill: parent
                            spacing: 0

                            // Attach button
                            Item {
                                width: 44; height: parent.height
                                Text {
                                    anchors.centerIn: parent
                                    text: "＋"
                                    font.pixelSize: 20
                                    color: attachHover.containsMouse ? "#DBDEE1" : "#80848E"
                                    Behavior on color { ColorAnimation { duration: 100 } }
                                }
                                MouseArea {
                                    id: attachHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                }
                            }

                            // Text input
                            TextInput {
                                id: chatInput
                                width: parent.width - 44 - 44
                                height: parent.height
                                verticalAlignment: TextInput.AlignVCenter
                                color: "#DBDEE1"
                                font.pixelSize: 15
                                selectionColor: "#5865F2"
                                clip: true

                                // Placeholder
                                Text {
                                    anchors.fill: parent
                                    verticalAlignment: Text.AlignVCenter
                                    text: "Message #" + backend.channelNameForId(backend.selectedTextChannelId)
                                    color: "#6D6F78"
                                    font.pixelSize: 15
                                    visible: chatInput.text.length === 0 && !chatInput.activeFocus
                                }

                                Keys.onReturnPressed: function(event) {
                                    if (text.trim().length > 0) {
                                        backend.sendMessage(text)
                                        text = ""
                                    }
                                    event.accepted = true
                                }
                            }

                            // Emoji button
                            Item {
                                width: 44; height: parent.height
                                Text {
                                    anchors.centerIn: parent
                                    text: "😊"
                                    font.pixelSize: 20
                                    opacity: emojiHover.containsMouse ? 1.0 : 0.7
                                    Behavior on opacity { NumberAnimation { duration: 100 } }
                                }
                                MouseArea {
                                    id: emojiHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: emojiPicker.visible = !emojiPicker.visible
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ════════════════════════════════════════════════════════════
    // Emoji Picker Popup
    // ════════════════════════════════════════════════════════════
    Rectangle {
        id: emojiPicker
        visible: false
        width: 300
        height: 250
        x: root.width - sidebar.width - serverStrip.width - width - 16
        y: root.height - 68 - height - 8
        color: "#2B2D31"
        radius: 8
        z: 100

        // Border
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: "#1E1F22"
            border.width: 1
            radius: 8
        }

        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            Text {
                text: "  Emoji"
                color: "#80848E"
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 0.5
                topPadding: 4
            }

            ScrollView {
                width: parent.width
                height: parent.height - 30
                clip: true
                ScrollBar.vertical.policy: ScrollBar.AsNeeded
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                Flow {
                    width: parent.width - 8
                    spacing: 2
                    leftPadding: 2

                    Repeater {
                        model: [
                            "😀","😁","😂","🤣","😃","😄","😅","😆","😉","😊",
                            "😋","😎","😍","😘","🥰","😗","😙","😚","🙂","🤗",
                            "🤩","🤔","🫡","🤨","😐","😑","😶","😏","😒","🙄",
                            "😬","🤥","😔","😪","🤤","😴","😷","😤","😠","😡",
                            "🤬","😈","👿","💀","☠️","💩","🤡","👹","👺","👻",
                            "👍","👎","❤️","💔","⭐","🔥","⚡","🎵","🚀","🎉",
                            "🐱","🐶","🦁","🐸","🦊","🐺","🦝","🐵","🙈","🦄",
                            "🌈","❄️","☀️","🌙","⛅","🌸","🌺","🌻","🍕","🍔"
                        ]

                        delegate: Rectangle {
                            required property string modelData
                            width: 36; height: 36
                            radius: 4
                            color: emojiItemMa.containsMouse ? "#35373C" : "transparent"
                            Behavior on color { ColorAnimation { duration: 80 } }

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: 20
                            }
                            MouseArea {
                                id: emojiItemMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    let cur = chatInput.text
                                    if (cur.length > 0 && cur[cur.length-1] !== ' ')
                                        cur += ' '
                                    cur += modelData + ' '
                                    chatInput.text = cur
                                    chatInput.cursorPosition = cur.length
                                    chatInput.forceActiveFocus()
                                    emojiPicker.visible = false
                                }
                            }
                        }
                    }
                }
            }
        }

        // Close when clicking outside
        MouseArea {
            anchors.fill: parent
            propagateComposedEvents: true
            onClicked: function(mouse) { mouse.accepted = false }
        }
    }

    // Close emoji picker on outside click
    MouseArea {
        anchors.fill: parent
        z: 99
        visible: emojiPicker.visible
        onClicked: emojiPicker.visible = false
    }

    // ════════════════════════════════════════════════════════════
    // Add Channel Dialog
    // ════════════════════════════════════════════════════════════
    Rectangle {
        id: addChannelDialog
        visible: false
        z: 200

        property string channelType: "text"

        function open(type) {
            channelType = type
            channelNameInput.text = ""
            visible = true
            channelNameInput.forceActiveFocus()
        }

        // --- NEW HELPER FUNCTION ---
        function doCreateChannel() {
            let name = channelNameInput.text.trim()
            if (name.length === 0) return
            if (addChannelDialog.channelType === "text")
                backend.requestNewTextChannel(name)
            else
                backend.requestNewVoiceChannel(name)
            addChannelDialog.visible = false
        }

        anchors.fill: parent
        color: "#00000080"

        // Modal box
        Rectangle {
            anchors.centerIn: parent
            width: 440
            height: 240
            radius: 4
            color: "#313338"

            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 0

                // Header
                Row {
                    width: parent.width
                    height: 40

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: addChannelDialog.channelType === "text"
                              ? "Create Text Channel"
                              : "Create Voice Channel"
                        color: "#F2F3F5"
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        width: parent.width - 24
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "✕"
                        color: "#B5BAC1"
                        font.pixelSize: 18
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: addChannelDialog.visible = false
                        }
                    }
                }

                Item { height: 16; width: 1 }

                Text {
                    text: "CHANNEL NAME"
                    color: "#B5BAC1"
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    font.letterSpacing: 0.5
                }

                Item { height: 8; width: 1 }

                Rectangle {
                    width: parent.width
                    height: 40
                    color: "#1E1F22"
                    radius: 4

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        spacing: 6

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: addChannelDialog.channelType === "text" ? "#" : "🔊"
                            color: "#80848E"
                            font.pixelSize: 16
                        }

                        TextInput {
                            id: channelNameInput
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 40
                            color: "#DBDEE1"
                            font.pixelSize: 15
                            selectionColor: "#5865F2"

                            Text {
                                anchors.fill: parent
                                verticalAlignment: Text.AlignVCenter
                                text: addChannelDialog.channelType === "text"
                                      ? "new-channel" : "New Voice"
                                color: "#6D6F78"
                                font.pixelSize: 15
                                visible: channelNameInput.text.length === 0
                            }

                            // --- CALL THE HELPER FUNCTION HERE ---
                            Keys.onReturnPressed: addChannelDialog.doCreateChannel()
                            Keys.onEscapePressed: addChannelDialog.visible = false
                        }
                    }
                }

                Item { height: 16; width: 1 }

                Row {
                    anchors.right: parent.right
                    spacing: 8

                    Rectangle {
                        width: 96; height: 38
                        radius: 3
                        color: cancelHover.containsMouse ? "#2C2D32" : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "Cancel"
                            color: "#DBDEE1"
                            font.pixelSize: 14
                        }
                        MouseArea {
                            id: cancelHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: addChannelDialog.visible = false
                        }
                    }

                    Rectangle {
                        id: createBtn
                        width: 110; height: 38
                        radius: 3
                        color: createHover.containsMouse ? "#4752C4" : "#5865F2"
                        Behavior on color { ColorAnimation { duration: 100 } }
                        
                        // --- ALIAS REMOVED ---

                        Text {
                            anchors.centerIn: parent
                            text: "Create Channel"
                            color: "white"
                            font.pixelSize: 14
                            font.weight: Font.Medium
                        }
                        MouseArea {
                            id: createHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            
                            // --- CALL THE HELPER FUNCTION HERE ---
                            onClicked: addChannelDialog.doCreateChannel()
                        }
                    }
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            z: -1
            onClicked: addChannelDialog.visible = false
        }
    }

    // ════════════════════════════════════════════════════════════
    // Login Overlay
    // ════════════════════════════════════════════════════════════
    Rectangle {
        id: loginOverlay
        anchors.fill: parent
        color: "#000000B0"
        z: 300
        visible: !backend.isLoggedIn || opacity > 0

        Behavior on opacity {
            NumberAnimation { 
                duration: 300 
                easing.type: Easing.InQuad 
                onRunningChanged: if (!running && loginOverlay.opacity === 0) loginOverlay.visible = false
            }
        }

        Rectangle {
            anchors.centerIn: parent
            width: 480
            height: loginCard.implicitHeight + 48
            radius: 4
            color: "#313338"

            Column {
                id: loginCard
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 32
                spacing: 0

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Welcome back!"
                    color: "#F2F3F5"
                    font.pixelSize: 24
                    font.weight: Font.Bold
                }

                Item { height: 8; width: 1 }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "We're so excited to see you again!"
                    color: "#B5BAC1"
                    font.pixelSize: 16
                }

                Item { height: 20; width: 1 }

                Text {
                    text: "USERNAME"
                    color: "#B5BAC1"
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    font.letterSpacing: 0.5
                }

                Item { height: 8; width: 1 }

                Rectangle {
                    width: parent.width
                    height: 44
                    color: "#1E1F22"
                    radius: 4
                    border.color: loginInput.activeFocus ? "#5865F2" : "transparent"
                    border.width: 1

                    TextInput {
                        id: loginInput
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#DBDEE1"
                        font.pixelSize: 16
                        selectionColor: "#5865F2"
                    }
                }

                Item { height: 20; width: 1 }

                Text {
                    text: "PASSWORD"
                    color: "#B5BAC1"
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    font.letterSpacing: 0.5
                }

                Item { height: 8; width: 1 }

                Rectangle {
                    width: parent.width
                    height: 44
                    color: "#1E1F22"
                    radius: 4
                    border.color: passwordInput.activeFocus ? "#5865F2" : "transparent"
                    border.width: 1

                    TextInput {
                        id: passwordInput
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#DBDEE1"
                        font.pixelSize: 16
                        selectionColor: "#5865F2"
                        echoMode: TextInput.Password
                    }
                }

                Item { height: 20; width: 1 }

                Rectangle {
                    id: loginJoinBtn
                    width: parent.width
                    height: 44
                    radius: 3
                    color: joinBtnHover.containsMouse ? "#4752C4" : "#5865F2"
                    Behavior on color { ColorAnimation { duration: 100 } }

                    function doLogin() {
                        let u = loginInput.text.trim()
                        let p = passwordInput.text.trim()
                        if (u.length > 0 && p.length > 0) backend.login(u, p)
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "Login / Register"
                        color: "white"
                        font.pixelSize: 16
                        font.weight: Font.Medium
                    }

                    MouseArea {
                        id: joinBtnHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: loginJoinBtn.doLogin()
                    }
                }

                Item { height: 4; width: 1 }
            }
        }
    }

    // ════════════════════════════════════════════════════════════
    // Inline component definitions
    // ════════════════════════════════════════════════════════════

    // ── SectionHeader ──────────────────────────────────────────
    component SectionHeader: Item {
        id: sectionRoot
        property alias label: sectionLabel.text
        signal addClicked

        width: channelScroll.width
        height: 32

        Row {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 6
            spacing: 0

            Text {
                id: sectionLabel
                anchors.verticalCenter: parent.verticalCenter
                color: sectionHeaderMa.containsMouse ? "#DBDEE1" : "#80848E"
                font.pixelSize: 11
                font.weight: Font.Bold
                font.letterSpacing: 0.5
                width: parent.width - 22
                elide: Text.ElideRight
                Behavior on color { ColorAnimation { duration: 100 } }
            }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 20; height: 20
                radius: 4
                color: addBtnMa.containsMouse ? "#35373C" : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: addBtnMa.containsMouse ? "#DBDEE1" : "#80848E"
                    font.pixelSize: 18
                    Behavior on color { ColorAnimation { duration: 100 } }
                }
                MouseArea {
                    id: addBtnMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sectionRoot.addClicked()
                }
            }
        }

        MouseArea {
            id: sectionHeaderMa
            anchors.fill: parent
            hoverEnabled: true
            propagateComposedEvents: true
            z: -1
        }
    }

    // ── ChannelRow ─────────────────────────────────────────────
    component ChannelRow: Item {
        id: chRowRoot
        property int     channelId:   -1
        property string  channelName: ""
        property string  icon:        "#"
        property bool    isSelected:  false
        property bool    isVoice:     false
        property bool    isActive:    false
        signal clicked

        width: channelScroll.width
        height: 34

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.topMargin: 1
            anchors.bottomMargin: 1
            radius: 4
            color: chRowRoot.isSelected
                   ? "#404249"
                   : chMa.containsMouse ? "#35373C" : "transparent"
            Behavior on color { ColorAnimation { duration: 80 } }

            Row {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 6

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: chRowRoot.icon
                    font.pixelSize: chRowRoot.isVoice ? 16 : 18
                    color: chRowRoot.isSelected ? "#DBDEE1"
                         : chMa.containsMouse ? "#B5BAC1" : "#80848E"
                    font.weight: chRowRoot.isVoice ? Font.Normal : Font.Black
                    Behavior on color { ColorAnimation { duration: 80 } }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: chRowRoot.channelName
                    color: chRowRoot.isSelected ? "#F2F3F5"
                         : chMa.containsMouse ? "#DBDEE1" : "#80848E"
                    font.pixelSize: 15
                    font.weight: chRowRoot.isSelected ? Font.DemiBold : Font.Normal
                    width: parent.width - 36
                    elide: Text.ElideRight
                    Behavior on color { ColorAnimation { duration: 80 } }
                }
            }
        }

        MouseArea {
            id: chMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chRowRoot.clicked()
        }
    }

    // ── VoicePeerRow ───────────────────────────────────────────
    component VoicePeerRow: Item {
        property string peerUsername: ""
        property bool   peerMuted:    false
        property bool   peerDeafened: false
        property bool   peerSpeaking: false
        property string peerColor:    "#5865F2"
        property string peerLetter:   "?"

        width: channelScroll.width
        height: 34

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.topMargin: 1
            anchors.bottomMargin: 1
            radius: 4
            color: peerMa.containsMouse ? "#35373C" : "transparent"

            Row {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 8
                spacing: 8

                // Mini avatar
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 24; height: 24
                    radius: 12
                    color: peerColor
                    border.color: peerSpeaking ? "#23A55A" : "transparent"
                    border.width: peerSpeaking ? 2 : 0
                    Behavior on border.color { ColorAnimation { duration: 150 } }

                    Text {
                        anchors.centerIn: parent
                        text: peerLetter
                        color: "white"
                        font.pixelSize: 10
                        font.weight: Font.Bold
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: peerUsername
                    color: peerSpeaking ? "#23A55A" : "#B5BAC1"
                    font.pixelSize: 14
                    width: parent.width - 24 - 8 - 36
                    elide: Text.ElideRight
                    Behavior on color { ColorAnimation { duration: 150 } }
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text { text: peerMuted    ? "🔇" : ""; font.pixelSize: 12; visible: peerMuted }
                    Text { text: peerDeafened ? "🎧" : ""; font.pixelSize: 12; visible: peerDeafened }
                }
            }

            MouseArea {
                id: peerMa
                anchors.fill: parent
                hoverEnabled: true
            }
        }
    }

    // ── PanelButton ────────────────────────────────────────────
    component PanelButton: Item {
        property alias btnIcon:    btnTxt.text
        property alias btnTooltip: btnTip.text
        property bool  isActive:   false
        signal clicked

        width: 32; height: 32

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: 4
            color: panelBtnMa.containsMouse ? "#35373C" : "transparent"
            Behavior on color { ColorAnimation { duration: 80 } }

            Text {
                id: btnTxt
                anchors.centerIn: parent
                font.pixelSize: 16
                opacity: isActive ? 1.0 : (panelBtnMa.containsMouse ? 1.0 : 0.75)
            }
        }

        ToolTip {
            id: btnTip
            visible: panelBtnMa.containsMouse
            delay: 500
            background: Rectangle {
                color: "#111214"
                radius: 4
                border.color: "#3F4147"
                border.width: 1
            }
            contentItem: Text {
                text: btnTip.text
                color: "#DBDEE1"
                font.pixelSize: 13
            }
        }

        MouseArea {
            id: panelBtnMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: parent.clicked()
        }
    }

    // ── MessageItem ────────────────────────────────────────────
    component MessageItem: Item {
        property string msgAuthor:       ""
        property string msgContent:      ""
        property string msgTimestamp:    ""
        property string msgAvatarColor:  "#5865F2"
        property string msgAvatarLetter: "?"
        property bool   isGrouped:       false
        property int    msgListWidth:    600

        width: msgListWidth
        height: isGrouped ? groupedHeight : fullHeight

        readonly property int fullHeight:    msgCol.implicitHeight + 16
        readonly property int groupedHeight: msgCol.implicitHeight + 2

        // Hover background
        Rectangle {
            anchors.fill: parent
            color: msgItemMa.containsMouse ? "#2E3035" : "transparent"
            Behavior on color { ColorAnimation { duration: 50 } }
        }

        // Full message (with avatar)
        Row {
            visible: !isGrouped
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            anchors.topMargin: 8
            spacing: 16

            Rectangle {
                id: avatarRect
                width: 40; height: 40
                radius: 20
                color: msgAvatarColor

                Text {
                    anchors.centerIn: parent
                    text: msgAvatarLetter
                    color: "white"
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
            }

            Column {
                id: msgCol
                width: parent.width - 40 - 16
                spacing: 2

                Row {
                    spacing: 8
                    Text {
                        text: msgAuthor
                        color: msgAvatarColor
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 1
                        text: "Today at " + msgTimestamp
                        color: "#4E5058"
                        font.pixelSize: 11
                    }
                }

                Text {
                    width: parent.width
                    text: msgContent
                    color: "#DBDEE1"
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                    lineHeight: 1.375
                }
            }
        }

        // Grouped message (no avatar, indented)
        Row {
            visible: isGrouped
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 72
            anchors.rightMargin: 16
            anchors.topMargin: 1
            spacing: 0

            Text {
                width: parent.width
                text: msgContent
                color: "#DBDEE1"
                font.pixelSize: 15
                wrapMode: Text.WordWrap
                lineHeight: 1.375
            }
        }

        // Hover timestamp for grouped messages
        Text {
            visible: isGrouped && msgItemMa.containsMouse
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: msgTimestamp
            color: "#4E5058"
            font.pixelSize: 10
            width: 48
            horizontalAlignment: Text.AlignRight
        }

        MouseArea {
            id: msgItemMa
            anchors.fill: parent
            hoverEnabled: true
        }
    }
}