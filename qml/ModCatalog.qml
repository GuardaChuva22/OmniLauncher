import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Modrinth search for one game: results already match its Minecraft version and loader.
GlassDialog {
    id: root
    property var game: null
    title: "Adicionar mods"
    width: Math.min(780, parent ? parent.width - 80 : 780)
    height: Math.min(660, parent ? parent.height - 80 : 660)

    function openFor(target) {
        if (!target) return
        game = target
        query.text = ""
        mods.loadInstalled(target.id)
        mods.search("", target.version, target.loader, 0)
        open()
        query.forceActiveFocus()
    }
    function formatCount(value) {
        if (value >= 1e6) return (value / 1e6).toFixed(1).replace(".", ",") + " mi"
        if (value >= 1e3) return Math.round(value / 1e3) + " mil"
        return String(value)
    }

    Timer {
        id: searchDelay
        interval: 350
        onTriggered: if (root.game) mods.search(query.text, root.game.version, root.game.loader, 0)
    }

    contentItem: ColumnLayout {
        spacing: 14
        Text {
            text: root.game ? "Minecraft " + root.game.version + "  ·  " + root.game.loader + "  ·  Modrinth" : ""
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 13
            Layout.topMargin: -10
        }
        InputField {
            id: query
            Layout.fillWidth: true
            iconName: "search"
            clearable: true
            placeholderText: "Buscar mods"
            onTextChanged: searchDelay.restart()
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: results
                anchors.fill: parent
                clip: true
                spacing: 2
                model: mods.results
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ThinScrollBar {}
                // Next page when the end comes into view.
                onAtYEndChanged: if (atYEnd && count > 0 && count < mods.totalHits && !mods.searching && root.game)
                    mods.search(query.text, root.game.version, root.game.loader, count)

                delegate: Item {
                    id: row
                    required property var modelData
                    readonly property bool installed: mods.installedProjects.indexOf(modelData.id) !== -1
                    readonly property bool busy: mods.installing.indexOf(modelData.id) !== -1
                    width: results.width
                    height: 78

                    HoverHandler { id: hover }
                    Rectangle {
                        anchors.fill: parent
                        radius: 12
                        color: hover.hovered ? "#0AFFFFFF" : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.fast } }
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10; anchors.rightMargin: 12
                        spacing: 14
                        Rectangle {
                            Layout.preferredWidth: 48; Layout.preferredHeight: 48
                            radius: 10
                            color: Theme.fill
                            clip: true
                            UiIcon { anchors.centerIn: parent; visible: icon.status !== Image.Ready; width: 20; height: 20; name: "puzzle"; color: Theme.faint }
                            Image {
                                id: icon
                                anchors.fill: parent
                                source: row.modelData.iconUrl
                                sourceSize: Qt.size(96, 96)
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    text: row.modelData.title
                                    color: Theme.text
                                    font.family: Theme.font; font.pixelSize: 14; font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    Layout.maximumWidth: implicitWidth
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: "por " + row.modelData.author
                                    color: Theme.faint
                                    font.family: Theme.font; font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: root.formatCount(row.modelData.downloads) + " downloads"
                                    color: Theme.faint
                                    font.family: Theme.font; font.pixelSize: 12
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.description
                                color: Theme.muted
                                font.family: Theme.font; font.pixelSize: 12
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                        }
                        ActionButton {
                            Layout.preferredWidth: 118
                            text: row.installed ? "Instalado" : row.busy ? "Instalando…" : "Instalar"
                            iconName: row.installed ? "check" : row.busy ? "" : "download"
                            primary: !row.installed && !row.busy
                            quiet: row.installed
                            enabled: !row.installed && !row.busy
                            reduced: root.reduced
                            onClicked: mods.install(row.modelData.id, root.game.id, root.game.version, root.game.loader)
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: results.count === 0
                text: mods.searching ? "Buscando…" : mods.searchError !== "" ? mods.searchError : "Nenhum mod encontrado"
                color: Theme.muted
                font.family: Theme.font; font.pixelSize: 14
            }
        }
    }
}
