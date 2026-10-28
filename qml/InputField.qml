import QtQuick
import QtQuick.Controls

TextField {
    id: control
    property string iconName: ""
    property bool clearable: false
    implicitHeight: 42
    color: Theme.text
    placeholderTextColor: Theme.faint
    selectionColor: "#8CE68AA6"
    selectedTextColor: "white"
    font.family: Theme.font
    font.pixelSize: 14
    leftPadding: iconName ? 38 : 14
    rightPadding: clearable && text ? 34 : 14
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true
    background: Rectangle {
        radius: Theme.radiusMd
        color: control.activeFocus ? "#14FFFFFF" : control.hovered ? "#10FFFFFF" : Theme.fill
        border.width: 1
        border.color: control.activeFocus ? Theme.accentLine : Theme.line
        Behavior on color { ColorAnimation { duration: Theme.fast } }
        Behavior on border.color { ColorAnimation { duration: Theme.fast } }
        UiIcon {
            visible: control.iconName !== ""
            name: control.iconName
            x: 13; anchors.verticalCenter: parent.verticalCenter
            width: 16; height: 16
            color: control.activeFocus ? Theme.accent : Theme.muted
        }
    }
    AbstractButton {
        visible: control.clearable && control.text !== ""
        anchors.right: parent.right; anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 22; height: 22
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        Accessible.name: "Limpar"
        onClicked: { control.clear(); control.forceActiveFocus() }
        background: Rectangle { radius: 11; color: parent.hovered ? Theme.fillActive : Theme.fillHover }
        contentItem: Item { UiIcon { anchors.centerIn: parent; width: 11; height: 11; stroke: 1.6; name: "close"; color: Theme.secondary } }
    }
}
