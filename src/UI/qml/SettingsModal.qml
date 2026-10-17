import QtQuick
import QtQuick.Controls

/* User settings modal: account info, audio preferences, security, and log out.
   The password fields are plain id'd inputs (no Repeater registration): the
   register-then-null dance broke the Change Password button after the first
   close/reopen of the modal. */
Rectangle {
    id: settingsModal
    visible: false; z: 250

    readonly property color kBorder:        "#26272B"
    readonly property color kFieldBorder:   "#3F4147"
    readonly property color kAccent:        "#5865F2"
    readonly property color kAccentHover:   "#4752C4"
    readonly property color kFieldBg:       "#1E1F22"
    readonly property color kRowHover:      "#2E3035"

    function open() {
        backend.clearPasswordNotice()
        settingsCurrentPasswordInput.text = ""
        settingsNewPasswordInput.text = ""
        settingsConfirmNewPasswordInput.text = ""
        settingsModal.visible = true
    }

    function close() {
        settingsModal.visible = false
        backend.clearPasswordNotice()
    }

    function submitPasswordChange() {
        backend.changePassword(settingsCurrentPasswordInput.text,
                               settingsNewPasswordInput.text,
                               settingsConfirmNewPasswordInput.text)
    }

    anchors.fill: parent; color: "#000000B3"

    MouseArea { anchors.fill: parent; z: -1; onClicked: settingsModal.close() }

    Rectangle {
        anchors.centerIn: parent; width: 480
        height: settingsCard.implicitHeight + 48
        radius: 8; color: "#313338"
        border.color: settingsModal.kBorder; border.width: 1

        MouseArea { anchors.fill: parent }   // Keep clicks inside the card

        Column {
            id: settingsCard; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 24; spacing: 0

            // ── Header ──
            Item {
                width: parent.width; height: 28
                Text { anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    text: "User Settings"; color: "#F2F3F5"; font.pixelSize: 20; font.weight: Font.Bold }
                Rectangle {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    width: 28; height: 28; radius: 6
                    color: closeMa.containsMouse ? settingsModal.kRowHover : "transparent"
                    Text { anchors.centerIn: parent; text: "\u2715"; color: closeMa.containsMouse ? "#F2F3F5" : "#B5BAC1"; font.pixelSize: 16 }
                    MouseArea { id: closeMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: settingsModal.close() }
                }
            }
            Item { height: 20; width: 1 }

            // ── Account section ──
            Text { text: "ACCOUNT"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5 }
            Item { height: 10; width: 1 }
            Rectangle {
                width: parent.width; height: 52; radius: 6
                color: accountRow.containsMouse ? settingsModal.kRowHover : settingsModal.kFieldBg
                border.color: settingsModal.kBorder; border.width: 1
                MouseArea { id: accountRow; anchors.fill: parent; hoverEnabled: true }
                Row {
                    anchors.fill: parent; anchors.leftMargin: 12; spacing: 10

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 36; height: 36; radius: 18
                        color: backend.isLoggedIn ? backend.avatarColor(backend.username) : settingsModal.kAccent
                        Text { anchors.centerIn: parent
                            text: backend.username.length > 0 ? backend.username[0].toUpperCase() : "?"
                            color: "white"; font.pixelSize: 16; font.weight: Font.Bold }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        Text { text: backend.username.length > 0 ? backend.username : "\u2026"; color: "#F2F3F5"; font.pixelSize: 15; font.weight: Font.DemiBold }
                        Row { spacing: 5
                            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 8; height: 8; radius: 4; color: "#23A55A" }
                            Text { text: "Logged in"; color: "#B5BAC1"; font.pixelSize: 12 }
                        }
                    }
                }
            }
            Item { height: 20; width: 1 }
            Rectangle { width: parent.width; height: 1; color: settingsModal.kBorder }
            Item { height: 20; width: 1 }

            // ── Voice & audio section ──
            Text { text: "VOICE & AUDIO"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5 }
            Item { height: 12; width: 1 }
            Text { text: "Input sensitivity"; color: "#DBDEE1"; font.pixelSize: 14; font.weight: Font.Medium }
            Item { height: 4; width: 1 }
            Text { text: "How loud you need to speak before your mic starts transmitting."; color: "#80848E"; font.pixelSize: 12; wrapMode: Text.WordWrap; width: parent.width }
            Item { height: 10; width: 1 }
            Rectangle {
                width: parent.width; height: 56; radius: 6
                color: settingsModal.kFieldBg; border.color: settingsModal.kBorder; border.width: 1

                Row {
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 12
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "Quiet"; color: "#80848E"; font.pixelSize: 11 }
                    Slider {
                        id: sensitivitySlider
                        width: parent.width - 100
                        anchors.verticalCenter: parent.verticalCenter
                        from: 0; to: 100; stepSize: 1
                        value: backend.voiceActivationSensitivity
                        onMoved: backend.setVoiceActivationSensitivity(Math.round(sensitivitySlider.value))
                    }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: "Loud"; color: "#80848E"; font.pixelSize: 11 }
                }
            }
            Item { height: 20; width: 1 }
            Rectangle { width: parent.width; height: 1; color: settingsModal.kBorder }
            Item { height: 20; width: 1 }

            // ── Security section ──
            Text { text: "SECURITY"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5 }
            Item { height: 12; width: 1 }
            Text { text: "Change password"; color: "#DBDEE1"; font.pixelSize: 14; font.weight: Font.Medium }
            Item { height: 4; width: 1 }
            Text { text: "Other devices stay signed out; this one keeps its session."; color: "#80848E"; font.pixelSize: 12; wrapMode: Text.WordWrap; width: parent.width }
            Item { height: 10; width: 1 }

            Column {
                width: parent.width; spacing: 10

                Text { text: "CURRENT PASSWORD"; color: "#B5BAC1"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.5 }
                Rectangle {
                    width: parent.width; height: 40; radius: 4
                    color: settingsModal.kFieldBg
                    border.color: settingsCurrentPasswordInput.activeFocus ? settingsModal.kAccent : settingsModal.kFieldBorder
                    border.width: 1
                    TextInput {
                        id: settingsCurrentPasswordInput
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#DBDEE1"; font.pixelSize: 14; selectionColor: settingsModal.kAccent
                        echoMode: TextInput.Password
                        maximumLength: 128
                        Keys.onReturnPressed: settingsModal.submitPasswordChange()
                    }
                }

                Text { text: "NEW PASSWORD"; color: "#B5BAC1"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.5 }
                Rectangle {
                    width: parent.width; height: 40; radius: 4
                    color: settingsModal.kFieldBg
                    border.color: settingsNewPasswordInput.activeFocus ? settingsModal.kAccent : settingsModal.kFieldBorder
                    border.width: 1
                    TextInput {
                        id: settingsNewPasswordInput
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#DBDEE1"; font.pixelSize: 14; selectionColor: settingsModal.kAccent
                        echoMode: TextInput.Password
                        maximumLength: 128
                        Keys.onReturnPressed: settingsModal.submitPasswordChange()
                    }
                }

                Text { text: "CONFIRM NEW PASSWORD"; color: "#B5BAC1"; font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 0.5 }
                Rectangle {
                    width: parent.width; height: 40; radius: 4
                    color: settingsModal.kFieldBg
                    border.color: settingsConfirmNewPasswordInput.activeFocus ? settingsModal.kAccent : settingsModal.kFieldBorder
                    border.width: 1
                    TextInput {
                        id: settingsConfirmNewPasswordInput
                        anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        color: "#DBDEE1"; font.pixelSize: 14; selectionColor: settingsModal.kAccent
                        echoMode: TextInput.Password
                        maximumLength: 128
                        Keys.onReturnPressed: settingsModal.submitPasswordChange()
                    }
                }
            }

            Item { height: 10; width: 1 }
            Text {
                width: parent.width
                text: backend.passwordNotice
                color: backend.passwordNotice === "Password updated." ? "#23A55A" : "#ED4245"
                font.italic: true; font.pixelSize: 12
                visible: backend.passwordNotice.length > 0
                wrapMode: Text.WordWrap
            }
            Item { height: 8; width: 1 }

            // Success clears the fields so the next edit starts from scratch
            Connections {
                target: backend
                function onPasswordNoticeChanged() {
                    if (backend.passwordNotice === "Password updated.")
                    {
                        settingsCurrentPasswordInput.text = ""
                        settingsNewPasswordInput.text = ""
                        settingsConfirmNewPasswordInput.text = ""
                    }
                }
            }

            Rectangle {
                width: 160; height: 38; radius: 4
                color: changePassMa.containsMouse ? settingsModal.kAccentHover : settingsModal.kAccent
                Text { anchors.centerIn: parent; text: "Change Password"; color: "white"; font.pixelSize: 14; font.weight: Font.Medium }
                MouseArea {
                    id: changePassMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: settingsModal.submitPasswordChange()
                }
            }
            Item { height: 20; width: 1 }
            Rectangle { width: parent.width; height: 1; color: settingsModal.kBorder }
            Item { height: 20; width: 1 }

            // ── Log out ──
            Rectangle {
                width: parent.width; height: 42; radius: 4
                color: logoutMa.containsMouse ? "#A1282C" : "#DA373C"
                border.color: logoutMa.containsMouse ? "#B53137" : settingsModal.kBorder; border.width: 1
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

    Keys.onEscapePressed: settingsModal.close()
    focus: visible
}
