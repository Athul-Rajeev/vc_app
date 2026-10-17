import QtQuick
import QtQuick.Controls

/* Discord-style channel sidebar. */
Rectangle {
    id: sidebar
    width: 240
    height: parent.height
    color: "#2B2D31"

    property bool textCollapsed:  false
    property bool voiceCollapsed: false

    // ── Server header ─────────────────────────────────────────
    Rectangle {
        id: serverHeader
        width: parent.width
        height: 48
        color: serverHeaderMa.containsMouse ? "#35373C" : "transparent"
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#1E1F22" }

        Row {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 8
            spacing: 4

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "My Server"
                color: "#F2F3F5"
                font.pixelSize: 15
                font.weight: Font.Bold
                width: parent.width - 20
                elide: Text.ElideRight
            }
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron"; size: 16; color: "#B5BAC1"
            }
        }

        MouseArea { id: serverHeaderMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
    }

    // ── Channel list ──────────────────────────────────────────
    ScrollView {
        id: channelList
        anchors {
            top: serverHeader.bottom
            left: parent.left
            right: parent.right
            bottom: userPanel.top
        }
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical.policy: ScrollBar.AsNeeded

        Column {
            width: channelList.width
            spacing: 0

            Item { width: 1; height: 16 }

            // ── Text channels ──
            SectionHeader {
                width: parent.width
                label: "TEXT CHANNELS"
                collapsed: sidebar.textCollapsed
                onToggleClicked: sidebar.textCollapsed = !sidebar.textCollapsed
                onAddClicked: addChannelDialog.open("text")
            }

            Repeater {
                model: backend.textChannels
                delegate: ChannelRow {
                    width: parent.width
                    visible: !sidebar.textCollapsed
                    channelId:   modelData.id
                    channelName: modelData.name
                    isVoice:     false
                    isSelected:  backend.selectedTextChannelId === modelData.id
                }
            }

            Item { width: 1; height: 16 }

            // ── Voice channels ──
            SectionHeader {
                width: parent.width
                label: "VOICE CHANNELS"
                collapsed: sidebar.voiceCollapsed
                onToggleClicked: sidebar.voiceCollapsed = !sidebar.voiceCollapsed
                onAddClicked: addChannelDialog.open("voice")
            }

            Repeater {
                model: backend.voiceChannels
                delegate: Column {
                    width: parent.width
                    spacing: 0
                    visible: !sidebar.voiceCollapsed

                    ChannelRow {
                        width: parent.width
                        channelId:   modelData.id
                        channelName: modelData.name
                        isVoice:     true
                        isSelected:  backend.activeVoiceChannelId === modelData.id
                        isActive:    backend.activeVoiceChannelId === modelData.id
                    }

                    Repeater {
                        model: modelData.peers || []
                        delegate: VoicePeerRow {
                            width: parent.width
                            peerUsername: modelData.username
                            peerMuted:    modelData.isMuted
                            peerDeafened: modelData.isDeafened
                            peerSpeaking: modelData.isSpeaking
                            peerColor:    modelData.avatarColor
                            peerLetter:   modelData.avatarLetter
                        }
                    }

                    // Disconnect button (only while connected to this channel)
                    Item {
                        width: parent.width
                        height: visible ? 40 : 0
                        visible: backend.activeVoiceChannelId === modelData.id

                        Rectangle {
                            anchors { fill: parent; leftMargin: 32; rightMargin: 8; topMargin: 2; bottomMargin: 8 }
                            radius: 4
                            color: discMa.containsMouse ? "#DA373C" : "transparent"
                            border.color: "#DA373C"
                            border.width: 1

                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    name: "disconnect"; size: 16
                                    color: discMa.containsMouse ? "#FFFFFF" : "#DA373C"
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Disconnect"
                                    color: discMa.containsMouse ? "#FFFFFF" : "#DA373C"
                                    font.pixelSize: 13; font.weight: Font.Medium
                                }
                            }

                            MouseArea {
                                id: discMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: backend.leaveVoiceChannel()
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 8 }
        }
    }

    // ── User panel ────────────────────────────────────────────
    Rectangle {
        id: userPanel
        width: parent.width
        height: 52
        color: "#232428"
        anchors.bottom: parent.bottom

        Row {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 4
            spacing: 4

            Rectangle {
                id: userInfo
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 100
                height: 42
                radius: 4
                color: userInfoMa.containsMouse ? "#35373C" : "transparent"

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    spacing: 8

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32; height: 32; radius: 16
                        color: backend.isLoggedIn ? backend.avatarColor(backend.username) : "#5865F2"

                        Text {
                            anchors.centerIn: parent
                            text: backend.isLoggedIn && backend.username.length > 0 ? backend.username[0].toUpperCase() : "?"
                            color: "white"; font.pixelSize: 14; font.weight: Font.Bold
                        }

                        Rectangle {
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: -1; anchors.bottomMargin: -1
                            width: 12; height: 12; radius: 6; color: "#232428"
                            Rectangle {
                                anchors.centerIn: parent; width: 8; height: 8; radius: 4
                                color: backend.isMuted ? "#ED4245" : (backend.isLoggedIn ? "#23A55A" : "#80848E")
                            }
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 32 - 8
                        spacing: 1

                        Text {
                            width: parent.width
                            text: backend.isLoggedIn ? backend.username : "Logging in\u2026"
                            color: "#F2F3F5"; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: backend.activeVoiceChannelId >= 0
                                  ? backend.channelNameForId(backend.activeVoiceChannelId)
                                  : "Voice Disconnected"
                            color: backend.activeVoiceChannelId >= 0 ? "#23A55A" : "#B5BAC1"
                            font.pixelSize: 11; elide: Text.ElideRight
                        }
                    }
                }

                MouseArea { id: userInfoMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0
                PanelButton { iconName: backend.isMuted ? "mic-off" : "mic"; btnTooltip: backend.isMuted ? "Unmute" : "Mute"; isActive: backend.isMuted; onClicked: backend.toggleMute() }
                PanelButton { iconName: backend.isDeafened ? "headphone-off" : "headphone"; btnTooltip: backend.isDeafened ? "Undeafen" : "Deafen"; isActive: backend.isDeafened; onClicked: backend.toggleDeafen() }
                PanelButton { iconName: "settings"; btnTooltip: "User Settings"; onClicked: {} }
            }
        }
    }
}
