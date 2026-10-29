import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Download center: what is downloading now (with cancel where possible) and the history.
Item {
    id: root
    property bool reduced: false
    property var downloads: []
    signal clearHistory()
    readonly property real gutter: width < 900 ? 32 : 48
    readonly property var active: downloads.filter(function(d) { return d.state === "active" })
    readonly property var finished: downloads.filter(function(d) { return d.state !== "active" })

    function iconFor(kind) {
        return kind === "game" ? "cube" : kind === "pack" ? "packs" : kind === "update" ? "download" : "puzzle"
    }
    function ago(ms) {
        if (!ms) return ""
        const minutes = Math.floor((Date.now() - ms) / 60000)
        if (minutes < 1) return "agora"
        if (minutes < 60) return minutes + " min atrás"
        const hours = Math.floor(minutes / 60)
        if (hours < 24) return hours + " h atrás"
        return new Date(ms).toLocaleDateString(Qt.locale(), "d MMM")
    }
    function cancel(item) {
        if (item.kind === "game") runner.cancel()
        else if (item.kind === "pack") mods.cancelPack()
    }

    Text {
        id: title
        x: root.gutter; y: 50
        text: "Downloads"
        color: Theme.text
        font.family: Theme.fontDisplay; font.pixelSize: 30; font.weight: Font.DemiBold
        font.letterSpacing: -0.6
    }

    Flickable {
        id: flick
        x: root.gutter; y: title.y + title.height + 18
        width: root.width - root.gutter * 2
        height: root.height - y
        contentHeight: column.implicitHeight + 40
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: ThinScrollBar {}

        ColumnLayout {
            id: column
            width: flick.width
            spacing: 0

            Text {
                text: "BAIXANDO AGORA"
                color: Theme.muted
                font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
                font.letterSpacing: 0.6
                Layout.bottomMargin: 10
            }
            Text {
                visible: root.active.length === 0
                text: "Nada sendo baixado."
                color: Theme.faint
                font.family: Theme.font; font.pixelSize: 13
                Layout.bottomMargin: 8
            }
            Repeater {
                model: root.active
                delegate: Rectangle {
                    id: activeRow
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.bottomMargin: 10
                    implicitHeight: 86
                    radius: Theme.radiusLg
                    color: "#0AFFFFFF"
                    border.width: 1; border.color: Theme.line
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 16
                        Rectangle {
                            Layout.preferredWidth: 44; Layout.preferredHeight: 44
                            radius: 12; color: Theme.accentSoft
                            UiIcon { anchors.centerIn: parent; width: 20; height: 20; name: root.iconFor(activeRow.modelData.kind); color: Theme.accent }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: activeRow.modelData.title
                                    color: Theme.text
                                    font.family: Theme.font; font.pixelSize: 14; font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    visible: activeRow.modelData.progress >= 0
                                    text: Math.round(activeRow.modelData.progress * 100) + "%"
                                    color: Theme.accentText
                                    font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
                                }
                            }
                            Rectangle {
                                id: bar
                                Layout.fillWidth: true
                                height: 4; radius: 2
                                color: "#1FFFFFFF"
                                clip: true
                                Rectangle {
                                    visible: activeRow.modelData.progress >= 0
                                    width: bar.width * Math.max(0, activeRow.modelData.progress)
                                    height: parent.height; radius: 2; color: Theme.accent
                                    Behavior on width { NumberAnimation { duration: root.reduced ? 0 : 200 } }
                                }
                                Rectangle {
                                    id: sweep
                                    visible: activeRow.modelData.progress < 0
                                    width: bar.width * 0.3; height: parent.height; radius: 2; color: Theme.accent
                                    NumberAnimation on x {
                                        running: sweep.visible && root.visible && !root.reduced
                                        from: -sweep.width; to: bar.width; duration: 1100; loops: Animation.Infinite
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: activeRow.modelData.detail || ""
                                color: Theme.muted
                                font.family: Theme.font; font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                        ActionButton {
                            visible: activeRow.modelData.kind === "game" || activeRow.modelData.kind === "pack"
                            text: "Cancelar"; quiet: true; reduced: root.reduced
                            onClicked: root.cancel(activeRow.modelData)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 24
                Layout.bottomMargin: 10
                Text {
                    Layout.fillWidth: true
                    text: "HISTÓRICO"
                    color: Theme.muted
                    font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
                    font.letterSpacing: 0.6
                }
                ActionButton {
                    visible: root.finished.length > 0
                    text: "Limpar histórico"; quiet: true; implicitHeight: 30; font.pixelSize: 12; reduced: root.reduced
                    onClicked: root.clearHistory()
                }
            }
            Text {
                visible: root.finished.length === 0
                text: "Downloads concluídos aparecem aqui."
                color: Theme.faint
                font.family: Theme.font; font.pixelSize: 13
            }
            Repeater {
                model: root.finished
                delegate: Item {
                    id: doneRow
                    required property var modelData
                    readonly property bool ok: modelData.state === "done"
                    Layout.fillWidth: true
                    implicitHeight: 58
                    RowLayout {
                        anchors.fill: parent
                        spacing: 14
                        Rectangle {
                            Layout.preferredWidth: 32; Layout.preferredHeight: 32
                            radius: 16
                            color: doneRow.ok ? "#26A9CFA0" : "#26F0A19A"
                            UiIcon {
                                anchors.centerIn: parent; width: 15; height: 15
                                name: doneRow.ok ? "check" : "close"
                                color: doneRow.ok ? Theme.success : Theme.danger
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: doneRow.modelData.title
                                color: Theme.text
                                font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: (doneRow.modelData.state === "done" ? "Concluído" : doneRow.modelData.state === "cancelled" ? "Cancelado" : "Falhou")
                                    + (doneRow.modelData.detail && doneRow.modelData.state !== "done" ? "  ·  " + doneRow.modelData.detail : "")
                                color: doneRow.ok ? Theme.faint : Theme.danger
                                font.family: Theme.font; font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                        Text {
                            text: root.ago(doneRow.modelData.finished)
                            color: Theme.faint
                            font.family: Theme.font; font.pixelSize: 12
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                }
            }
        }
    }
}
