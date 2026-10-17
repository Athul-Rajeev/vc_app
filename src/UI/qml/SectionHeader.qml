import QtQuick

/* Discord-style category header: caret + UPPERCASE label + hover "+" button. */
Item {
    id: sectionRoot

    property alias label: sectionLabel.text
    property bool collapsed: false

    signal toggleClicked
    signal addClicked

    width: 240
    height: 24

    Icon {
        id: caret
        anchors.left: parent.left
        anchors.leftMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        name: "caret"
        size: 10
        color: sectionHeaderMa.containsMouse ? "#DBDEE1" : "#80848E"
        rotation: sectionRoot.collapsed ? -90 : 0
        Behavior on rotation { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }

    Text {
        id: sectionLabel
        anchors.left: caret.right
        anchors.leftMargin: 2
        anchors.right: addBtn.left
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        color: sectionHeaderMa.containsMouse ? "#DBDEE1" : "#80848E"
        font.pixelSize: 12
        font.weight: Font.Bold
        font.letterSpacing: 0.4
        elide: Text.ElideRight
    }

    Item {
        id: addBtn
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 18
        height: 18
        visible: sectionHeaderMa.containsMouse || addBtnMa.containsMouse

        Rectangle {
            anchors.fill: parent
            radius: 3
            color: addBtnMa.containsMouse ? "#35373C" : "transparent"
        }
        Icon {
            anchors.centerIn: parent
            name: "plus"
            size: 15
            color: addBtnMa.containsMouse ? "#DBDEE1" : "#80848E"
        }
        MouseArea {
            id: addBtnMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: sectionRoot.addClicked()
        }
    }

    MouseArea {
        id: sectionHeaderMa
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        z: -1
        onClicked: sectionRoot.toggleClicked()
    }
}
