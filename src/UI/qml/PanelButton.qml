import QtQuick
import QtQuick.Controls

/* Discord-style panel icon button (mute / deafen / settings). */
Item {
    id: btnRoot

    property string iconName: ""
    property string btnTooltip: ""
    property bool   isActive: false

    signal clicked

    width: 32; height: 32

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: 4
        color: panelBtnMa.containsMouse ? "#35373C" : "transparent"

        Icon {
            anchors.centerIn: parent
            name: btnRoot.iconName
            size: 20
            color: btnRoot.isActive ? "#ED4245"
                 : (panelBtnMa.containsMouse ? "#DBDEE1" : "#B5BAC1")
        }
    }

    ToolTip {
        id: btnTip
        visible: panelBtnMa.containsMouse
        delay: 500
        background: Rectangle { color: "#111214"; radius: 4; border.color: "#3F4147"; border.width: 1 }
        contentItem: Text { text: btnRoot.btnTooltip; color: "#DBDEE1"; font.pixelSize: 13 }
    }

    MouseArea {
        id: panelBtnMa
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: btnRoot.clicked()
    }
}
