import QtQuick
import QtQuick.Controls

Rectangle {
    id: addChannelDialog
    visible: false; z: 200
    property string channelType: "text"

    function open(type) {
        channelType = type
        channelNameInput.text = ""
        visible = true
        channelNameInput.forceActiveFocus()
    }

    function doCreateChannel() {
        let name = channelNameInput.text.trim()
        if (name.length === 0) return
        if (addChannelDialog.channelType === "text")
            backend.requestNewTextChannel(name)
        else
            backend.requestNewVoiceChannel(name)
        addChannelDialog.visible = false
    }

    anchors.fill: parent; color: "#00000080"

    Rectangle {
        anchors.centerIn: parent; width: 440; height: 240; radius: 4; color: "#313338"
        Column {
            anchors.fill: parent; anchors.margins: 24; spacing: 0
            Row {
                width: parent.width; height: 40
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: addChannelDialog.channelType === "text" ? "Create Text Channel" : "Create Voice Channel"
                    color: "#F2F3F5"; font.pixelSize: 20; font.weight: Font.Bold
                    width: parent.width - 24
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "\u2715"; color: "#B5BAC1"; font.pixelSize: 18
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: addChannelDialog.visible = false
                    }
                }
            }
            Item { height: 16; width: 1 }
            Text { text: "CHANNEL NAME"; color: "#B5BAC1"; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 0.5 }
            Item { height: 8; width: 1 }
            Rectangle {
                width: parent.width; height: 40; color: "#1E1F22"; radius: 4
                Row {
                    anchors.fill: parent; anchors.leftMargin: 10; spacing: 6
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: addChannelDialog.channelType === "text" ? "#" : "\uD83D\uDD0A"
                        color: "#80848E"; font.pixelSize: 16
                    }
                    TextInput {
                        id: channelNameInput
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 40
                        color: "#DBDEE1"; font.pixelSize: 15; selectionColor: "#5865F2"
                        Text {
                            anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                            text: addChannelDialog.channelType === "text" ? "new-channel" : "New Voice"
                            color: "#6D6F78"; font.pixelSize: 15
                            visible: channelNameInput.text.length === 0
                        }
                        Keys.onReturnPressed: addChannelDialog.doCreateChannel()
                        Keys.onEscapePressed: addChannelDialog.visible = false
                    }
                }
            }
            Item { height: 16; width: 1 }
            Row {
                anchors.right: parent.right; spacing: 8
                Rectangle {
                    width: 96; height: 38; radius: 3
                    color: cancelHover.containsMouse ? "#2C2D32" : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"; color: "#DBDEE1"; font.pixelSize: 14
                    }
                    MouseArea {
                        id: cancelHover
                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: addChannelDialog.visible = false
                    }
                }
                Rectangle {
                    width: 110; height: 38; radius: 3
                    color: createHover.containsMouse ? "#4752C4" : "#5865F2"
                    Text {
                        anchors.centerIn: parent
                        text: "Create Channel"; color: "white"; font.pixelSize: 14; font.weight: Font.Medium
                    }
                    MouseArea {
                        id: createHover
                        anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                        onClicked: addChannelDialog.doCreateChannel()
                    }
                }
            }
        }
    }
    MouseArea { anchors.fill: parent; z: -1; onClicked: addChannelDialog.visible = false }
}