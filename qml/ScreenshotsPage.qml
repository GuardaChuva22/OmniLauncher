import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

// Every game's screenshots in one gallery, with a full-screen viewer.
Item {
    id: root
    property bool reduced: false
    signal notify(string message)
    readonly property real gutter: width < 900 ? 32 : 48
    property var all: []
    property string gameFilter: ""
    property int viewing: -1
    readonly property var shown: gameFilter === "" ? all : all.filter(function(shot) { return shot.gameId === gameFilter })
    readonly property var gamesWithShots: {
        const ids = []
        for (const shot of all) if (ids.indexOf(shot.gameId) === -1) ids.push(shot.gameId)
        return launcher.games.filter(function(game) { return ids.indexOf(game.id) !== -1 })
    }

    function refresh() { all = runner.screenshots() }
    function gameTitle(id) {
        const game = launcher.games.find(function(g) { return g.id === id })
        return game ? game.title : qsTr("Jogo removido")
    }
    function when(iso) {
        return new Date(iso).toLocaleString(Qt.locale(), qsTr("d 'de' MMMM yyyy, HH:mm"))
    }
    onVisibleChanged: if (visible) refresh()
    Connections {
        target: runner
        function onGameExited() { if (root.visible) root.refresh() }
    }

    RowLayout {
        id: header
        x: root.gutter; y: 50
        width: root.width - root.gutter * 2
        spacing: 12
        Row {
            spacing: 12
            Layout.fillWidth: true
            Text {
                id: titleText
                text: qsTr("Capturas")
                color: Theme.text
                font.family: Theme.fontDisplay; font.pixelSize: 30; font.weight: Font.DemiBold
                font.letterSpacing: -0.6
            }
            Text {
                anchors.baseline: titleText.baseline
                text: root.shown.length
                color: Theme.faint
                font.family: Theme.fontDisplay; font.pixelSize: 20; font.weight: Font.Medium
            }
        }
    }

    Row {
        id: filters
        x: root.gutter; y: header.y + header.height + 18
        spacing: 6
        visible: root.all.length > 0
        Repeater {
            model: [{ id: "", title: qsTr("Todos") }].concat(root.gamesWithShots.map(function(g) { return { id: g.id, title: g.title } }))
            delegate: AbstractButton {
                id: chip
                required property var modelData
                readonly property bool active: root.gameFilter === modelData.id
                height: 32
                implicitWidth: chipLabel.implicitWidth + 24
                hoverEnabled: true
                onClicked: root.gameFilter = modelData.id
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: chip.active ? Theme.accentSoft : chip.hovered ? "#0AFFFFFF" : "transparent"
                }
                contentItem: Text {
                    id: chipLabel
                    text: chip.modelData.title
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: chip.active ? Theme.accentText : Theme.muted
                    font.family: Theme.font; font.pixelSize: 13
                    font.weight: chip.active ? Font.DemiBold : Font.Medium
                }
            }
        }
    }

    GridView {
        id: grid
        x: root.gutter - 8
        y: filters.y + filters.height + 16
        width: root.width - root.gutter * 2 + 16
        height: root.height - y
        readonly property int columns: Math.max(2, Math.floor(width / 320))
        cellWidth: Math.floor(width / columns)
        cellHeight: Math.round((cellWidth - 16) * 9 / 16) + 16
        clip: true
        bottomMargin: 32
        model: root.shown
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ThinScrollBar {}
        delegate: Item {
            id: cell
            required property var modelData
            required property int index
            width: grid.cellWidth; height: grid.cellHeight
            Item {
                id: tile
                anchors.fill: parent; anchors.margins: 8
                scale: hover.hovered && !root.reduced ? 1.02 : 1
                Behavior on scale { NumberAnimation { duration: root.reduced ? 0 : Theme.normal; easing.type: Easing.OutCubic } }
                Image {
                    id: shot
                    anchors.fill: parent
                    source: cell.modelData.url
                    sourceSize: Qt.size(560, 315)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    visible: false
                }
                Rectangle { id: shotMask; anchors.fill: parent; radius: Theme.radiusLg; visible: false; layer.enabled: true }
                Rectangle { anchors.fill: parent; radius: Theme.radiusLg; color: Theme.fill }
                MultiEffect {
                    anchors.fill: parent; source: shot; maskEnabled: true; maskSource: shotMask
                    opacity: shot.status === Image.Ready ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.normal } }
                }
                Rectangle {
                    anchors.fill: parent; radius: Theme.radiusLg
                    opacity: hover.hovered ? 1 : 0
                    gradient: Gradient {
                        GradientStop { position: 0.55; color: "#00000000" }
                        GradientStop { position: 1; color: "#B3000000" }
                    }
                    Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.fast } }
                    Column {
                        anchors.left: parent.left; anchors.bottom: parent.bottom; anchors.margins: 12
                        spacing: 2
                        Text { text: root.gameTitle(cell.modelData.gameId); color: Theme.text; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold }
                        Text { text: root.when(cell.modelData.takenAt); color: Theme.secondary; font.family: Theme.font; font.pixelSize: 11 }
                    }
                }
                Rectangle { anchors.fill: parent; radius: Theme.radiusLg; color: "transparent"; border.width: 1; border.color: hover.hovered ? "#33FFFFFF" : "#14FFFFFF" }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.viewing = cell.index }
            }
        }
    }

    Column {
        visible: root.all.length === 0
        anchors.centerIn: parent
        spacing: 10
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 60; height: 60; radius: 30
            color: Theme.fill; border.width: 1; border.color: Theme.line
            UiIcon { anchors.centerIn: parent; width: 26; height: 26; name: "image"; color: Theme.muted }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Nenhuma captura ainda")
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 18; font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Aperte F2 dentro do jogo e elas aparecem aqui.")
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 13
        }
    }

    // Viewer
    FocusScope {
        id: viewer
        anchors.fill: parent
        visible: root.viewing >= 0 && root.viewing < root.shown.length
        focus: visible
        readonly property var current: visible ? root.shown[root.viewing] : null
        onVisibleChanged: if (visible) forceActiveFocus()
        Keys.onEscapePressed: root.viewing = -1
        Keys.onLeftPressed: if (root.viewing > 0) root.viewing--
        Keys.onRightPressed: if (root.viewing < root.shown.length - 1) root.viewing++
        Rectangle {
            anchors.fill: parent
            color: "#F20B090C"
            MouseArea { anchors.fill: parent; onClicked: root.viewing = -1 }
        }
        Image {
            anchors.fill: parent
            anchors.margins: 64
            anchors.bottomMargin: 96
            source: viewer.current ? viewer.current.url : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            smooth: true
            mipmap: true
        }
        ActionButton {
            visible: root.viewing > 0
            anchors.left: parent.left; anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            width: 44; height: 44; round: true; iconName: "back"; reduced: root.reduced
            onClicked: root.viewing--
        }
        ActionButton {
            visible: root.viewing < root.shown.length - 1
            anchors.right: parent.right; anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            width: 44; height: 44; round: true; iconName: "chevron"; reduced: root.reduced
            onClicked: root.viewing++
        }
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 24
            spacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: viewer.current ? root.gameTitle(viewer.current.gameId) : ""; color: Theme.text; font.family: Theme.font; font.pixelSize: 15; font.weight: Font.DemiBold }
                Text { text: viewer.current ? root.when(viewer.current.takenAt) + "  ·  " + (root.viewing + 1) + qsTr(" de ") + root.shown.length : ""; color: Theme.muted; font.family: Theme.font; font.pixelSize: 12 }
            }
            ActionButton {
                text: qsTr("Copiar"); iconName: "copy"; reduced: root.reduced
                onClicked: root.notify(runner.copyImage(viewer.current.path) ? qsTr("Imagem copiada.") : qsTr("Não foi possível copiar a imagem."))
            }
            ActionButton {
                text: qsTr("Pasta"); iconName: "folder"; quiet: true; reduced: root.reduced
                onClicked: runner.openFile(viewer.current.path.substring(0, viewer.current.path.lastIndexOf("/")))
            }
            ActionButton {
                text: qsTr("Excluir"); iconName: "trash"; danger: true; quiet: true; reduced: root.reduced
                onClicked: {
                    if (runner.trashFile(viewer.current.path)) {
                        const index = root.viewing
                        root.refresh()
                        root.viewing = Math.min(index, root.shown.length - 1)
                        root.notify(qsTr("Captura movida para a Lixeira."))
                    }
                }
            }
            ActionButton {
                iconName: "close"; quiet: true; round: true; width: 40; height: 40; reduced: root.reduced
                Accessible.name: "Fechar"
                onClicked: root.viewing = -1
            }
        }
    }
}
