import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shown while `target` (the Downloads button it is declared in) is hovered: a compact
// list of what is downloading right now, beside the sidebar.
Popup {
    id: root
    property Item target: parent
    property var downloads: []
    x: target.width + 14
    y: target.height - height
    width: 300
    padding: 14
    visible: target.hovered && downloads.length > 0
    closePolicy: Popup.NoAutoClose
    background: Rectangle { color: Theme.popup; radius: 14; border.width: 1; border.color: Theme.lineStrong }
    contentItem: ColumnLayout {
        spacing: 10
        Text {
            text: qsTr("Baixando agora")
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
        }
        Repeater {
            model: root.downloads.slice(0, 4)
            delegate: ColumnLayout {
                id: summary
                required property var modelData
                Layout.fillWidth: true
                spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: summary.modelData.title
                        color: Theme.text
                        font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: summary.modelData.progress >= 0
                        text: Math.round(summary.modelData.progress * 100) + "%"
                        color: Theme.accentText
                        font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 3; radius: 1.5
                    color: "#1FFFFFFF"
                    Rectangle {
                        width: parent.width * Math.max(0.08, summary.modelData.progress)
                        height: parent.height; radius: 1.5
                        color: Theme.accent
                        opacity: summary.modelData.progress >= 0 ? 1 : 0.5
                    }
                }
            }
        }
    }
}
