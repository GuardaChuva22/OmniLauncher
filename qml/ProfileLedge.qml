import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// The active profile at the foot of the sidebar: its player in 3D (SkinView3D) leaning
// on the profile card as on a wall, head turned to the cursor. It climbs in once the
// launcher opens, waving; a click (on it or the card) opens the profile menu with a
// little hop; another profile makes it lose its grip and fall, and the next one climbs
// up in its place. Downloads and Settings sit on the card, on the right.
// On the skins page it climbs onto the card and jumps out to the page's stage (it is drawn
// in `stageLayer`, over the whole window), wearing what the page tries on; it jumps back when
// the page closes.
Item {
    id: root
    property bool reduced: false
    property bool menuOpen: false
    property int page: 0
    property var activeDownloads: []
    property real downloadsProgress: -1
    signal profileClicked()
    signal pageRequested(int index)
    readonly property alias card: card

    // The skins page: where the player stands there (in `stageLayer`'s coordinates), whether it
    // should, and what it wears ({source, cape, variant}).
    property Item stageLayer: null
    property rect stageRect: Qt.rect(0, 0, 0, 0)
    property bool onStage: false
    property var outfit: null

    readonly property bool hasProfile: launcher.activeProfileId !== ""
    readonly property string wantedSkin: hasProfile && launcher.activeProfile.type === "microsoft"
        ? (auth.skinRevision, auth.skinSource(launcher.activeProfile.uuid)) : ""
    // The profile drawn now; it trails the active one while the old player falls.
    property var shown: ({ id: "", name: "", type: "" })
    // A player is up on the ledge (or on the way up).
    property bool present: false
    property bool started: false
    property string shownSkin: ""
    // Off the ledge (on its way to the stage, on it, or on its way back); wearing the outfit;
    // standing on the stage, done jumping.
    property bool away: false
    property bool dressed: false
    property bool arrived: false
    // 0 on the ledge, 1 on the stage.
    property real travel: 0

    readonly property int cardHeight: 64
    readonly property real unit: 7.5
    // How far the player's item reaches down over the card (the hands over its edge).
    readonly property int overhang: 30
    readonly property bool busy: enterAnim.running || landAnim.running || waveAnim.running || lookAroundAnim.running
        || fallAnim.running || hopAnim.running || spinAnim.running || slapAnim.running || idleAnim.running
        || departAnim.running || returnAnim.running
    readonly property bool transitioning: enterAnim.running || landAnim.running || fallAnim.running || away

    implicitHeight: 182 + cardHeight

    readonly property var restPose: ({
        rise: 0, advance: 0, tilt: 0, turn: 0, rightLegPitch: 0, leftLegPitch: 0, bodyPitch: 12, bodyRoll: 0, headPitch: 0, headYaw: 0, headRoll: 0,
        rightArmPitch: 82, rightArmYaw: 6, rightArmRoll: 0, leftArmPitch: 82, leftArmYaw: 6, leftArmRoll: 0
    })
    // Below the ledge, arms up: ready to spring in.
    readonly property var hiddenPose: ({
        rise: -36, advance: 0, tilt: 0, turn: 0, rightLegPitch: 0, leftLegPitch: 0, bodyPitch: 0, bodyRoll: 0, headPitch: -14, headYaw: 0, headRoll: 0,
        rightArmPitch: 172, rightArmYaw: 0, rightArmRoll: 10, leftArmPitch: 172, leftArmYaw: 0, leftArmRoll: 10
    })

    function setPose(pose) { for (const key in pose) player[key] = pose[key] }
    function stopAll() {
        for (const animation of [enterAnim, landAnim, waveAnim, lookAroundAnim, fallAnim, hopAnim, spinAnim, slapAnim, idleAnim, pressAnim, releaseAnim,
                                 hoverAnim, idleDrum, idleLook, idleStretch, departAnim, returnAnim, dropIn])
            animation.stop()
        moves.stop()
    }

    // Draw the active profile.
    function apply() {
        const changed = shown.id !== launcher.activeProfileId
        shown = hasProfile ? { id: launcher.activeProfileId, name: launcher.activeProfile.name, type: launcher.activeProfile.type }
                           : { id: "", name: "", type: "" }
        shownSkin = wantedSkin
        if (changed && !reduced) nameSwap.restart()
    }
    function arrive(greet) {
        apply()
        present = hasProfile
        if (!hasProfile) return
        if (reduced) { setPose(restPose); player.follow = 1; return }
        landAnim.greet = greet
        enterAnim.start()
    }
    // Once the launcher is on screen.
    function start() {
        if (started) return
        started = true
        if (onStage) { apply(); present = hasProfile; syncStage() }
        else arrive(true)
    }
    function sync() {
        if (!started || away || launcher.activeProfileId === shown.id) { apply(); return }
        if (fallAnim.running) return // it lands on whatever is active by then
        if (reduced) { stopAll(); arrive(false); return }
        stopAll()
        if (present) fallAnim.start()
        else arrive(false)
    }
    onWantedSkinChanged: sync()
    Connections {
        target: launcher
        function onActiveProfileChanged() { root.sync() }
    }
    Component.onCompleted: { setPose(hiddenPose); apply() }

    // Off to the stage, or back to the ledge. Without the ledge on screen (a compact
    // sidebar) or with reduced motion, it is simply there.
    function syncStage() {
        if (!started || !stageLayer) return
        const want = onStage && hasProfile
        const going = departAnim.running || dropIn.running || (away && !returnAnim.running)
        if (want === going) return
        stopAll()
        if (want) {
            if (reduced || !visible) {
                away = true; dressed = true; travel = 1
                player.wall = false
                setPose(moves.standPose); player.turn = 0
                if (reduced) { arrived = true; player.follow = 1 }
                else dropIn.start()
                return
            }
            departAnim.start()
        } else {
            if (reduced || !visible) {
                arrived = false; dressed = false; away = false; travel = 0
                player.wall = true
                arrive(false)
                return
            }
            returnAnim.start()
        }
    }
    onOnStageChanged: syncStage()
    onStageLayerChanged: syncStage()
    // The page tried something on: "arms", "outfit" or "cape".
    function react(kind) { if (arrived) moves.react(kind) }

    PlayerMoves {
        id: moves
        player: player
        reduced: root.reduced
    }
    // Now and then on the stage, something to pass the time.
    Timer {
        interval: 9000
        repeat: true
        running: root.arrived && !root.reduced && Qt.application.state === Qt.ApplicationActive
        onTriggered: { interval = 7000 + Math.random() * 8000; moves.idle() }
    }

    function poke() {
        if (transitioning || !present || reduced) return
        stopAll()
        const pick = Math.random()
        if (pick < 0.5) hopAnim.start()
        else if (pick < 0.75) spinAnim.start()
        else slapAnim.start()
    }

    // ── The ledge: card and player, dipping a little under the player's weight ──
    Item {
        id: ledge
        anchors.fill: parent
        transform: Translate { id: dip }

        Button {
            id: card
            anchors.bottom: parent.bottom
            width: parent.width; height: root.cardHeight
            hoverEnabled: true
            padding: 0
            Accessible.name: root.hasProfile ? "Perfis, " + launcher.activeProfile.name : qsTr("Perfis, escolher Perfil")
            onClicked: { root.profileClicked(); root.poke() }
            // A slab seen from a little above: its top face (where the arms rest) over
            // the front, which holds the name and the buttons.
            background: Item {
                readonly property real band: player.ledge - player.ledgeBack
                Rectangle {
                    y: -parent.band
                    width: parent.width; height: parent.height + parent.band
                    radius: Theme.radiusLg
                    color: root.menuOpen ? "#17FFFFFF" : card.hovered || hit.containsMouse ? "#10FFFFFF" : "#0AFFFFFF"
                    border.width: 1; border.color: Theme.line
                    Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }
                    // The top face, lit from above.
                    Item {
                        x: 1; y: 1
                        width: parent.width - 2; height: parent.parent.band - 1
                        clip: true
                        Rectangle {
                            width: parent.width; height: parent.height + Theme.radiusLg * 2
                            radius: Theme.radiusLg - 1
                            gradient: Gradient {
                                GradientStop { position: 0; color: "#0CFFFFFF" }
                                GradientStop { position: 0.4; color: "#14FFFFFF" }
                            }
                        }
                    }
                }
                // The lit front edge.
                Rectangle {
                    x: 1; width: parent.width - 2
                    height: 1
                    color: "#33FFFFFF"
                }
                Rectangle {
                    x: -2; y: -parent.band - 2
                    width: parent.width + 4; height: parent.height + parent.band + 4
                    radius: Theme.radiusLg + 2; color: "transparent"
                    border.width: 2; border.color: Theme.focusRing
                    visible: card.visualFocus
                }
            }
            contentItem: Item {
                Column {
                    id: names
                    anchors.left: parent.left; anchors.leftMargin: 16
                    anchors.right: tools.left; anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 5 + nameShift
                    property real nameShift: 0
                    spacing: 2
                    Row {
                        width: parent.width
                        spacing: 6
                        Text {
                            id: nameText
                            width: Math.min(implicitWidth, parent.width - 12)
                            text: root.shown.id !== "" ? root.shown.name : qsTr("Escolher Perfil")
                            color: Theme.text
                            font.family: Theme.font; font.pixelSize: 13; font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            visible: root.shown.id !== ""
                            anchors.verticalCenter: nameText.verticalCenter
                            width: 6; height: 6; radius: 3
                            color: Theme.success
                        }
                    }
                    Text {
                        width: parent.width
                        text: root.shown.id === "" ? qsTr("Nenhum perfil ativo")
                            : root.shown.type === "microsoft" ? qsTr("Conta Microsoft") : qsTr("Perfil offline")
                        color: Theme.muted
                        font.family: Theme.font; font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
                Row {
                    id: tools
                    anchors.right: parent.right; anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 4
                    spacing: 4
                    LedgeButton {
                        id: downloadsButton
                        iconName: "download"
                        label: qsTr("Downloads")
                        active: root.page === 4
                        progress: root.activeDownloads.length === 0 ? -2 : root.downloadsProgress
                        onClicked: root.pageRequested(4)
                        DownloadsPeek { downloads: root.activeDownloads }
                    }
                    LedgeButton {
                        iconName: "settings"
                        label: qsTr("Configurações")
                        active: root.page === 2
                        onClicked: root.pageRequested(2)
                    }
                }
            }
        }

        // The card's top face (drawn by the card): shadows under the forearms.
        Item {
            id: topFace
            width: parent.width
            y: player.restTop + player.ledgeBack
            height: player.ledge - player.ledgeBack + 1
            Repeater {
                model: [-1, 1]
                SoftGlow {
                    required property int modelData
                    readonly property real arm: modelData < 0 ? player.rightArmPitch : player.leftArmPitch
                    width: 4.5 * root.unit; height: topFace.height * 0.7
                    x: topFace.width / 2 + modelData * 6.2 * root.unit - width / 2
                    y: topFace.height * 0.3
                    radius: height / 2
                    blurMax: 8
                    color: "black"
                    opacity: 0.55 * Math.max(0, 1 - Math.abs(player.rise) / 2) * Math.max(0, 1 - Math.abs(arm - 82) / 30)
                }
            }
        }

        // A rose halo behind the player while it is pointed at.
        SoftGlow {
            width: 92; height: 92
            x: player.headCenter.x - width / 2
            y: player.restTop + player.headCenter.y - height / 2 + 14
            radius: 46
            blurMax: 40
            color: Theme.accent
            opacity: root.present && !root.away && (hit.containsMouse || root.menuOpen) ? 0.22 : 0
            Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.slow } }
        }

        // Its shadow on the stage's floor, fainter and smaller as it leaves the floor.
        RadialGlow {
            id: floorShadow
            parent: root.stageLayer || ledge
            readonly property real shrink: Math.max(0.55, 1 - Math.max(0, player.rise) / 12)
            width: 24 * player.groundScale * shrink; height: width * 0.24
            x: player.x + player.width / 2 - width / 2
            y: player.y + player.ground - height / 2
            color: "#E6000000"
            core: 0.2
            visible: root.away && opacity > 0
            opacity: Math.max(0, Math.min(1, (root.travel - 0.75) / 0.25)) * Math.max(0.35, 1 - Math.max(0, player.rise) / 8)
        }

        SkinView3D {
            id: player
            parent: root.stageLayer || ledge
            z: 1
            // Room above the ledge, to stand on it.
            readonly property real headroom: 170
            readonly property real ledgeHeight: root.height - root.cardHeight + root.overhang + headroom
            // Where the ledge is in `stageLayer` (which covers the sidebar's parent).
            readonly property point home: Qt.point(root.parent ? root.parent.x + root.x : root.x,
                                                   root.parent ? root.parent.y + root.y : root.y)
            // Its top in the ledge's own coordinates, while on it.
            readonly property real restTop: -headroom
            readonly property real stageUnit: Math.max(1, Math.min(root.stageRect.height / 44, root.stageRect.width / 26))
            readonly property real arc: Math.sin(Math.PI * root.travel) * 160
            function mix(a, b) { return a + (b - a) * root.travel }

            x: root.stageLayer ? mix(home.x, root.stageRect.x) : 0
            y: (root.stageLayer ? mix(home.y - headroom, root.stageRect.y) : -headroom) - arc
            width: mix(root.width, root.stageRect.width)
            height: mix(ledgeHeight, root.stageRect.height)
            ledge: ledgeHeight - root.overhang
            unit: mix(root.unit, stageUnit)
            stand: root.travel * root.travel * (3 - 2 * root.travel)
            transform: Translate { y: dip.y * (1 - root.travel) }
            visible: root.away || (root.visible && (root.shown.id !== "" || root.busy))
            name: root.shown.name
            source: root.dressed && root.outfit ? root.outfit.source : root.shownSkin
            cape: root.dressed && root.outfit ? root.outfit.cape : ""
            variant: root.dressed && root.outfit ? root.outfit.variant : ""

            MouseArea {
                id: hit
                readonly property real reach: 6.5 * root.unit
                x: player.headCenter.x - reach
                y: Math.max(0, player.headCenter.y - reach)
                width: reach * 2
                height: Math.max(0, player.ledge - y)
                enabled: root.present && !root.away
                visible: enabled
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onPressed: if (!root.transitioning && !root.reduced) { root.stopAll(); pressAnim.start() }
                onReleased: if (!containsMouse && !root.transitioning && !root.reduced) releaseAnim.start()
                onClicked: {
                    sfx.play("click")
                    root.profileClicked()
                    root.poke()
                }
                onContainsMouseChanged: {
                    if (containsMouse) sfx.play("hover")
                    if (root.busy || root.reduced || !root.present) return
                    hoverAnim.stop()
                    hoverAnim.lean = containsMouse ? 16 : 12
                    hoverAnim.roll = containsMouse ? 7 : 0
                    hoverAnim.start()
                }
            }

            // On the stage: drag to turn it, double-click to face you again, click for a move.
            MouseArea {
                readonly property real s: player.groundScale
                x: player.width / 2 - 12 * s
                y: player.ground - 34 * s
                width: 24 * s; height: 36 * s
                enabled: root.arrived
                visible: enabled
                cursorShape: pressed && dragged ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property real lastX: 0
                property real startX: 0
                property bool dragged: false
                onPressed: function(mouse) { lastX = mouse.x; startX = mouse.x; dragged = false }
                onPositionChanged: function(mouse) {
                    if (!dragged && Math.abs(mouse.x - startX) > 4) {
                        dragged = true
                        moves.stop()
                        followBack.stop()
                        player.follow = 0
                    }
                    if (dragged) { player.turn += (mouse.x - lastX) * 0.8; lastX = mouse.x }
                }
                onReleased: if (dragged) followBack.restart()
                onClicked: if (!dragged && !moves.busy) { sfx.play("click"); moves.react(Math.random() < 0.5 ? "outfit" : "arms") }
                onDoubleClicked: moves.settle()
            }
        }
    }

    // The head goes back to following the cursor after a drag.
    NumberAnimation { id: followBack; target: player; property: "follow"; to: 1; duration: 600; easing.type: Easing.InOutSine }

    // ── Choreography ──────────────────────────────────────────────────────
    component Tween: NumberAnimation { duration: 220; easing.type: Easing.OutCubic }

    SequentialAnimation {
        id: nameSwap
        ParallelAnimation {
            NumberAnimation { target: names; property: "opacity"; from: 0; to: 1; duration: 320; easing.type: Easing.OutCubic }
            NumberAnimation { target: names; property: "nameShift"; from: 8; to: 0; duration: 420; easing.type: Easing.OutBack }
        }
    }

    // The card sinks a little and springs back: the player landed on it.
    SequentialAnimation {
        id: thump
        property real depth: 2.5
        NumberAnimation { target: dip; property: "y"; to: thump.depth; duration: 70; easing.type: Easing.OutQuad }
        NumberAnimation { target: dip; property: "y"; to: 0; duration: 420; easing.type: Easing.OutElastic; easing.amplitude: 1.2; easing.period: 0.4 }
    }
    function land(depth) { thump.depth = depth; thump.restart() }

    // Springs up from behind the card, arms up…
    SequentialAnimation {
        id: enterAnim
        ScriptAction { script: { root.setPose(root.hiddenPose); player.follow = 0 } }
        PauseAnimation { duration: 90 }
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 3.4; duration: 430; easing.type: Easing.OutCubic }
            Tween { target: player; property: "bodyPitch"; to: -4; duration: 430 }
            Tween { target: player; property: "headPitch"; to: -12; duration: 430 }
            Tween { target: player; property: "rightArmRoll"; to: 22; duration: 430 }
            Tween { target: player; property: "leftArmRoll"; to: 22; duration: 430 }
        }
        PauseAnimation { duration: 30 }
        ScriptAction { script: landAnim.start() }
    }
    // …and drops onto it, slapping both hands down.
    SequentialAnimation {
        id: landAnim
        property bool greet: true
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 0; duration: 420; easing.type: Easing.OutBack; easing.overshoot: 2.6 }
            Tween { target: player; property: "rightArmPitch"; to: 82; duration: 210; easing.type: Easing.InQuad }
            Tween { target: player; property: "leftArmPitch"; to: 82; duration: 210; easing.type: Easing.InQuad }
            Tween { target: player; property: "rightArmRoll"; to: 0; duration: 210 }
            Tween { target: player; property: "leftArmRoll"; to: 0; duration: 210 }
            Tween { target: player; property: "rightArmYaw"; to: 6; duration: 210 }
            Tween { target: player; property: "leftArmYaw"; to: 6; duration: 210 }
            Tween { target: player; property: "bodyPitch"; to: 12; duration: 420; easing.type: Easing.OutBack; easing.overshoot: 2 }
            SequentialAnimation {
                Tween { target: player; property: "headPitch"; to: 12; duration: 210; easing.type: Easing.InQuad }
                Tween { target: player; property: "headPitch"; to: 0; duration: 320; easing.type: Easing.OutBack }
            }
            SequentialAnimation {
                PauseAnimation { duration: 200 }
                ScriptAction { script: root.land(3) }
            }
        }
        ScriptAction { script: landAnim.greet ? waveAnim.start() : lookAroundAnim.start() }
    }
    // Hello!
    SequentialAnimation {
        id: waveAnim
        ParallelAnimation {
            Tween { target: player; property: "leftArmPitch"; to: 158; duration: 300; easing.type: Easing.OutBack }
            Tween { target: player; property: "leftArmYaw"; to: 0; duration: 300 }
            Tween { target: player; property: "headRoll"; to: -9; duration: 300 }
            Tween { target: player; property: "bodyRoll"; to: -3; duration: 300 }
        }
        SequentialAnimation {
            loops: 2
            Tween { target: player; property: "leftArmRoll"; to: 30; duration: 150; easing.type: Easing.InOutSine }
            Tween { target: player; property: "leftArmRoll"; to: -4; duration: 150; easing.type: Easing.InOutSine }
        }
        ParallelAnimation {
            Tween { target: player; property: "leftArmPitch"; to: 82; duration: 230; easing.type: Easing.InQuad }
            Tween { target: player; property: "leftArmRoll"; to: 0; duration: 230 }
            Tween { target: player; property: "leftArmYaw"; to: 6; duration: 230 }
            Tween { target: player; property: "headRoll"; to: 0; duration: 300 }
            Tween { target: player; property: "bodyRoll"; to: 0; duration: 300 }
            SequentialAnimation {
                PauseAnimation { duration: 220 }
                ScriptAction { script: root.land(1.2) }
            }
        }
        Tween { target: player; property: "follow"; to: 1; duration: 600; easing.type: Easing.InOutSine }
    }
    // A new player looks around for the one that fell.
    SequentialAnimation {
        id: lookAroundAnim
        Tween { target: player; property: "headYaw"; to: -38; duration: 260; easing.type: Easing.InOutSine }
        PauseAnimation { duration: 200 }
        Tween { target: player; property: "headYaw"; to: 34; duration: 380; easing.type: Easing.InOutSine }
        ParallelAnimation {
            Tween { target: player; property: "headPitch"; to: 16; duration: 260 }
            Tween { target: player; property: "headRoll"; to: 8; duration: 260 }
        }
        PauseAnimation { duration: 260 }
        ParallelAnimation {
            Tween { target: player; property: "headYaw"; to: 0; duration: 360; easing.type: Easing.InOutSine }
            Tween { target: player; property: "headPitch"; to: 0; duration: 360 }
            Tween { target: player; property: "headRoll"; to: 0; duration: 360 }
            Tween { target: player; property: "follow"; to: 1; duration: 600; easing.type: Easing.InOutSine }
        }
    }

    // Loses its grip: a hop of surprise, a flail, and down it goes.
    SequentialAnimation {
        id: fallAnim
        ParallelAnimation {
            Tween { target: player; property: "follow"; to: 0; duration: 140 }
            Tween { target: player; property: "rise"; to: 1.6; duration: 170; easing.type: Easing.OutQuad }
            Tween { target: player; property: "rightArmPitch"; to: 128; duration: 170 }
            Tween { target: player; property: "leftArmPitch"; to: 112; duration: 170 }
            Tween { target: player; property: "headPitch"; to: -24; duration: 170 }
            Tween { target: player; property: "headYaw"; to: 0; duration: 170 }
            Tween { target: player; property: "headRoll"; to: 0; duration: 170 }
            Tween { target: player; property: "bodyPitch"; to: 2; duration: 170 }
        }
        ParallelAnimation {
            SequentialAnimation {
                loops: 2
                Tween { target: player; property: "rightArmRoll"; to: 38; duration: 85; easing.type: Easing.InOutSine }
                Tween { target: player; property: "rightArmRoll"; to: -6; duration: 85; easing.type: Easing.InOutSine }
            }
            SequentialAnimation {
                loops: 2
                Tween { target: player; property: "leftArmRoll"; to: -6; duration: 85; easing.type: Easing.InOutSine }
                Tween { target: player; property: "leftArmRoll"; to: 38; duration: 85; easing.type: Easing.InOutSine }
            }
            Tween { target: player; property: "headPitch"; to: 26; duration: 300; easing.type: Easing.OutBack }
            Tween { target: player; property: "rise"; to: 1.9; duration: 340; easing.type: Easing.InOutSine }
        }
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: -36; duration: 470; easing.type: Easing.InCubic }
            Tween { target: player; property: "tilt"; to: -18; duration: 470; easing.type: Easing.InQuad }
            Tween { target: player; property: "rightArmPitch"; to: 172; duration: 260 }
            Tween { target: player; property: "leftArmPitch"; to: 172; duration: 260 }
            Tween { target: player; property: "rightArmRoll"; to: 24; duration: 260 }
            Tween { target: player; property: "leftArmRoll"; to: 24; duration: 260 }
            Tween { target: player; property: "headPitch"; to: -30; duration: 260 }
        }
        ScriptAction { script: { root.present = false; root.arrive(false) } }
    }

    // To the stage: a crouch, a climb onto the card, a look at the stage, and a leap with a
    // spin, landing on its feet.
    SequentialAnimation {
        id: departAnim
        ScriptAction { script: { root.away = true; root.dressed = true; player.wall = true; root.arrived = false } }
        ParallelAnimation {
            Tween { target: player; property: "follow"; to: 0; duration: 140 }
            Tween { target: player; property: "rise"; to: -1.6; duration: 150; easing.type: Easing.OutQuad }
            Tween { target: player; property: "bodyPitch"; to: 20; duration: 150 }
            Tween { target: player; property: "headPitch"; to: 12; duration: 150 }
            Tween { target: player; properties: "headYaw,headRoll,bodyRoll,tilt"; to: 0; duration: 150 }
            Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 74; duration: 150 }
        }
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 17; duration: 440; easing.type: Easing.OutCubic }
            Tween { target: player; property: "advance"; to: 6; duration: 440; easing.type: Easing.InOutSine }
            Tween { target: player; property: "bodyPitch"; to: 0; duration: 440 }
            Tween { target: player; property: "headPitch"; to: -6; duration: 440 }
            Tween { target: player; properties: "rightArmYaw,leftArmYaw"; to: 0; duration: 300 }
            Tween { target: player; properties: "rightArmRoll,leftArmRoll"; to: -3; duration: 300 }
            SequentialAnimation {
                Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 24; duration: 220 }
                Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 0; duration: 220 }
            }
            SequentialAnimation {
                PauseAnimation { duration: 140 }
                Tween { target: player; property: "rightLegPitch"; to: 70; duration: 150; easing.type: Easing.OutQuad }
                Tween { target: player; property: "rightLegPitch"; to: 0; duration: 150; easing.type: Easing.InQuad }
            }
        }
        ScriptAction { script: root.land(2.4) }
        // Standing on the card, it looks over at the stage and crouches to jump.
        ParallelAnimation {
            Tween { target: player; property: "turn"; to: 28; duration: 240 }
            Tween { target: player; property: "headYaw"; to: 16; duration: 240 }
            Tween { target: player; property: "headPitch"; to: -8; duration: 240 }
        }
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 15.8; duration: 150; easing.type: Easing.OutQuad }
            Tween { target: player; property: "bodyPitch"; to: 12; duration: 150 }
            Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: -34; duration: 150 }
        }
        ScriptAction { script: player.wall = false }
        ParallelAnimation {
            NumberAnimation { target: root; property: "travel"; to: 1; duration: 880; easing.type: Easing.InOutSine }
            Tween { target: player; property: "rise"; to: 0; duration: 880; easing.type: Easing.InOutSine }
            Tween { target: player; property: "advance"; to: 0; duration: 880 }
            Tween { target: player; property: "turn"; to: 360; duration: 880; easing.type: Easing.InOutCubic }
            Tween { target: player; property: "headYaw"; to: 0; duration: 300 }
            Tween { target: player; property: "headPitch"; to: -12; duration: 300 }
            Tween { target: player; property: "bodyPitch"; to: 0; duration: 300 }
            SequentialAnimation {
                Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 165; duration: 260; easing.type: Easing.OutQuad }
                PauseAnimation { duration: 300 }
                Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 30; duration: 320; easing.type: Easing.InQuad }
            }
            SequentialAnimation {
                Tween { target: player; properties: "rightArmRoll,leftArmRoll"; to: 22; duration: 260 }
                PauseAnimation { duration: 300 }
                Tween { target: player; properties: "rightArmRoll,leftArmRoll"; to: -30; duration: 320 }
            }
            SequentialAnimation {
                Tween { target: player; property: "rightLegPitch"; to: 42; duration: 260 }
                PauseAnimation { duration: 300 }
                Tween { target: player; property: "rightLegPitch"; to: 0; duration: 320 }
            }
            SequentialAnimation {
                Tween { target: player; property: "leftLegPitch"; to: -24; duration: 260 }
                PauseAnimation { duration: 300 }
                Tween { target: player; property: "leftLegPitch"; to: 0; duration: 320 }
            }
            Tween { target: player; property: "capeSwing"; to: 46; duration: 500 }
        }
        ScriptAction { script: player.turn = 0 }
        // Lands: knees give, then up, arms out.
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: -1.5; duration: 90; easing.type: Easing.OutQuad }
            Tween { target: player; property: "bodyPitch"; to: 12; duration: 90 }
            Tween { target: player; property: "headPitch"; to: 10; duration: 90 }
        }
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 0; duration: 400; easing.type: Easing.OutBack; easing.overshoot: 2 }
            Tween { target: player; property: "bodyPitch"; to: 0; duration: 400; easing.type: Easing.OutBack }
            Tween { target: player; property: "headPitch"; to: -4; duration: 400 }
            Tween { target: player; property: "capeSwing"; to: 9; duration: 520; easing.type: Easing.OutBack }
            Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 0; duration: 400; easing.type: Easing.OutBack }
            Tween { target: player; properties: "rightArmRoll,leftArmRoll"; to: -3; duration: 520; easing.type: Easing.OutBack }
        }
        ScriptAction { script: { root.arrived = true; moves.settle() } }
    }
    // Back to the ledge: a leap onto the card, and a hop down behind it to lean on it again.
    SequentialAnimation {
        id: returnAnim
        ScriptAction { script: { root.arrived = false; player.turn = ((player.turn % 360) + 540) % 360 - 180 } }
        ParallelAnimation {
            Tween { target: player; property: "follow"; to: 0; duration: 140 }
            Tween { target: player; property: "turn"; to: 0; duration: 180 }
            Tween { target: player; property: "rise"; to: -1.4; duration: 170; easing.type: Easing.OutQuad }
            Tween { target: player; property: "bodyPitch"; to: 12; duration: 170 }
            Tween { target: player; properties: "headYaw,headRoll,bodyRoll,tilt,rightLegPitch,leftLegPitch"; to: 0; duration: 170 }
            Tween { target: player; property: "headPitch"; to: 6; duration: 170 }
            Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: -34; duration: 170 }
            Tween { target: player; properties: "rightArmYaw,leftArmYaw"; to: 0; duration: 170 }
        }
        ParallelAnimation {
            NumberAnimation { target: root; property: "travel"; to: 0; duration: 820; easing.type: Easing.InOutSine }
            Tween { target: player; property: "rise"; to: 17; duration: 820; easing.type: Easing.InOutSine }
            Tween { target: player; property: "advance"; to: 6; duration: 820 }
            Tween { target: player; property: "turn"; to: -360; duration: 820; easing.type: Easing.InOutCubic }
            Tween { target: player; property: "bodyPitch"; to: 0; duration: 300 }
            Tween { target: player; property: "headPitch"; to: -12; duration: 300 }
            SequentialAnimation {
                Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 165; duration: 260; easing.type: Easing.OutQuad }
                PauseAnimation { duration: 260 }
                Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 20; duration: 300; easing.type: Easing.InQuad }
            }
            SequentialAnimation {
                Tween { target: player; property: "rightLegPitch"; to: 42; duration: 260 }
                PauseAnimation { duration: 260 }
                Tween { target: player; property: "rightLegPitch"; to: 0; duration: 300 }
            }
            SequentialAnimation {
                Tween { target: player; property: "leftLegPitch"; to: -24; duration: 260 }
                PauseAnimation { duration: 260 }
                Tween { target: player; property: "leftLegPitch"; to: 0; duration: 300 }
            }
        }
        ScriptAction { script: { player.turn = 0; player.wall = true; root.land(3) } }
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 15.8; duration: 90; easing.type: Easing.OutQuad }
            Tween { target: player; property: "bodyPitch"; to: 12; duration: 90 }
        }
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 17; duration: 220; easing.type: Easing.OutBack }
            Tween { target: player; property: "bodyPitch"; to: 0; duration: 220 }
            Tween { target: player; property: "headPitch"; to: 0; duration: 220 }
            Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 0; duration: 220 }
        }
        // Hops down behind the card, arms up, out of sight…
        ParallelAnimation {
            SequentialAnimation {
                Tween { target: player; property: "rise"; to: 19.5; duration: 150; easing.type: Easing.OutQuad }
                Tween { target: player; property: "rise"; to: -36; duration: 420; easing.type: Easing.InCubic }
            }
            Tween { target: player; property: "advance"; to: 0; duration: 400 }
            Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 172; duration: 280 }
            Tween { target: player; properties: "rightArmRoll,leftArmRoll"; to: 10; duration: 280 }
            Tween { target: player; property: "headPitch"; to: -14; duration: 280 }
        }
        // …and climbs back up to lean on it.
        ScriptAction { script: { root.dressed = false; root.away = false; root.arrive(false) } }
    }
    // With the ledge out of view (a compact sidebar): it drops onto the stage from above.
    SequentialAnimation {
        id: dropIn
        ScriptAction { script: { root.arrived = false; player.follow = 0; player.rise = 26; player.rightArmPitch = 165; player.leftArmPitch = 165 } }
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 0; duration: 520; easing.type: Easing.InQuad }
            Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 30; duration: 520 }
        }
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: -1.5; duration: 90; easing.type: Easing.OutQuad }
            Tween { target: player; property: "bodyPitch"; to: 12; duration: 90 }
        }
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 0; duration: 400; easing.type: Easing.OutBack; easing.overshoot: 2 }
            Tween { target: player; property: "bodyPitch"; to: 0; duration: 400; easing.type: Easing.OutBack }
            Tween { target: player; properties: "rightArmPitch,leftArmPitch"; to: 0; duration: 400; easing.type: Easing.OutBack }
        }
        ScriptAction { script: { root.arrived = true; moves.settle() } }
    }

    // Clicked: crouches on the press…
    ParallelAnimation {
        id: pressAnim
        Tween { target: player; property: "rise"; to: -1; duration: 110; easing.type: Easing.OutQuad }
        Tween { target: player; property: "bodyPitch"; to: 17; duration: 110 }
        Tween { target: player; property: "headPitch"; to: 9; duration: 110 }
    }
    ParallelAnimation {
        id: releaseAnim
        Tween { target: player; property: "rise"; to: 0; duration: 260; easing.type: Easing.OutBack }
        Tween { target: player; property: "bodyPitch"; to: 12; duration: 260 }
        Tween { target: player; property: "headPitch"; to: 0; duration: 260 }
    }
    // …and then one of: a hop off the ledge,
    SequentialAnimation {
        id: hopAnim
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 2.8; duration: 170; easing.type: Easing.OutQuad }
            Tween { target: player; property: "rightArmPitch"; to: 138; duration: 170; easing.type: Easing.OutQuad }
            Tween { target: player; property: "leftArmPitch"; to: 138; duration: 170; easing.type: Easing.OutQuad }
            Tween { target: player; property: "bodyPitch"; to: 6; duration: 170 }
            Tween { target: player; property: "headPitch"; to: -12; duration: 170 }
        }
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 0; duration: 360; easing.type: Easing.OutBack; easing.overshoot: 2.4 }
            Tween { target: player; property: "rightArmPitch"; to: 82; duration: 190; easing.type: Easing.InQuad }
            Tween { target: player; property: "leftArmPitch"; to: 82; duration: 190; easing.type: Easing.InQuad }
            Tween { target: player; property: "bodyPitch"; to: 12; duration: 360; easing.type: Easing.OutBack }
            Tween { target: player; property: "headPitch"; to: 0; duration: 360; easing.type: Easing.OutBack }
            SequentialAnimation {
                PauseAnimation { duration: 180 }
                ScriptAction { script: root.land(2.2) }
            }
        }
    }
    // a full turn of the head,
    SequentialAnimation {
        id: spinAnim
        ParallelAnimation {
            Tween { target: player; property: "headYaw"; from: 0; to: 360; duration: 620; easing.type: Easing.OutBack; easing.overshoot: 1.4 }
            SequentialAnimation {
                Tween { target: player; property: "rise"; to: 1.2; duration: 160; easing.type: Easing.OutQuad }
                Tween { target: player; property: "rise"; to: 0; duration: 380; easing.type: Easing.OutBounce }
            }
            Tween { target: player; property: "bodyPitch"; to: 12; duration: 300 }
            Tween { target: player; property: "headPitch"; to: 0; duration: 300 }
        }
        ScriptAction { script: player.headYaw = 0 }
    }
    // or a drum roll on the ledge.
    SequentialAnimation {
        id: slapAnim
        ParallelAnimation {
            Tween { target: player; property: "rise"; to: 0; duration: 160; easing.type: Easing.OutBack }
            Tween { target: player; property: "bodyPitch"; to: 14; duration: 160 }
            Tween { target: player; property: "headPitch"; to: 6; duration: 160 }
        }
        SequentialAnimation {
            loops: 3
            Tween { target: player; property: "rightArmPitch"; to: 112; duration: 70; easing.type: Easing.OutQuad }
            Tween { target: player; property: "rightArmPitch"; to: 82; duration: 70; easing.type: Easing.InQuad }
            ScriptAction { script: root.land(1.4) }
            Tween { target: player; property: "leftArmPitch"; to: 112; duration: 70; easing.type: Easing.OutQuad }
            Tween { target: player; property: "leftArmPitch"; to: 82; duration: 70; easing.type: Easing.InQuad }
            ScriptAction { script: root.land(1.4) }
        }
        ParallelAnimation {
            Tween { target: player; property: "bodyPitch"; to: 12; duration: 260 }
            Tween { target: player; property: "headPitch"; to: 0; duration: 260 }
        }
    }

    // Leaning in while pointed at.
    ParallelAnimation {
        id: hoverAnim
        property real lean: 12
        property real roll: 0
        Tween { target: player; property: "bodyPitch"; to: hoverAnim.lean; duration: 320; easing.type: Easing.OutBack }
        Tween { target: player; property: "headRoll"; to: hoverAnim.roll; duration: 320; easing.type: Easing.OutBack }
    }

    // Now and then, something to pass the time.
    SequentialAnimation {
        id: idleAnim
        property int kind: 0
        ScriptAction { script: if (idleAnim.kind === 0) idleDrum.start(); else if (idleAnim.kind === 1) idleLook.start(); else idleStretch.start() }
    }
    SequentialAnimation {
        id: idleDrum
        SequentialAnimation {
            loops: 4
            Tween { target: player; property: "rightArmPitch"; to: 96; duration: 80; easing.type: Easing.OutQuad }
            Tween { target: player; property: "rightArmPitch"; to: 82; duration: 80; easing.type: Easing.InQuad }
            Tween { target: player; property: "leftArmPitch"; to: 96; duration: 80; easing.type: Easing.OutQuad }
            Tween { target: player; property: "leftArmPitch"; to: 82; duration: 80; easing.type: Easing.InQuad }
        }
    }
    SequentialAnimation {
        id: idleLook
        Tween { target: player; property: "follow"; to: 0; duration: 300 }
        Tween { target: player; property: "headYaw"; to: -46; duration: 420; easing.type: Easing.InOutSine }
        PauseAnimation { duration: 500 }
        Tween { target: player; property: "headYaw"; to: 42; duration: 620; easing.type: Easing.InOutSine }
        PauseAnimation { duration: 420 }
        Tween { target: player; property: "headYaw"; to: 0; duration: 380; easing.type: Easing.InOutSine }
        Tween { target: player; property: "follow"; to: 1; duration: 500; easing.type: Easing.InOutSine }
    }
    SequentialAnimation {
        id: idleStretch
        ParallelAnimation {
            Tween { target: player; property: "rightArmPitch"; to: 168; duration: 520; easing.type: Easing.InOutSine }
            Tween { target: player; property: "leftArmPitch"; to: 168; duration: 520; easing.type: Easing.InOutSine }
            Tween { target: player; property: "rightArmRoll"; to: 16; duration: 520; easing.type: Easing.InOutSine }
            Tween { target: player; property: "leftArmRoll"; to: 16; duration: 520; easing.type: Easing.InOutSine }
            Tween { target: player; property: "rise"; to: 1.2; duration: 520; easing.type: Easing.InOutSine }
            Tween { target: player; property: "bodyPitch"; to: 0; duration: 520; easing.type: Easing.InOutSine }
            Tween { target: player; property: "headPitch"; to: -18; duration: 520; easing.type: Easing.InOutSine }
        }
        PauseAnimation { duration: 450 }
        ParallelAnimation {
            Tween { target: player; property: "rightArmPitch"; to: 82; duration: 380; easing.type: Easing.InQuad }
            Tween { target: player; property: "leftArmPitch"; to: 82; duration: 380; easing.type: Easing.InQuad }
            Tween { target: player; property: "rightArmRoll"; to: 0; duration: 380 }
            Tween { target: player; property: "leftArmRoll"; to: 0; duration: 380 }
            Tween { target: player; property: "rise"; to: 0; duration: 500; easing.type: Easing.OutBack }
            Tween { target: player; property: "bodyPitch"; to: 12; duration: 500; easing.type: Easing.OutBack }
            Tween { target: player; property: "headPitch"; to: 0; duration: 500 }
            SequentialAnimation {
                PauseAnimation { duration: 370 }
                ScriptAction { script: root.land(1.6) }
            }
        }
    }
    Timer {
        interval: 11000
        repeat: true
        running: root.present && !root.away && root.visible && !root.reduced && Qt.application.state === Qt.ApplicationActive
        onTriggered: {
            interval = 9000 + Math.random() * 9000
            if (root.busy || idleDrum.running || idleLook.running || idleStretch.running || root.menuOpen || hit.containsMouse) return
            idleAnim.kind = Math.floor(Math.random() * 3)
            idleAnim.start()
        }
    }

    // A round icon button on the card; `progress` (0 to 1, -1 unknown, -2 none) rings it.
    component LedgeButton: Button {
        id: control
        property string iconName: ""
        property string label: ""
        property bool active: false
        property real progress: -2
        width: 34; height: 34
        hoverEnabled: true
        Accessible.name: label
        ToolTip.visible: hovered && progress === -2
        ToolTip.text: label
        ToolTip.delay: 400
        onHoveredChanged: if (hovered && !active) sfx.play("hover")
        background: Rectangle {
            radius: width / 2
            color: control.active ? Theme.accentSoft : control.down ? Theme.fillHover : control.hovered ? "#12FFFFFF" : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.fast } }
            Rectangle {
                anchors.fill: parent; anchors.margins: -2
                radius: width / 2; color: "transparent"
                border.width: 2; border.color: Theme.focusRing
                visible: control.visualFocus
            }
        }
        contentItem: Item {
            UiIcon {
                anchors.centerIn: parent
                width: 17; height: 17
                name: control.iconName
                color: control.active ? Theme.accent : control.hovered ? Theme.secondary : Theme.muted
                Behavior on color { ColorAnimation { duration: Theme.fast } }
                scale: control.down ? 0.88 : control.hovered ? 1.1 : 1
                Behavior on scale { NumberAnimation { duration: Theme.normal; easing.type: Easing.OutBack } }
            }
            Shape {
                anchors.fill: parent
                visible: control.progress > -2
                preferredRendererType: Shape.CurveRenderer
                asynchronous: true
                RotationAnimator on rotation {
                    running: control.progress === -1 && control.visible
                    from: 0; to: 360; duration: 1100; loops: Animation.Infinite
                }
                ShapePath {
                    strokeColor: "#24FFFFFF"; strokeWidth: 2; fillColor: "transparent"
                    PathAngleArc { centerX: 17; centerY: 17; radiusX: 15; radiusY: 15; startAngle: 0; sweepAngle: 360 }
                }
                ShapePath {
                    strokeColor: Theme.accent; strokeWidth: 2; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathAngleArc {
                        centerX: 17; centerY: 17; radiusX: 15; radiusY: 15; startAngle: -90
                        sweepAngle: control.progress < 0 ? 90 : 360 * Math.max(0.03, control.progress)
                        Behavior on sweepAngle { NumberAnimation { duration: Theme.normal } }
                    }
                }
            }
        }
    }
}
