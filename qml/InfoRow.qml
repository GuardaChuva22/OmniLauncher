import QtQuick
import QtQuick.Layouts

// Label/value line used in the game drawer. Extra trailing items go in the default slot.
Item {
    id: root
    property string label: ""
    property string value: ""
    property string iconName: ""
    property bool divider: true
    default property alias trailing: extra.data
    implicitHeight: 46
    Layout.fillWidth: true

    RowLayout {
        anchors.fill: parent
        spacing: 12
        UiIcon {
            visible: root.iconName !== ""
            name: root.iconName
            Layout.preferredWidth: 16; Layout.preferredHeight: 16
            color: Theme.muted
        }
        Text {
            text: root.label
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 13
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        Text {
            visible: root.value !== ""
            text: root.value
            color: Theme.text
            font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignRight
            Layout.maximumWidth: root.width * 0.6
        }
        Row { id: extra; spacing: 8; Layout.alignment: Qt.AlignVCenter }
    }
    Rectangle {
        visible: root.divider
        anchors.bottom: parent.bottom
        width: parent.width; height: 1
        color: Theme.line
    }
}
