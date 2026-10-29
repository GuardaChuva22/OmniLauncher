import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Lists mods.versionChoices (newest first) and reports the one picked.
GlassDialog {
    id: root
    property string currentId: ""
    property string actionText: "Instalar"
    property string selectedId: ""
    signal picked(string versionId)
    width: Math.min(640, parent ? parent.width - 80 : 640)
    height: Math.min(620, parent ? parent.height - 80 : 620)
    onAboutToShow: selectedId = ""

    contentItem: ColumnLayout {
        spacing: 14
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: list
                anchors.fill: parent
                clip: true
                spacing: 4
                model: mods.versionChoices
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ThinScrollBar {}
                delegate: AbstractButton {
                    id: option
                    required property var modelData
                    required property int index
                    readonly property bool chosen: root.selectedId === modelData.id
                    readonly property bool current: root.currentId === modelData.id
                    width: list.width
                    height: 62
                    hoverEnabled: true
                    onClicked: root.selectedId = modelData.id
                    onDoubleClicked: if (!current) { root.picked(modelData.id); root.close() }
                    background: Rectangle {
                        radius: 12
                        color: option.chosen ? Theme.accentSoft : option.hovered ? "#0AFFFFFF" : "transparent"
                        border.width: option.chosen ? 1 : 0
                        border.color: Theme.accentLine
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                    contentItem: RowLayout {
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 6
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: option.modelData.versionNumber
                                color: Theme.text
                                font.family: Theme.font; font.pixelSize: 14; font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: "Minecraft " + option.modelData.gameVersions + "  ·  " + option.modelData.loaders + "  ·  " + option.modelData.date
                                color: Theme.muted
                                font.family: Theme.font; font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                        Repeater {
                            model: [
                                { show: option.current, label: "Atual", strong: true },
                                { show: option.index === 0 && !option.current, label: "Mais recente", strong: false },
                                { show: option.modelData.type !== "release", label: option.modelData.type === "beta" ? "Beta" : "Alpha", strong: false }
                            ].filter(function(tag) { return tag.show })
                            delegate: Rectangle {
                                required property var modelData
                                Layout.preferredHeight: 22
                                Layout.preferredWidth: tagText.implicitWidth + 16
                                Layout.rightMargin: 4
                                radius: 6
                                color: modelData.strong ? Theme.accentSoft : Theme.fill
                                Text {
                                    id: tagText
                                    anchors.centerIn: parent
                                    text: parent.modelData.label
                                    color: parent.modelData.strong ? Theme.accentText : Theme.secondary
                                    font.family: Theme.font; font.pixelSize: 11; font.weight: Font.DemiBold
                                }
                            }
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: mods.versionChoicesLoading ? "Buscando versões…" : "Nenhuma versão compatível encontrada"
                color: Theme.muted
                font.family: Theme.font; font.pixelSize: 14
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Item { Layout.fillWidth: true }
            ActionButton { text: "Cancelar"; quiet: true; reduced: root.reduced; onClicked: root.close() }
            ActionButton {
                text: root.actionText
                primary: true
                reduced: root.reduced
                enabled: root.selectedId !== "" && root.selectedId !== root.currentId
                onClicked: { root.picked(root.selectedId); root.close() }
            }
        }
    }
}
