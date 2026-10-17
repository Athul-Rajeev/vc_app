import QtQuick
import QtQuick.Controls

Rectangle {
    id: loginOverlay
    property string mode: "login"   // "login" | "register"

    visible: true; z: 300
    anchors.fill: parent; color: "#000000B0"
    opacity: backend.isLoggedIn ? 0 : 1
    Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.InQuad } }

    function switchMode(newMode) {
        mode = newMode
        usernameInput.text = ""
        passwordInput.text = ""
        confirmPasswordInput.text = ""
        backend.clearLoginError()
        usernameInput.forceActiveFocus()
    }

    function submit() {
        let user = usernameInput.text
        let pass = passwordInput.text
        if (mode === "login") {
            if (user.length > 0 && pass.length > 0) backend.login(user, pass)
        } else {
            if (user.length > 0 && pass.length > 0) backend.registerAccount(user, pass, confirmPasswordInput.text)
        }
    }

    MouseArea { anchors.fill: parent; hoverEnabled: true }

    Rectangle {
        anchors.centerIn: parent; width: 480; height: loginCard.implicitHeight + 48; radius: 4; color: "#313338"
        Column {
            id: loginCard; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 32; spacing: 0
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: loginOverlay.mode === "login" ? "Welcome back!" : "Create an account"; color: "#F2F3F5"; font.pixelSize: 24; font.weight: Font.Bold }
            Item { height: 8; width: 1 }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: loginOverlay.mode === "login" ? "We're so excited to see you again!" : "Pick a username and password. It's just you and your friends here."
                color: "#B5BAC1"; font.pixelSize: 16
            }
            Item { height: 20; width: 1 }
            Text { text: "USERNAME"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5 }
            Item { height: 8; width: 1 }
            Rectangle { width: parent.width; height: 44; color: "#1E1F22"; radius: 4; border.color: usernameInput.activeFocus ? "#5865F2" : "transparent"; border.width: 1
                TextInput {
                    id: usernameInput; anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: TextInput.AlignVCenter; color: "#DBDEE1"; font.pixelSize: 16; selectionColor: "#5865F2"
                    maximumLength: 32
                    Keys.onReturnPressed: loginOverlay.submit()
                }
            }
            Item { height: 20; width: 1 }
            Text { text: "PASSWORD"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5 }
            Item { height: 8; width: 1 }
            Rectangle { width: parent.width; height: 44; color: "#1E1F22"; radius: 4; border.color: passwordInput.activeFocus ? "#5865F2" : "transparent"; border.width: 1
                TextInput {
                    id: passwordInput; anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: TextInput.AlignVCenter; color: "#DBDEE1"; font.pixelSize: 16; selectionColor: "#5865F2"; echoMode: TextInput.Password
                    maximumLength: 128
                    Keys.onReturnPressed: loginOverlay.submit()
                }
            }
            Text { text: "CONFIRM PASSWORD"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5; visible: loginOverlay.mode === "register" }
            Item { height: 8; width: 1; visible: loginOverlay.mode === "register" }
            Rectangle { width: parent.width; height: 44; color: "#1E1F22"; radius: 4; border.color: confirmPasswordInput.activeFocus ? "#5865F2" : "transparent"; border.width: 1; visible: loginOverlay.mode === "register"
                TextInput {
                    id: confirmPasswordInput; anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: TextInput.AlignVCenter; color: "#DBDEE1"; font.pixelSize: 16; selectionColor: "#5865F2"; echoMode: TextInput.Password
                    maximumLength: 128
                    Keys.onReturnPressed: loginOverlay.submit()
                }
            }
            Item { height: 8; width: 1 }
            Text { width: parent.width; text: backend.loginError; color: "#ED4245"; font.pixelSize: 13; font.italic: true; visible: backend.loginError.length > 0; wrapMode: Text.WordWrap }
            Item { height: backend.loginError.length > 0 ? 12 : 20; width: 1 }
            Rectangle {
                width: parent.width; height: 44; radius: 3; color: submitMa.containsMouse ? "#4752C4" : "#5865F2"
                Text { anchors.centerIn: parent; text: loginOverlay.mode === "login" ? "Login" : "Create Account"; color: "white"; font.pixelSize: 16; font.weight: Font.Medium }
                MouseArea { id: submitMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: loginOverlay.submit()
                }
            }
            Item { height: 16; width: 1 }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: modeSwitchText.width + 8; height: 24; radius: 3; color: "transparent"
                Text { id: modeSwitchText; anchors.centerIn: parent
                    text: loginOverlay.mode === "login"
                          ? "Need an account? Register"
                          : "Already have an account? Login"
                    color: modeSwitchMa.containsMouse ? "#00A8FC" : "#00A8F0"; font.pixelSize: 13
                }
                MouseArea { id: modeSwitchMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: loginOverlay.switchMode(loginOverlay.mode === "login" ? "register" : "login")
                }
            }
            Item { height: 4; width: 1 }
        }
    }
}
