import QtQuick
import QtQuick.Controls

Rectangle {
    id: loginOverlay
    visible: true; z: 300
    anchors.fill: parent; color: "#000000B0"
    opacity: backend.isLoggedIn ? 0 : 1
    Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.InQuad } }

    Rectangle {
        anchors.centerIn: parent; width: 480; height: loginCard.implicitHeight + 48; radius: 4; color: "#313338"
        Column {
            id: loginCard; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 32; spacing: 0
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Welcome back!"; color: "#F2F3F5"; font.pixelSize: 24; font.weight: Font.Bold }
            Item { height: 8; width: 1 }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "We're so excited to see you again!"; color: "#B5BAC1"; font.pixelSize: 16 }
            Item { height: 20; width: 1 }
            Text { text: "USERNAME"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5 }
            Item { height: 8; width: 1 }
            Rectangle { width: parent.width; height: 44; color: "#1E1F22"; radius: 4; border.color: loginInput.activeFocus ? "#5865F2" : "transparent"; border.width: 1
                TextInput { id: loginInput; anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: TextInput.AlignVCenter; color: "#DBDEE1"; font.pixelSize: 16; selectionColor: "#5865F2" }
            }
            Item { height: 20; width: 1 }
            Text { text: "PASSWORD"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5 }
            Item { height: 8; width: 1 }
            Rectangle { width: parent.width; height: 44; color: "#1E1F22"; radius: 4; border.color: passwordInput.activeFocus ? "#5865F2" : "transparent"; border.width: 1
                TextInput { id: passwordInput; anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: TextInput.AlignVCenter; color: "#DBDEE1"; font.pixelSize: 16; selectionColor: "#5865F2"; echoMode: TextInput.Password }
            }
            Item { height: 8; width: 1 }
            Text { width: parent.width; text: backend.loginError; color: "#ED4245"; font.pixelSize: 13; font.italic: true; visible: backend.loginError.length > 0; wrapMode: Text.WordWrap }
            Item { height: backend.loginError.length > 0 ? 12 : 20; width: 1 }
            Rectangle {
                width: parent.width; height: 44; radius: 3; color: joinBtnHover.containsMouse ? "#4752C4" : "#5865F2"
                Text { anchors.centerIn: parent; text: "Login / Register"; color: "white"; font.pixelSize: 16; font.weight: Font.Medium }
                MouseArea { id: joinBtnHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: { let u = loginInput.text.trim(); let p = passwordInput.text.trim(); if (u.length > 0 && p.length > 0) backend.login(u, p) }
                }
            }
            Item { height: 4; width: 1 }
        }
    }
}