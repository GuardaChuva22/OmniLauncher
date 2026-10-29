import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Modrinth search for one game: results already match its Minecraft version and loader.
GlassDialog {
    id: root
    property var game: null
    property string kind: "mod"
    property string loader: ""
    title: kind === "shader" ? qsTr("Adicionar shaders") : kind === "resourcepack" ? qsTr("Adicionar resource packs") : qsTr("Adicionar mods")
    width: Math.min(780, parent ? parent.width - 80 : 780)
    height: Math.min(660, parent ? parent.height - 80 : 660)

    function openFor(target, contentKind, contentLoader) {
        if (!target) return
        game = target
        kind = contentKind || "mod"
        loader = contentLoader || target.loader
        query.text = ""
        mods.loadInstalled(target.id, kind)
        mods.search("", target.version, loader, 0, kind)
        open()
        query.forceActiveFocus()
    }
    function formatCount(value) {
        if (value >= 1e6) return Number(value / 1e6).toLocaleString(Qt.locale(), "f", 1) + qsTr(" mi")
        if (value >= 1e3) return Math.round(value / 1e3) + qsTr(" mil")
        return String(value)
    }

    Timer {
        id: searchDelay
        interval: 350
        onTriggered: if (root.game) mods.search(query.text, root.game.version, root.loader, 0, root.kind)
    }

    contentItem: ColumnLayout {
        spacing: 14
        Text {
            text: root.game ? "Minecraft " + root.game.version + "  ·  " + (root.kind === "mod" ? root.game.loader : root.kind === "shader" ? qsTr("Iris / OptiFine") : qsTr("Resource packs")) + qsTr("  ·  Modrinth") : ""
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 13
            Layout.topMargin: -10
        }
        InputField {
            id: query
            Layout.fillWidth: true
            iconName: "search"
            clearable: true
            placeholderText: root.kind === "shader" ? qsTr("Buscar shaders") : root.kind === "resourcepack" ? qsTr("Buscar resource packs") : qsTr("Buscar mods")
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
                    mods.search(query.text, root.game.version, root.loader, count, root.kind)

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
                                    text: qsTr("por ") + row.modelData.author
                                    color: Theme.faint
                                    font.family: Theme.font; font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Text {
                                    text: root.formatCount(row.modelData.downloads) + qsTr(" downloads")
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
                            text: row.installed ? qsTr("Instalado") : row.busy ? qsTr("Instalando…") : qsTr("Instalar")
                            iconName: row.installed ? "check" : row.busy ? "" : "download"
                            primary: !row.installed && !row.busy
                            quiet: row.installed
                            enabled: !row.installed && !row.busy
                            reduced: root.reduced
                            onClicked: mods.install(row.modelData.id, root.game.id, root.game.version, root.loader, root.kind)
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: results.count === 0
                text: mods.searching ? qsTr("Buscando…") : mods.searchError !== "" ? mods.searchError : qsTr("Nada encontrado")
                color: Theme.muted
                font.family: Theme.font; font.pixelSize: 14
            }
        }
    }
}
