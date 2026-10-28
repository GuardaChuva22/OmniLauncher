import QtQuick
import QtQuick.Layouts

// Title + optional description on the left, a control on the right.
Item {
    id: root
    property string title: ""
    property string description: ""
    property bool divider: true
    default property alias control: slot.data
    implicitHeight: Math.max(64, texts.implicitHeight + 28)
    Layout.fillWidth: true

    ColumnLayout {
        id: texts
        anchors.left: parent.left; anchors.leftMargin: 20
        anchors.right: slot.left; anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3
        Text {
            text: root.title
            color: Theme.text
            font.family: Theme.font; font.pixelSize: 14; font.weight: Font.Medium
            Layout.fillWidth: true; elide: Text.ElideRight
        }
        Text {
            visible: root.description !== ""
            text: root.description
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
    Item {
        id: slot
        anchors.right: parent.right; anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width; height: childrenRect.height
    }
    Rectangle {
        visible: root.divider
        anchors.bottom: parent.bottom
        x: 20; width: parent.width - 40; height: 1
        color: Theme.line
    }
}
