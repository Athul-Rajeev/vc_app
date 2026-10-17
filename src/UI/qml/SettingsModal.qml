import QtQuick
import QtQuick.Controls

/* User settings modal: account info, audio preferences, and log out. */
Rectangle {
    id: settingsModal
    visible: false; z: 250

    function open() {
        settingsModal.visible = true
    }

    function close() {
        settingsModal.visible = false
    }

    anchors.fill: parent; color: "#00000080"

    MouseArea { anchors.fill: parent; z: -1; onClicked: settingsModal.close() }

    Rectangle {
        anchors.centerIn: parent; width: 440; height: settingsCard.implicitHeight + 48; radius: 4; color: "#313338"
        MouseArea { anchors.fill: parent }   // Keep clicks inside the card

        Column {
            id: settingsCard; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 24; spacing: 0

            // ── Header ──
            Item {
                width: parent.width; height: 28
                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    text: "User Settings"; color: "#F2F3F5"; font.pixelSize: 20; font.weight: Font.Bold }
                Text { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    text: "\u2715"; color: "#B5BAC1"; font.pixelSize: 18
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: settingsModal.close() } }
            }
            Item { height: 20; width: 1 }

            // ── Account section ──
            Text { text: "ACCOUNT"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5 }
            Item { height: 10; width: 1 }
            Item {
                width: parent.width; height: 44
                Row {
                    anchors.fill: parent; spacing: 10

                    Rectangle {
                        width: 44; height: 44; radius: 22
                        color: backend.isLoggedIn ? backend.avatarColor(backend.username) : "#5865F2"
                        Text { anchors.centerIn: parent
                            text: backend.username.length > 0 ? backend.username[0].toUpperCase() : "?"
                            color: "white"; font.pixelSize: 18; font.weight: Font.Bold }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        Text { text: backend.username.length > 0 ? backend.username : "\u2026"; color: "#F2F3F5"; font.pixelSize: 15; font.weight: Font.DemiBold }
                        Text { text: "Logged in"; color: "#23A55A"; font.pixelSize: 12 }
                    }
                }
            }
            Item { height: 20; width: 1 }
            Rectangle { width: parent.width; height: 1; color: "#3F4147" }
            Item { height: 20; width: 1 }

            // ── Audio section ──
            Text { text: "VOICE & AUDIO"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5 }
            Item { height: 12; width: 1 }
            Text { text: "Input sensitivity"; color: "#DBDEE1"; font.pixelSize: 14 }
            Text { text: "How loud you need to speak before your mic starts transmitting."; color: "#80848E"; font.pixelSize: 12; wrapMode: Text.WordWrap; width: parent.width }
            Item { height: 8; width: 1 }
            Row {
                width: parent.width; spacing: 12
                Text { anchors.verticalCenter: parent.verticalCenter; text: "Quiet"; color: "#80848E"; font.pixelSize: 11 }
                Slider {
                    id: sensitivitySlider
                    width: parent.width - 120
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0; to: 100; stepSize: 1
                    value: backend.voiceActivationSensitivity
                    onMoved: backend.setVoiceActivationSensitivity(Math.round(sensitivitySlider.value))
                }
                Text { anchors.verticalCenter: parent.verticalCenter; text: "Loud"; color: "#80848E"; font.pixelSize: 11 }
            }
            Item { height: 20; width: 1 }
            Rectangle { width: parent.width; height: 1; color: "#3F4147" }
            Item { height: 20; width: 1 }

            // ── Log out ──
            Rectangle {
                width: parent.width; height: 42; radius: 4
                color: logoutMa.containsMouse ? "#A1282C" : "#DA373C"
                Row {
                    anchors.centerIn: parent; spacing: 8
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "disconnect"; size: 16; color: "white" }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "Log Out"; color: "white"; font.pixelSize: 14; font.weight: Font.Medium }
                }
                MouseArea {
                    id: logoutMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        backend.leaveVoiceChannel()
                        backend.logout()
                        settingsModal.close()
                    }
                }
            }
            Item { height: 4; width: 1 }
        }
    }

    // Escape closes the modal; QML Shortcut needs a scoped item, so bind to the dim layer.
    Keys.onEscapePressed: settingsModal.close()
    focus: visible
}
