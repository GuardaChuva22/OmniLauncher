import QtQuick
import QtQuick.Controls

ComboBox {
    id: control
    implicitHeight: 42
    font.family: Theme.font
    font.pixelSize: 14
    leftPadding: 14; rightPadding: 36
    hoverEnabled: true
    // The saved choice, bound by the user of the field. The field shows it from the start,
    // again after a pick (the save may have turned it down) and after its options change,
    // a language switch included, which puts a ComboBox back on its first option.
    // -1: the field keeps its own index.
    property int selectedIndex: -1
    function showSelected() {
        if (selectedIndex >= 0 && selectedIndex < count && currentIndex !== selectedIndex)
            currentIndex = selectedIndex
    }
    onSelectedIndexChanged: showSelected()
    onCountChanged: showSelected()
    onModelChanged: Qt.callLater(showSelected)
    Component.onCompleted: showSelected()
    Connections {
        target: control
        function onActivated() { Qt.callLater(control.showSelected) }
    }
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: UiIcon {
        x: control.width - 30; y: (control.height - height) / 2
        width: 14; height: 14; name: "updown"; color: Theme.muted
    }
    background: Rectangle {
        radius: Theme.radiusMd
        color: control.pressed || control.popup.visible ? "#16FFFFFF" : control.hovered ? "#10FFFFFF" : Theme.fill
        border.width: 1
        border.color: control.visualFocus || control.popup.visible ? Theme.accentLine : Theme.line
        Behavior on color { ColorAnimation { duration: Theme.fast } }
    }
    delegate: ItemDelegate {
        id: option
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : control.width
        height: 36
        highlighted: control.highlightedIndex === index
        leftPadding: 12; rightPadding: 12
        contentItem: Item {
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: option.modelData
                color: Theme.text
                font.family: Theme.font; font.pixelSize: 14
                font.weight: control.currentIndex === option.index ? Font.DemiBold : Font.Normal
            }
            UiIcon {
                visible: control.currentIndex === option.index
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                width: 15; height: 15; name: "check"; color: Theme.accent
            }
        }
        background: Rectangle { radius: 7; color: option.highlighted ? Theme.fillActive : "transparent" }
    }
    popup: Popup {
        y: control.height + 6
        width: control.width
        padding: 5
        implicitHeight: Math.min(contentItem.implicitHeight + 10, 260)
        enter: Transition {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.fast }
            NumberAnimation { property: "y"; from: control.height + 1; to: control.height + 6; duration: Theme.normal; easing.type: Easing.OutCubic }
        }
        exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 100 } }
        background: Rectangle {
            color: Theme.popup; radius: 12
            border.width: 1; border.color: Theme.lineStrong
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            spacing: 1
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
