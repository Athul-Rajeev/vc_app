import QtQuick
import QtQuick.Controls

/* Full-screen authentication surface.
   Four modes: "login", "register", and "reset" (all show the form), plus the
   busy states driven by backend.loginState ("connecting"/"authenticating"),
   which replace the form with a spinner. The background is opaque so the
   signed-out user never sees the (empty) app behind it. */
Rectangle {
    id: loginOverlay
    property string mode: "login"   // "login" | "register" | "reset"

    readonly property bool isBusy: backend.loginState === "connecting" || backend.loginState === "authenticating"
    readonly property string busyTitle: backend.loginState === "connecting"
        ? "Connecting to server…"
        : (mode === "register" ? "Creating account…"
          : mode === "reset" ? "Resetting password…"
          : "Signing in…")

    visible: true; z: 300
    anchors.fill: parent; color: "#313338"
    opacity: backend.isLoggedIn ? 0 : 1
    Behavior on opacity { NumberAnimation { duration: 300; easing.type: Easing.InQuad } }

    // Slow-connection hint: after a few seconds the user learns "it's still working".
    Timer {
        id: slowConnectionTimer
        interval: 5000; repeat: false
        onTriggered: slowConnectionHint.visible = true
    }
    onIsBusyChanged: {
        if (isBusy) { slowConnectionHint.visible = false; slowConnectionTimer.restart() }
        else slowConnectionTimer.stop()
    }

    function reset() {
        mode = "login"
        usernameInput.text = ""
        passwordInput.text = ""
        confirmPasswordInput.text = ""
        resetTokenInput.text = ""
        // NOTE: loginError is deliberately NOT cleared here. When the server
        // rejects a session/token on logout, the (curated) AUTH_FAILED message
        // must survive this call — the user needs to see why they were signed out.
    }

    function switchMode(newMode) {
        mode = newMode
        usernameInput.text = ""
        passwordInput.text = ""
        confirmPasswordInput.text = ""
        resetTokenInput.text = ""
        backend.clearLoginError()
        usernameInput.forceActiveFocus()
    }

    function submit() {
        let user = usernameInput.text
        let pass = passwordInput.text
        if (mode === "login") {
            if (user.length > 0 && pass.length > 0) backend.login(user, pass)
        } else if (mode === "register") {
            if (user.length > 0 && pass.length > 0) backend.registerAccount(user, pass, confirmPasswordInput.text)
        } else {
            if (user.length > 0 && pass.length > 0)
                backend.resetPassword(user, resetTokenInput.text, pass, confirmPasswordInput.text)
        }
    }

    MouseArea { anchors.fill: parent; hoverEnabled: true }

    Rectangle {
        id: authCard
        anchors.centerIn: parent; width: 480; radius: 4; color: "#313338"
        height: loginOverlay.isBusy ? busyCard.implicitHeight + 48 : loginCard.implicitHeight + 48

        // ── Busy state: spinner replaces the form ──
        Column {
            id: busyCard
            visible: loginOverlay.isBusy
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.topMargin: 32; anchors.leftMargin: 32; anchors.rightMargin: 32
            spacing: 0

            Item {
                width: parent.width; height: 44
                // Spinning arc over a static dim ring
                Item {
                    id: spinner
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 40; height: 40

                    Rectangle {
                        anchors.fill: parent
                        radius: 20; color: "transparent"; border.color: "#3F4147"; border.width: 3
                    }
                    Item {
                        anchors.fill: parent
                        NumberAnimation on rotation { from: 0; to: 360; duration: 900; loops: Animation.Infinite }
                        Canvas {
                            anchors.fill: parent
                            onPaint: {
                                let ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                ctx.strokeStyle = "#5865F2"
                                ctx.lineWidth = 3
                                ctx.lineCap = "round"
                                ctx.beginPath()
                                ctx.arc(width / 2, height / 2, width / 2 - 1.5, 0, Math.PI * 1.4)
                                ctx.stroke()
                            }
                        }
                    }
                }
            }
            Item { height: 16; width: 1 }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: loginOverlay.busyTitle; color: "#F2F3F5"; font.pixelSize: 16; font.weight: Font.Medium }
            Item { height: 8; width: 1 }
            Text {
                id: slowConnectionHint
                visible: false
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Still working — slow connection to the server, hang tight…"
                color: "#80848E"; font.pixelSize: 12; font.italic: true
            }
            Item { height: 4; width: 1 }
        }

        // ── Form state: login / register / reset ──
        Column {
            id: loginCard
            visible: !loginOverlay.isBusy
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 32; spacing: 0
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: loginOverlay.mode === "login" ? "Welcome back!"
                    : loginOverlay.mode === "register" ? "Create an account"
                    : "Reset your password"
                color: "#F2F3F5"; font.pixelSize: 24; font.weight: Font.Bold
            }
            Item { height: 8; width: 1 }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: loginOverlay.mode === "login" ? "We're so excited to see you again!"
                    : loginOverlay.mode === "register" ? "Pick a username and password. It's just you and your friends here."
                    : "Paste the one-time token the server admin gave you, then choose a new password."
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
            Text { text: "RESET TOKEN"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5; visible: loginOverlay.mode === "reset" }
            Item { height: 8; width: 1; visible: loginOverlay.mode === "reset" }
            Rectangle { width: parent.width; height: 44; color: "#1E1F22"; radius: 4; border.color: resetTokenInput.activeFocus ? "#5865F2" : "transparent"; border.width: 1; visible: loginOverlay.mode === "reset"
                TextInput {
                    id: resetTokenInput; anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: TextInput.AlignVCenter; color: "#DBDEE1"; font.pixelSize: 16; selectionColor: "#5865F2"
                    Keys.onReturnPressed: loginOverlay.submit()
                }
            }
            Item { height: 20; width: 1 }
            Text {
                text: loginOverlay.mode === "reset" ? "NEW PASSWORD" : "PASSWORD"
                color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5
            }
            Item { height: 8; width: 1 }
            Rectangle { width: parent.width; height: 44; color: "#1E1F22"; radius: 4; border.color: passwordInput.activeFocus ? "#5865F2" : "transparent"; border.width: 1
                TextInput {
                    id: passwordInput; anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; verticalAlignment: TextInput.AlignVCenter; color: "#DBDEE1"; font.pixelSize: 16; selectionColor: "#5865F2"; echoMode: TextInput.Password
                    maximumLength: 128
                    Keys.onReturnPressed: loginOverlay.submit()
                }
            }
            Text { text: "CONFIRM PASSWORD"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5; visible: loginOverlay.mode !== "login" }
            Item { height: 8; width: 1; visible: loginOverlay.mode !== "login" }
            Rectangle { width: parent.width; height: 44; color: "#1E1F22"; radius: 4; border.color: confirmPasswordInput.activeFocus ? "#5865F2" : "transparent"; border.width: 1; visible: loginOverlay.mode !== "login"
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
                Text { anchors.centerIn: parent
                    text: loginOverlay.mode === "login" ? "Login"
                        : loginOverlay.mode === "register" ? "Create Account"
                        : "Reset Password"
                    color: "white"; font.pixelSize: 16; font.weight: Font.Medium }
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
                          : (loginOverlay.mode === "register" ? "Already have an account? Login" : "Back to login")
                    color: modeSwitchMa.containsMouse ? "#00A8FC" : "#00A8F0"; font.pixelSize: 13
                }
                MouseArea { id: modeSwitchMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: loginOverlay.switchMode(loginOverlay.mode === "login" ? "register" : "login")
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Forgot your password?"
                color: forgotMa.containsMouse ? "#00A8FC" : "#00A8F0"; font.pixelSize: 13
                visible: loginOverlay.mode === "login"
                MouseArea { id: forgotMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                    onClicked: loginOverlay.switchMode("reset")
                }
            }
            Item { height: 4; width: 1 }
        }
    }
}
