import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

FocusScope {
    id: root
    property var games: []
    property var allGames: []
    property string selectedId: ""
    property string loaderFilter: "Todos"
    property bool reduced: false
    property alias searchText: search.text
    signal openRequested(var game)
    signal playRequested(var game)
    signal editRequested(var game)
    signal createRequested()
    signal wrappedRequested()
    signal previewChanged(string id, bool active)

    readonly property real gutter: width < 900 ? 32 : 48
    readonly property real gap: 28
    readonly property real available: width - gutter * 2
    readonly property int columns: Math.max(2, Math.floor((available + gap) / (160 + gap)))
    readonly property real tileWidth: Math.min(210, Math.floor((available - gap * (columns - 1)) / columns))
    readonly property bool filtering: search.text !== "" || loaderFilter !== "Todos"
    readonly property var gridModel: filtering ? games : games.concat([{ id: "__create", isCreate: true }])

    function focusSearch() { search.forceActiveFocus(); search.selectAll() }
    function clearFilters() { search.text = ""; loaderFilter = "Todos" }
    function countFor(loader) {
        if (loader === "Todos") return allGames.length
        return allGames.filter(function(game) { return game.loader === loader }).length
    }
    function moveSelection(delta) {
        if (!games.length) return
        let index = games.findIndex(function(game) { return game.id === root.selectedId })
        index = index < 0 ? 0 : Math.max(0, Math.min(games.length - 1, index + delta))
        grid.positionViewAtIndex(index, GridView.Contain)
        const cell = grid.itemAtIndex(index)
        if (cell) cell.focusTile()
    }
    Keys.onLeftPressed: moveSelection(-1)
    Keys.onRightPressed: moveSelection(1)
    Keys.onUpPressed: moveSelection(-columns)
    Keys.onDownPressed: moveSelection(columns)

    // Header
    RowLayout {
        id: header
        x: root.gutter; y: 50
        width: root.available
        spacing: 10
        Row {
            spacing: 12
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            Text {
                id: title
                text: "Jogos"
                color: Theme.text
                font.family: Theme.fontDisplay; font.pixelSize: 30; font.weight: Font.DemiBold
                font.letterSpacing: -0.6
            }
            Text {
                anchors.baseline: title.baseline
                text: root.allGames.length
                color: Theme.faint
                font.family: Theme.fontDisplay; font.pixelSize: 20; font.weight: Font.Medium
            }
        }
        InputField {
            id: search
            objectName: "search"
            Layout.preferredWidth: Math.min(280, root.available * 0.32)
            iconName: "search"
            clearable: true
            placeholderText: "Buscar jogo"
            Keys.onEscapePressed: { text = ""; root.forceActiveFocus() }
            Keys.onDownPressed: root.moveSelection(0)
        }
        ActionButton {
            text: "Retrospectiva"
            iconName: "spark"
            quiet: true
            reduced: root.reduced
            onClicked: root.wrappedRequested()
        }
        ActionButton {
            text: "Criar jogo"
            iconName: "plus"
            reduced: root.reduced
            onClicked: root.createRequested()
            ToolTip.visible: hovered; ToolTip.text: "Ctrl+N"; ToolTip.delay: 700
        }
    }

    // Loader filter
    Row {
        id: filters
        z: 1
        x: root.gutter; y: header.y + header.height + 18
        spacing: 6
        Repeater {
            model: ["Todos"].concat(Theme.loaders)
            delegate: AbstractButton {
                id: chip
                required property string modelData
                readonly property bool active: root.loaderFilter === modelData
                readonly property int count: root.countFor(modelData)
                height: 32
                implicitWidth: chipRow.implicitWidth + 24
                hoverEnabled: true
                Accessible.name: modelData
                onClicked: root.loaderFilter = modelData
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: chip.active ? Theme.accentSoft : chip.hovered ? "#0AFFFFFF" : "transparent"
                    Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }
                    Rectangle {
                        anchors.fill: parent; anchors.margins: -2
                        radius: parent.radius + 2; color: "transparent"
                        border.width: 2; border.color: Theme.focusRing
                        visible: chip.visualFocus
                    }
                }
                contentItem: Item {
                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text {
                            id: chipLabel
                            text: chip.modelData
                            color: chip.active ? Theme.accentText : chip.hovered ? Theme.secondary : Theme.muted
                            font.family: Theme.font; font.pixelSize: 13
                            font.weight: chip.active ? Font.DemiBold : Font.Medium
                        }
                        Text {
                            text: chip.count
                            color: Theme.faint
                            font.family: Theme.font; font.pixelSize: 12
                            anchors.baseline: chipLabel.baseline
                        }
                    }
                }
            }
        }
    }

    // The grid itself does not clip: lifted covers and the selection glow spill
    // past cell bounds. This frame clips instead, with room on every side.
    Item {
        id: gridFrame
        readonly property real side: 64
        // Starts slightly above the filter row (which sits on top) so the
        // selection halo of the first row is never cut.
        readonly property real overlap: 16
        x: root.gutter - root.gap / 2 - side
        y: filters.y + filters.height - overlap
        width: grid.width + side * 2
        height: root.height - y
        clip: true

        GridView {
            id: grid
            objectName: "gameGrid"
            x: gridFrame.side
            width: root.columns * cellWidth
            height: parent.height
            cellWidth: root.tileWidth + root.gap
            cellHeight: root.tileWidth * 1.5 + 58 + 30
            model: root.gridModel
            // Room above the first row for the hover lift, ring and glow.
            topMargin: 64; bottomMargin: 56
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            reuseItems: false
            ScrollBar.vertical: ThinScrollBar { parent: gridFrame; x: grid.x + grid.width + 4; y: grid.y + 30; height: grid.height - 30 }

            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                // The raised/selected tile draws above its neighbours, glow included.
                z: tile.item && (tile.item.highlighted || tile.item.selected) ? 1 : 0
                width: grid.cellWidth; height: grid.cellHeight
                function focusTile() { if (tile.item) tile.item.forceActiveFocus(Qt.TabFocusReason) }

                Loader {
                    id: tile
                    x: root.gap / 2
                    width: root.tileWidth
                    sourceComponent: cell.modelData.isCreate ? createTile : coverTile
                }
                Component {
                    id: coverTile
                    GameCover {
                        game: cell.modelData
                        width: root.tileWidth
                        selected: root.selectedId === game.id
                        reduced: root.reduced
                        onChosen: root.openRequested(game)
                        onPlayRequested: root.playRequested(game)
                        onEditRequested: root.editRequested(game)
                        onPreviewed: function(active) { root.previewChanged(game.id, active) }
                    }
                }
                Component {
                    id: createTile
                    AbstractButton {
                        id: create
                        width: root.tileWidth
                        height: root.tileWidth * 1.5
                        hoverEnabled: true
                        Accessible.name: "Criar jogo"
                        onClicked: root.createRequested()
                        background: Rectangle {
                            radius: Theme.radiusLg
                            color: create.down ? Theme.fillHover : create.hovered ? "#0FFFFFFF" : "#06FFFFFF"
                            border.width: create.visualFocus ? 2 : 1
                            border.color: create.visualFocus ? Theme.focusRing : create.hovered ? Theme.lineStrong : "#17FFFFFF"
                            Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }
                        }
                        contentItem: Item {
                            Column {
                                anchors.centerIn: parent
                                spacing: 12
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 44; height: 44; radius: 22
                                    color: create.hovered ? Theme.accentSoftHover : Theme.fillHover
                                    Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }
                                    UiIcon { anchors.centerIn: parent; width: 20; height: 20; name: "plus"; color: create.hovered ? Theme.accent : Theme.text }
                                }
                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "Criar jogo"
                                    color: create.hovered ? Theme.text : Theme.secondary
                                    font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Column {
        visible: root.filtering && !root.games.length
        anchors.horizontalCenter: gridFrame.horizontalCenter
        y: gridFrame.y + 100
        spacing: 14
        UiIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "search"; width: 30; height: 30; color: Theme.faint }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Nenhum jogo encontrado"
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 18; font.weight: Font.DemiBold
        }
        ActionButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Limpar filtros"; quiet: true; reduced: root.reduced
            onClicked: root.clearFilters()
        }
    }
}
