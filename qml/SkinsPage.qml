import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Shapes

// The active Microsoft profile's look, as a fitting room: the profile's player (from the
// sidebar, see ProfileLedge) jumps onto the stage on the left and wears whatever is tried on
// in the wardrobe on the right — library skins and the account's capes. Nothing changes on
// the account until "Aplicar visual" (SkinManager, through the official Minecraft services).
Item {
    id: root
    property bool reduced: false
    signal notify(string message)
    signal profileRequested()
    // Something new was tried on: "outfit", "arms" or "cape" (the player reacts to it).
    signal tried(string kind)
    readonly property real gutter: width < 900 ? 32 : 48

    readonly property bool microsoft: launcher.activeProfileId !== "" && launcher.activeProfile.type === "microsoft"
    readonly property string uuid: microsoft ? launcher.activeProfile.uuid : ""
    readonly property bool loaded: skins.uuid === uuid && uuid !== ""
    readonly property var activeSkin: loaded ? (skins.profile.skin || {}) : ({})
    readonly property var capes: loaded ? (skins.profile.capes || []) : []
    readonly property var activeCape: capes.find(function(cape) { return cape.active }) || null

    // What is tried on: a library skin ("" the one in use), its arms ("" its own), a cape
    // (undefined the one in use, null none).
    property string previewId: ""
    property string previewVariant: ""
    property var previewCape: undefined
    readonly property var previewEntry: skins.library.find(function(entry) { return entry.id === root.previewId }) || null
    readonly property string previewSource: previewEntry ? previewEntry.source
        : activeSkin.source || (microsoft ? (auth.skinRevision, auth.skinSource(uuid)) : "")
    readonly property string shownVariant: previewVariant || (previewEntry ? previewEntry.variant : activeSkin.variant) || ""
    readonly property var shownCape: previewCape !== undefined ? previewCape : activeCape
    readonly property bool skinDirty: (previewId !== "" && previewId !== activeSkin.libraryId)
        || (previewVariant !== "" && previewVariant !== activeSkin.variant)
    readonly property bool capeDirty: previewCape !== undefined
        && (previewCape ? previewCape.id : "") !== (activeCape ? activeCape.id : "")
    readonly property bool dirty: skinDirty || capeDirty

    // For the player on the stage.
    readonly property var outfit: ({
        source: previewSource,
        cape: shownCape && shownCape.source ? shownCape.source : "",
        variant: shownVariant
    })
    readonly property rect stageRect: Qt.rect(stage.x + figure.x, stage.y + figure.y, figure.width, figure.height)

    function tryOn(id) {
        const next = id === activeSkin.libraryId ? "" : id
        if (previewId === next) return
        previewId = next
        previewVariant = ""
        tried("outfit")
    }
    function tryCape(cape) {
        if ((cape ? cape.id : "") === (shownCape ? shownCape.id : "")) return
        previewCape = (cape ? cape.id : "") === (activeCape ? activeCape.id : "") ? undefined : cape
        tried("cape")
    }
    function setVariant(variant) {
        if (variant === shownVariant) return
        previewVariant = variant
        tried("arms")
    }
    function revert() {
        previewId = ""
        previewVariant = ""
        previewCape = undefined
        tried("outfit")
    }
    function applyLook() {
        skins.applyLook(uuid, skinDirty ? (previewId || activeSkin.libraryId || "") : "", shownVariant || "classic",
                        capeDirty, previewCape ? previewCape.id : "")
    }
    function load() {
        if (!microsoft) return
        skins.load(uuid)
        previewId = ""
        previewVariant = ""
        previewCape = undefined
    }
    onVisibleChanged: if (visible) load()
    onUuidChanged: if (visible) load()

    Connections {
        target: skins
        function onFailed(message) { if (root.visible) root.notify(message) }
        function onDone(message) { if (root.visible) root.notify(message) }
        function onProfileChanged() {
            // What was just applied is what is in use now.
            if (root.previewId !== "" && root.previewId === root.activeSkin.libraryId) root.previewId = ""
            if (root.previewVariant !== "" && root.previewVariant === root.activeSkin.variant) root.previewVariant = ""
            if (root.previewCape !== undefined && (root.previewCape ? root.previewCape.id : "") === (root.activeCape ? root.activeCape.id : ""))
                root.previewCape = undefined
        }
    }

    FileDialog {
        id: skinFile
        title: qsTr("Importar skin")
        nameFilters: [qsTr("Skins do Minecraft (*.png)")]
        fileMode: FileDialog.OpenFile
        onAccepted: { const id = skins.importSkin(selectedFile); if (id) root.tryOn(id) }
    }

    // ── Header ────────────────────────────────────────────────────────────
    Column {
        id: header
        x: root.gutter; y: 50
        width: root.width - root.gutter * 2
        spacing: 4
        Text {
            text: qsTr("Skins")
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 30; font.weight: Font.DemiBold
            font.letterSpacing: -0.6
        }
        Text {
            text: qsTr("Prove skins e capas à vontade. Nada muda na sua conta até você aplicar.")
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 13
        }
    }

    // ── Without a Microsoft profile ───────────────────────────────────────
    Column {
        visible: !root.microsoft
        anchors.centerIn: parent
        spacing: 14
        width: 360
        UiIcon { anchors.horizontalCenter: parent.horizontalCenter; width: 36; height: 36; name: "shirt"; color: Theme.faint }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Skins e capas ficam na sua conta Microsoft. Escolha um perfil Microsoft para personalizar.")
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 14
        }
        ActionButton {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Escolher perfil")
            onClicked: root.profileRequested()
        }
    }

    // ── The stage: the player stands here (drawn over the page, see ProfileLedge) ──
    Item {
        id: stage
        visible: root.microsoft
        x: root.gutter
        y: header.y + header.height + 12
        width: Math.max(280, root.width - root.gutter * 2 - wardrobe.width - 40)
        height: root.height - y - 28

        // Where the player stands, and the floor under its feet (as SkinView3D frames it).
        Item {
            id: figure
            width: parent.width
            height: parent.height - controls.height - 12
            readonly property real unit: Math.max(1, Math.min(height / 44, width / 26))
            readonly property real floorY: height / 2 + 16 * unit * 40 / 44
        }
        // A soft glow behind the player, and a pool of light on the floor.
        RadialGlow {
            width: Math.min(stage.width * 1.1, figure.unit * 58); height: width
            x: (stage.width - width) / 2
            y: figure.height / 2 - height / 2
            color: "#24E68AA6"
            core: 0
        }
        RadialGlow {
            width: Math.min(stage.width * 0.95, figure.unit * 46); height: width * 0.2
            x: (stage.width - width) / 2
            y: figure.floorY - height / 2
            color: "#4DE68AA6"
            core: 0.1
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: figure.floorY + figure.unit * 2.2
            text: qsTr("Arraste para girar · clique para uma pose")
            color: Theme.faint
            font.family: Theme.font; font.pixelSize: 11
            opacity: 0.8
        }

        // What it wears, and putting it on.
        Column {
            id: controls
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width, 360)
            spacing: 14

            Column {
                width: parent.width
                spacing: 3
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: root.previewEntry ? root.previewEntry.name : qsTr("Seu visual")
                    color: Theme.text
                    font.family: Theme.font; font.pixelSize: 18; font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: skins.busy ? qsTr("Sincronizando com o Minecraft…")
                        : root.dirty ? qsTr("Provando · ainda não aplicado")
                        : root.shownCape ? qsTr("Em uso, com a capa %1").arg(root.shownCape.alias) : qsTr("Em uso")
                    color: root.dirty ? Theme.accentText : Theme.muted
                    font.family: Theme.font; font.pixelSize: 12
                }
            }

            // Classic or slim arms.
            Item {
                width: parent.width; height: 34
                Rectangle {
                    id: variants
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 200; height: 34; radius: 17
                    color: "#0DFFFFFF"
                    Rectangle {
                        width: parent.width / 2 - 4; height: parent.height - 6
                        x: root.shownVariant === "slim" ? parent.width / 2 + 1 : 3; y: 3
                        radius: height / 2
                        color: "#1CFFFFFF"
                        Behavior on x { NumberAnimation { duration: root.reduced ? 0 : Theme.normal; easing.type: Easing.OutCubic } }
                    }
                    Row {
                        anchors.fill: parent
                        Repeater {
                            model: [{ id: "classic", label: qsTr("Braços largos") }, { id: "slim", label: qsTr("Braços finos") }]
                            delegate: AbstractButton {
                                id: variantButton
                                required property var modelData
                                width: variants.width / 2; height: variants.height
                                hoverEnabled: true
                                Accessible.name: modelData.label
                                onClicked: { sfx.play("click"); root.setVariant(modelData.id) }
                                contentItem: Text {
                                    text: variantButton.modelData.label
                                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                    color: (root.shownVariant || "classic") === variantButton.modelData.id ? Theme.text
                                        : variantButton.hovered ? Theme.secondary : Theme.muted
                                    font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
                                }
                            }
                        }
                    }
                }
            }

            // Apply or undo what is tried on.
            Item {
                width: parent.width; height: 42
                Row {
                    anchors.centerIn: parent
                    spacing: 10
                    opacity: root.dirty ? 1 : 0
                    visible: opacity > 0
                    scale: root.dirty ? 1 : 0.94
                    Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.normal } }
                    Behavior on scale { NumberAnimation { duration: root.reduced ? 0 : Theme.slow; easing.type: Easing.OutBack } }
                    ActionButton {
                        quiet: true
                        text: qsTr("Desfazer")
                        enabled: !skins.busy
                        onClicked: root.revert()
                    }
                    ActionButton {
                        primary: true
                        iconName: "check"
                        text: qsTr("Aplicar visual")
                        enabled: !skins.busy
                        onClicked: root.applyLook()
                    }
                }
            }
        }
    }

    // ── The wardrobe ──────────────────────────────────────────────────────
    Item {
        id: wardrobe
        visible: root.microsoft
        anchors.right: parent.right; anchors.rightMargin: root.gutter
        y: header.y + header.height + 18
        width: Math.min(470, Math.max(320, root.width * 0.4))
        height: root.height - y - 24

        readonly property int columns: Math.max(2, Math.floor((width - 12 + 12) / (128 + 12)))
        readonly property real tileWidth: Math.floor((width - 12 - (columns - 1) * 12) / columns)
        readonly property real tileHeight: Math.round(tileWidth * 1.32)

        SegmentTabs {
            id: tabs
            width: parent.width
            reduced: root.reduced
            model: [qsTr("Skins") + "  " + skins.library.length, qsTr("Capas") + "  " + root.capes.length]
        }

        Flickable {
            id: shelf
            anchors.top: tabs.bottom; anchors.topMargin: 16
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            contentHeight: tabs.currentIndex === 0 ? skinGrid.height + 60 : capeGrid.height + 20
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ThinScrollBar {}

            // Skins: the library, and a tile to bring in a new one.
            Flow {
                id: skinGrid
                visible: tabs.currentIndex === 0
                width: shelf.width - 12
                spacing: 12
                Repeater {
                    model: skins.library
                    delegate: Tile {
                        id: skinTile
                        width: wardrobe.tileWidth; height: wardrobe.tileHeight
                        required property var modelData
                        readonly property bool inUse: modelData.id === root.activeSkin.libraryId
                        chosen: root.previewId === "" ? inUse : root.previewId === modelData.id
                        badge: inUse ? qsTr("Em uso") : ""
                        label: modelData.name
                        Accessible.name: modelData.name
                        onClicked: { sfx.play("click"); root.tryOn(modelData.id) }

                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 18 - (skinTile.hovered ? 3 : 0)
                            height: skinTile.height - 64; width: height / 2
                            source: "image://skinfront/" + encodeURIComponent(skinTile.modelData.source) + "|" + skinTile.modelData.variant
                            sourceSize.height: 256
                            smooth: false
                            Behavior on y { NumberAnimation { duration: root.reduced ? 0 : Theme.normal; easing.type: Easing.OutBack } }
                        }
                        ActionButton {
                            anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 7
                            width: 26; height: 26; round: true
                            quiet: true; iconName: "trash"; iconSize: 13
                            visible: (skinTile.hovered || hovered) && !skinTile.inUse
                            Accessible.name: qsTr("Remover da biblioteca")
                            ToolTip.visible: hovered; ToolTip.text: qsTr("Remover da biblioteca"); ToolTip.delay: 400
                            onClicked: {
                                if (root.previewId === skinTile.modelData.id) root.previewId = ""
                                skins.removeSkin(skinTile.modelData.id)
                            }
                        }
                    }
                }
                // Drop a PNG here, or pick one.
                Tile {
                    id: importTile
                    width: wardrobe.tileWidth; height: wardrobe.tileHeight
                    dashed: true
                    chosen: drop.containsDrag
                    Accessible.name: qsTr("Importar skin")
                    onClicked: skinFile.open()
                    Column {
                        anchors.centerIn: parent
                        spacing: 10
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 40; height: 40; radius: 20
                            color: importTile.hovered || drop.containsDrag ? Theme.accentSoft : "#0DFFFFFF"
                            Behavior on color { ColorAnimation { duration: Theme.fast } }
                            UiIcon {
                                anchors.centerIn: parent; width: 18; height: 18; name: "plus"
                                color: importTile.hovered || drop.containsDrag ? Theme.accent : Theme.muted
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("Importar skin")
                            color: Theme.secondary
                            font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("ou solte um PNG")
                            color: Theme.faint
                            font.family: Theme.font; font.pixelSize: 11
                        }
                    }
                }
            }
            ActionButton {
                visible: tabs.currentIndex === 0
                y: skinGrid.height + 16
                quiet: true
                iconName: "restart"
                text: qsTr("Voltar à skin padrão")
                enabled: !skins.busy
                onClicked: skins.resetSkin(root.uuid)
            }

            // Capes the account owns.
            Flow {
                id: capeGrid
                visible: tabs.currentIndex === 1
                width: shelf.width - 12
                spacing: 12
                Text {
                    visible: root.loaded && root.capes.length === 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Essa conta ainda não tem capas.")
                    color: Theme.muted
                    font.family: Theme.font; font.pixelSize: 13
                }
                Repeater {
                    model: root.capes.length > 0 ? [null].concat(root.capes) : []
                    delegate: Tile {
                        id: capeTile
                        width: wardrobe.tileWidth; height: wardrobe.tileHeight
                        required property var modelData
                        readonly property bool inUse: modelData ? modelData.active : root.activeCape === null
                        chosen: (modelData ? modelData.id : "") === (root.shownCape ? root.shownCape.id : "")
                        badge: inUse ? qsTr("Em uso") : ""
                        label: modelData ? modelData.alias : qsTr("Sem capa")
                        Accessible.name: label
                        onClicked: { sfx.play("click"); root.tryCape(modelData) }

                        // The cape's outside, as it hangs.
                        Item {
                            id: capeFace
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 22 - (capeTile.hovered ? 3 : 0)
                            height: capeTile.height - 74; width: height * 10 / 16
                            Behavior on y { NumberAnimation { duration: root.reduced ? 0 : Theme.normal; easing.type: Easing.OutBack } }
                            Repeater {
                                model: capeTile.modelData && capeTile.modelData.source ? [capeTile.modelData.source] : []
                                Image {
                                    required property string modelData
                                    anchors.fill: parent
                                    source: modelData
                                    sourceClipRect: Qt.rect(1, 1, 10, 16)
                                    smooth: false
                                }
                            }
                            // None: an empty hanger.
                            Shape {
                                visible: !capeTile.modelData
                                anchors.fill: parent
                                preferredRendererType: Shape.CurveRenderer
                                ShapePath {
                                    strokeColor: Theme.lineStrong; strokeWidth: 1.5
                                    strokeStyle: ShapePath.DashLine; dashPattern: [3, 3]
                                    fillColor: "transparent"
                                    startX: 0; startY: 0
                                    PathLine { x: capeFace.width; y: 0 }
                                    PathLine { x: capeFace.width; y: capeFace.height }
                                    PathLine { x: 0; y: capeFace.height }
                                    PathLine { x: 0; y: 0 }
                                }
                            }
                            UiIcon {
                                visible: !capeTile.modelData
                                anchors.centerIn: parent; width: 16; height: 16; name: "close"; color: Theme.faint
                            }
                        }
                    }
                }
            }
        }
    }

    DropArea {
        id: drop
        anchors.fill: parent
        enabled: root.microsoft
        keys: ["text/uri-list"]
        onEntered: tabs.currentIndex = 0
        onDropped: function(event) {
            for (const url of event.urls) {
                const id = skins.importSkin(url)
                if (id) root.tryOn(id)
            }
        }
    }

    // A wardrobe tile: a lit, rounded card that lifts on hover; the chosen one is ringed in
    // rose, the one in use carries a badge.
    component Tile: AbstractButton {
        id: tile
        property bool chosen: false
        property bool dashed: false
        property string badge: ""
        property string label: ""
        hoverEnabled: true
        scale: pressed ? 0.96 : 1
        Behavior on scale { NumberAnimation { duration: Theme.fast; easing.type: Easing.OutBack } }
        background: Item {
            SoftGlow {
                anchors.fill: parent
                radius: 16
                blurMax: 28
                color: Theme.accent
                opacity: tile.chosen && !tile.dashed ? 0.28 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.normal } }
            }
            Rectangle {
                anchors.fill: parent
                radius: 16
                visible: !tile.dashed
                gradient: Gradient {
                    GradientStop { position: 0; color: tile.chosen ? "#2EE68AA6" : tile.hovered ? "#17FFFFFF" : "#0EFFFFFF" }
                    GradientStop { position: 1; color: tile.chosen ? "#14E68AA6" : tile.hovered ? "#0AFFFFFF" : "#05FFFFFF" }
                }
                border.width: tile.chosen ? 2 : 0
                border.color: Theme.accent
            }
            Shape {
                anchors.fill: parent
                visible: tile.dashed
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: tile.chosen ? Theme.accent : tile.hovered ? Theme.muted : Theme.lineStrong
                    strokeWidth: 1.5
                    strokeStyle: ShapePath.DashLine; dashPattern: [4, 4]
                    fillColor: tile.chosen ? Theme.accentSoft : "transparent"
                    PathRectangle { x: 1; y: 1; width: tile.width - 2; height: tile.height - 2; radius: 15 }
                }
            }
            Rectangle {
                anchors.fill: parent; anchors.margins: -3
                radius: 19; color: "transparent"
                border.width: 2; border.color: Theme.focusRing
                visible: tile.visualFocus
            }
        }
        Text {
            visible: tile.label !== ""
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.margins: 10; anchors.bottomMargin: 12
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: tile.label
            color: tile.chosen ? Theme.text : Theme.secondary
            font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
        }
        Rectangle {
            visible: tile.badge !== ""
            x: 8; y: 8
            width: badgeText.implicitWidth + 12; height: 18; radius: 9
            color: Theme.accent
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: tile.badge
                color: Theme.accentInk
                font.family: Theme.font; font.pixelSize: 10; font.weight: Font.Bold
            }
        }
    }
}
