import QtQuick

/* Discord-style channel row (text or voice). */
Item {
    id: chRowRoot

    property int    channelId:   -1
    property string channelName: ""
    property bool   isSelected:  false
    property bool   isVoice:     false
    property bool   isActive:    false
    property bool   hasUnread:   false

    height: 34

    Rectangle {
        id: rowBg
        anchors {
            fill: parent
            leftMargin: 8; rightMargin: 8; topMargin: 1; bottomMargin: 1
        }
        radius: 4
        color: chRowRoot.isSelected ? "#404249"
             : (chMa.containsMouse ? "#35373C" : "transparent")

        Row {
            anchors {
                fill: parent
                leftMargin: 8; rightMargin: 8
            }
            spacing: 6

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name:  chRowRoot.isVoice ? "speaker" : "hash"
                size:  20
                color: chRowRoot.isSelected ? "#DBDEE1"
                     : (chMa.containsMouse ? "#B5BAC1" : "#80848E")
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: chRowRoot.channelName
                color: chRowRoot.isSelected ? "#FFFFFF"
                     : (chMa.containsMouse || chRowRoot.hasUnread ? "#DBDEE1" : "#949BA4")
                font.pixelSize: 16
                font.weight: Font.Medium
                width: parent.width - 20 - 6
                elide: Text.ElideRight
            }
        }
    }

    MouseArea {
        id: chMa
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (chRowRoot.isVoice)
                backend.joinVoiceChannel(chRowRoot.channelId)
            else
                backend.selectTextChannel(chRowRoot.channelId)
        }
    }
}
