import QtQuick
import QtQuick.Controls

// Underlined text tabs with a sliding indicator.
Item {
    id: root
    property var model: []
    property int currentIndex: 0
    property bool reduced: false
    readonly property Item currentItem: tabs.count > 0 ? tabs.itemAt(Math.min(currentIndex, tabs.count - 1)) : null
    implicitHeight: 40
    implicitWidth: row.implicitWidth

    Row {
        id: row
        height: parent.height
        spacing: 24
        Repeater {
            id: tabs
            model: root.model
            delegate: AbstractButton {
                id: tab
                required property var modelData
                required property int index
                height: row.height
                implicitWidth: label.implicitWidth
                hoverEnabled: true
                Accessible.name: modelData
                Accessible.role: Accessible.PageTab
                onClicked: root.currentIndex = index
                contentItem: Text {
                    id: label
                    text: tab.modelData
                    font.family: Theme.font; font.pixelSize: 14
                    font.weight: root.currentIndex === tab.index ? Font.DemiBold : Font.Medium
                    color: root.currentIndex === tab.index ? Theme.text : tab.hovered ? Theme.secondary : Theme.muted
                    verticalAlignment: Text.AlignVCenter
                    Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }
                }
                background: Rectangle {
                    anchors.fill: parent; anchors.margins: -4
                    radius: 6; color: "transparent"
                    border.width: 2; border.color: Theme.focusRing
                    visible: tab.visualFocus
                }
            }
        }
    }
    Rectangle { width: parent.width; height: 1; anchors.bottom: parent.bottom; color: Theme.line }
    Rectangle {
        anchors.bottom: parent.bottom
        height: 2; radius: 1
        color: Theme.accent
        x: root.currentItem ? root.currentItem.x : 0
        width: root.currentItem ? root.currentItem.width : 0
        Behavior on x { NumberAnimation { duration: root.reduced ? 0 : Theme.normal; easing.type: Easing.OutCubic } }
        Behavior on width { NumberAnimation { duration: root.reduced ? 0 : Theme.normal; easing.type: Easing.OutCubic } }
    }
}
