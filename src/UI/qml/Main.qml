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

    FontLoader { id: fontNormal; source: "qrc:/fonts/GoogleSans-Regular.ttf" }

    Connections {
        target: backend
        function onRequestScrollToBottom() {
            Qt.callLater(function() { chatArea.msgList.positionViewAtEnd() })
        }
        function onIsLoggedInChanged() {
            if (backend.isLoggedIn) loginOverlay.opacity = 0
        }
    }

    Row {
        anchors.fill: parent
        spacing: 0

        ServerStrip { id: serverStrip }
        Sidebar { id: sidebar }
        ChatArea { id: chatArea }
    }

    EmojiPicker { id: emojiPicker; visible: false }
    AddChannelDialog { id: addChannelDialog; visible: false }
    SettingsModal { id: settingsModal; visible: false }
    LoginOverlay { id: loginOverlay; visible: !backend.isLoggedIn || opacity > 0 }
}