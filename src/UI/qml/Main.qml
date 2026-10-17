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
        // Returning to the login screen must start from a clean form: a stale
        // username/password sitting in the fields after logout looks like an
        // accidental "pre-filled" login (and invites Enter-to-relogin mistakes).
        function onIsLoggedInChanged() {
            if (!backend.isLoggedIn) loginOverlay.reset()
        }
        // NOTE: Do NOT assign loginOverlay.opacity imperatively here — that would
        // destroy its declarative "opacity: backend.isLoggedIn ? 0 : 1" binding
        // (QML removes a binding on first imperative write), which permanently
        // froze the overlay at opacity 0 after the first login and broke the
        // return to the login screen on logout.
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