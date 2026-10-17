import QtQuick

Rectangle {
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
                Text { anchors.centerIn: parent; text: "\uD83D\uDCAC"; font.pixelSize: 22 }
                MouseArea { id: homeHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
            }
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 32; height: 2
            color: "#3F4147"
            radius: 1
        }

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
                Text { anchors.centerIn: parent; text: "VC"; font.pixelSize: 14; font.weight: Font.Bold; color: "white" }
                MouseArea { id: serverHover; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor }
            }
        }
    }
}