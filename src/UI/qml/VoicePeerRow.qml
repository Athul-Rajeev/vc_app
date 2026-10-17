import QtQuick

/* Discord-style connected voice user row. */
Item {
    id: peerRoot

    property string peerUsername: ""
    property bool   peerMuted: false
    property bool   peerDeafened: false
    property bool   peerSpeaking: false
    property string peerColor: "#5865F2"
    property string peerLetter: "?"

    height: 34

    Rectangle {
        anchors {
            fill: parent
            leftMargin: 8; rightMargin: 8; topMargin: 1; bottomMargin: 1
        }
        radius: 4
        color: peerMa.containsMouse ? "#35373C" : "transparent"

        Row {
            anchors {
                fill: parent
                leftMargin: 24; rightMargin: 8
            }
            spacing: 8

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 24; height: 24; radius: 12
                color: peerRoot.peerColor
                border.color: peerRoot.peerSpeaking ? "#23A55A" : "transparent"
                border.width: peerRoot.peerSpeaking ? 2 : 0

                Text {
                    anchors.centerIn: parent
                    text: peerRoot.peerLetter
                    color: "white"; font.pixelSize: 10; font.weight: Font.Bold
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: peerRoot.peerUsername
                color: peerRoot.peerSpeaking ? "#FFFFFF" : "#949BA4"
                font.pixelSize: 14
                font.weight: Font.Medium
                width: parent.width - 24 - 8 - (peerRoot.peerMuted || peerRoot.peerDeafened ? 20 : 0)
                elide: Text.ElideRight
            }

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: peerRoot.peerDeafened
                name: "headphone-off"; size: 16; color: "#80848E"
            }
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: peerRoot.peerMuted && !peerRoot.peerDeafened
                name: "mic-off"; size: 16; color: "#80848E"
            }
        }
    }

    MouseArea {
        id: peerMa
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
    }
}
