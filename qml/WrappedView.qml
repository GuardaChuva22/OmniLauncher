import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Retrospectiva: the player's Minecraft in story-like slides (time, favourite
// world, distance, mining, combat, trivia), ending on a card to save or copy.
// Slides advance on their own; click the right side or press → for the next,
// the left side or ← for the previous, Esc to close.
Item {
    id: root
    property bool reduced: false
    signal notify(string message)
    anchors.fill: parent
    visible: opacity > 0
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : 420; easing.type: Easing.OutCubic } }

    property bool shown: false
    property var info: ({})
    property var slides: []
    property int slide: 0
    readonly property string current: slides.length ? slides[Math.min(slide, slides.length - 1)] : ""
    readonly property var tones: ({ intro: "#E68AA6", world: "#7FB2E8", travel: "#6FC7B8", mining: "#E8B86A",
                                      combat: "#E8836F", trivia: "#A08AE6", final: "#E68AA6" })
    readonly property color tone: tones[current] || Theme.accent

    function open(gameId) {
        info = {}
        slides = []
        shown = true
        forceActiveFocus()
        sfx.play("open")
        Qt.callLater(function() {
            info = wrapped.build(launcher.games, launcher.gameSettings, gameId || "")
            slides = buildSlides(info)
            slide = 0
            restartSlide()
        })
    }
    function close() { shown = false; progress.stop() }
    function go(index) {
        if (index < 0 || index >= slides.length || index === slide) return
        slide = index
        sfx.play(index === slides.length - 1 ? "success" : "page")
        restartSlide()
    }
    function restartSlide() {
        progress.stop()
        progress.value = 0
        if (current !== "final") progress.start()
    }
    function buildSlides(d) {
        const list = ["intro"]
        if (d.hasStats && d.topWorld && d.topWorld.name) list.push("world")
        if (d.distanceCm > 0) list.push("travel")
        if (d.mined > 0) list.push("mining")
        if (d.mobKills > 0 || d.deaths > 0) list.push("combat")
        if (trivia(d).length > 0) list.push("trivia")
        list.push("final")
        return list
    }
    function fmt(n) { return Number(Math.round(n || 0)).toLocaleString(Qt.locale("pt_BR"), "f", 0) }
    function seconds(d) { return Math.max(d.playSeconds || 0, Math.floor((d.playTicks || 0) / 20)) }
    // Under an hour reads better in minutes.
    function timeValue(s) { return s < 3600 ? Math.max(1, Math.round(s / 60)) : hours(s) }
    function timeUnit(s) { return s < 3600 ? (Math.round(s / 60) <= 1 ? "minuto" : "minutos") : "horas" }
    function timeText(s) {
        const v = timeValue(s)
        return s >= 3600 && v < 10 ? Number(v).toLocaleString(Qt.locale("pt_BR"), "f", 1) : fmt(v)
    }
    function hours(s) { const h = s / 3600; return h >= 10 ? Math.round(h) : Math.round(h * 10) / 10 }
    function km(cm) { return cm / 100000 }
    function distanceLine(cm) {
        const k = km(cm)
        if (k < 1) return "São " + fmt(Math.max(1, cm / 10500)) + " campos de futebol de ponta a ponta."
        if (k >= 430) return "Mais do que ir de São Paulo ao Rio de Janeiro."
        if (k >= 95) return "Dá pra ir de São Paulo a Campinas."
        return "É a Avenida Paulista inteira " + fmt(Math.max(1, k / 2.8)) + (k / 2.8 >= 1.5 ? " vezes." : " vez.")
    }
    function trivia(d) {
        const e = d.extras || {}
        const all = [{ v: e.jumps, label: "pulos" }, { v: e.chests, label: "baús abertos" }, { v: e.hearts, label: "corações de dano causados" },
                     { v: e.trades, label: "trocas com aldeões" }, { v: e.fish, label: "peixes pescados" }, { v: e.bred, label: "animais criados" },
                     { v: e.sleeps, label: "noites dormidas" }, { v: e.enchants, label: "itens encantados" }, { v: e.cakes, label: "fatias de bolo" },
                     { v: e.raids, label: "invasões vencidas" }, { v: e.playerKills, label: "jogadores derrotados" }]
        return all.filter(function(x) { return x.v > 0 }).slice(0, 6)
    }

    Keys.onEscapePressed: root.close()
    Keys.onRightPressed: root.go(root.slide + 1)
    Keys.onSpacePressed: root.go(root.slide + 1)
    Keys.onLeftPressed: root.go(root.slide - 1)

    NumberAnimation {
        id: progress
        target: progress; property: "value"
        property real value: 0
        from: 0; to: 1; duration: 7500
        onFinished: root.go(root.slide + 1)
    }

    // ── Background: drifting light in the slide's colour ──────────────────
    Rectangle { anchors.fill: parent; color: "#0B090C" }
    Repeater {
        model: [{ x: 0.18, y: 0.25, size: 560, dx: 90, dy: 60, t: 11000, o: 0.34 },
                { x: 0.82, y: 0.7, size: 640, dx: -110, dy: -50, t: 13000, o: 0.26 },
                { x: 0.55, y: 0.05, size: 420, dx: 60, dy: 90, t: 9000, o: 0.18 }]
        delegate: SoftGlow {
            id: blob
            required property var modelData
            required property int index
            width: modelData.size; height: modelData.size
            radius: modelData.size / 2
            blurMax: 64
            color: index === 1 ? Qt.darker(root.tone, 1.6) : root.tone
            Behavior on color { ColorAnimation { duration: 900 } }
            opacity: modelData.o
            property real drift: 0
            x: root.width * modelData.x - width / 2 + modelData.dx * drift
            y: root.height * modelData.y - height / 2 + modelData.dy * drift
            SequentialAnimation on drift {
                running: root.visible && !root.reduced
                loops: Animation.Infinite
                NumberAnimation { to: 1; duration: blob.modelData.t; easing.type: Easing.InOutSine }
                NumberAnimation { to: 0; duration: blob.modelData.t; easing.type: Easing.InOutSine }
            }
        }
    }

    // Click zones: left third goes back, the rest forward.
    MouseArea {
        anchors.fill: parent
        onClicked: function(mouse) { mouse.x < width * 0.3 ? root.go(root.slide - 1) : root.go(root.slide + 1) }
    }

    // ── Progress and close ────────────────────────────────────────────────
    Row {
        id: bars
        x: 40; y: 26
        width: parent.width - 140
        spacing: 6
        Repeater {
            model: root.slides.length
            delegate: Rectangle {
                required property int index
                width: (bars.width - bars.spacing * (root.slides.length - 1)) / Math.max(1, root.slides.length)
                height: 3; radius: 1.5
                color: "#26FFFFFF"
                Rectangle {
                    height: parent.height; radius: 1.5
                    color: "#F5F1ED"
                    width: parent.width * (index < root.slide ? 1 : index === root.slide ? (root.current === "final" ? 1 : progress.value) : 0)
                }
            }
        }
    }
    ActionButton {
        anchors.right: parent.right; anchors.rightMargin: 28
        y: 12
        width: 40; height: 40; round: true; quiet: true; iconName: "close"; reduced: root.reduced
        Accessible.name: "Fechar"
        onClicked: root.close()
    }

    // ── Slides ────────────────────────────────────────────────────────────
    component Kicker: Text {
        color: Qt.lighter(root.tone, 1.25)
        font.family: Theme.font; font.pixelSize: 14; font.weight: Font.Bold
        font.letterSpacing: 2.4
        font.capitalization: Font.AllUppercase
    }
    component Big: Text {
        color: Theme.text
        font.family: Theme.fontDisplay; font.weight: Font.Bold
        font.pixelSize: 88; font.letterSpacing: -2.5
    }
    component Line: Text {
        color: Theme.secondary
        font.family: Theme.font; font.pixelSize: 19; font.weight: Font.Medium
        wrapMode: Text.WordWrap
    }
    // Counts from 0 to `target` as it appears.
    component Count: Big {
        id: count
        property real target: 0
        property real shownValue: 0
        property string suffix: ""
        property int decimals: 0
        text: (decimals > 0 ? Number(shownValue).toLocaleString(Qt.locale("pt_BR"), "f", decimals) : root.fmt(shownValue)) + suffix
        NumberAnimation on shownValue {
            running: true
            from: 0; to: count.target
            duration: root.reduced ? 0 : 1500; easing.type: Easing.OutCubic
        }
    }
    // Fades and rises in `delay` ms after the slide opens.
    component Rise: Item {
        id: rise
        property int delay: 0
        default property alias content: inner.data
        implicitWidth: inner.childrenRect.width
        implicitHeight: inner.childrenRect.height
        opacity: 0
        transform: Translate { id: lift; y: 26 }
        Item { id: inner; width: rise.width; height: rise.height }
        SequentialAnimation {
            running: true
            PauseAnimation { duration: root.reduced ? 0 : rise.delay }
            ParallelAnimation {
                NumberAnimation { target: rise; property: "opacity"; to: 1; duration: root.reduced ? 0 : 520; easing.type: Easing.OutCubic }
                NumberAnimation { target: lift; property: "y"; to: 0; duration: root.reduced ? 0 : 720; easing.type: Easing.OutQuint }
            }
        }
    }
    component Glyph: Rectangle {
        id: icon
        property string source: ""
        property string label: ""
        property real size: 44
        width: size; height: size
        radius: size * 0.22
        color: "#14FFFFFF"
        border.width: 1; border.color: "#1AFFFFFF"
        Image {
            anchors.fill: parent; anchors.margins: icon.size * 0.14
            visible: icon.source !== ""
            source: icon.source
            smooth: false; mipmap: false
            fillMode: Image.PreserveAspectCrop
            verticalAlignment: Image.AlignTop
        }
        Text {
            visible: icon.source === ""
            anchors.centerIn: parent
            text: icon.label.charAt(0)
            color: Theme.secondary
            font.family: Theme.fontDisplay; font.pixelSize: icon.size * 0.42; font.weight: Font.Bold
        }
    }
    // Ranked list: icon, name, count and a bar relative to the first.
    component Ranking: Column {
        id: ranking
        property var items: []
        property int delay: 0
        spacing: 12
        Repeater {
            model: ranking.items
            delegate: Rise {
                required property var modelData
                required property int index
                delay: ranking.delay + index * 110
                width: ranking.width; height: 52
                RowLayout {
                    width: parent.width; height: parent.height
                    spacing: 14
                    Text { text: (index + 1); color: Theme.faint; font.family: Theme.fontDisplay; font.pixelSize: 18; font.weight: Font.Bold; Layout.preferredWidth: 18 }
                    Glyph { source: modelData.icon || ""; label: modelData.name; size: 44 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: modelData.name; color: Theme.text; font.family: Theme.font; font.pixelSize: 16; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight }
                            Text { text: root.fmt(modelData.count); color: Theme.secondary; font.family: Theme.font; font.pixelSize: 15; font.weight: Font.Medium }
                        }
                        Rectangle {
                            Layout.fillWidth: true; height: 4; radius: 2; color: "#1AFFFFFF"
                            Rectangle {
                                height: parent.height; radius: 2; color: root.tone
                                property real grow: 0
                                width: parent.width * grow * modelData.count / Math.max(1, ranking.items[0].count)
                                NumberAnimation on grow { running: true; to: 1; duration: root.reduced ? 0 : 1100; easing.type: Easing.OutCubic }
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        id: stage
        anchors.fill: parent
        anchors.topMargin: 70; anchors.bottomMargin: 40
        readonly property real column: Math.min(720, width - 120)

        Text {
            visible: root.slides.length === 0 && root.shown
            anchors.centerIn: parent
            text: "Juntando suas aventuras…"
            color: Theme.secondary
            font.family: Theme.font; font.pixelSize: 16
        }

        Loader {
            id: slideLoader
            anchors.centerIn: parent
            width: stage.column
            active: root.current !== ""
            sourceComponent: root.current === "intro" ? introSlide : root.current === "world" ? worldSlide
                           : root.current === "travel" ? travelSlide : root.current === "mining" ? miningSlide
                           : root.current === "combat" ? combatSlide : root.current === "trivia" ? triviaSlide : finalSlide
        }
    }

    Component {
        id: introSlide
        ColumnLayout {
            spacing: 14
            Rise { delay: 0; Kicker { text: "Sua retrospectiva" } }
            Rise { delay: 140; Layout.fillWidth: true; Big { width: stage.column; text: root.info.title || ""; font.pixelSize: 56; font.letterSpacing: -1.4; wrapMode: Text.WordWrap } }
            Rise {
                delay: 420
                Layout.topMargin: 30
                Row {
                    spacing: 16
                    Count { id: introHours; target: root.timeValue(root.seconds(root.info)); decimals: root.seconds(root.info) >= 3600 && root.timeValue(root.seconds(root.info)) < 10 ? 1 : 0; font.pixelSize: 120; color: Qt.lighter(root.tone, 1.15) }
                    Text { anchors.baseline: introHours.baseline; text: root.timeUnit(root.seconds(root.info)); color: Theme.text; font.family: Theme.fontDisplay; font.pixelSize: 34; font.weight: Font.DemiBold }
                }
            }
            Rise {
                delay: 700
                Line {
                    width: stage.column
                    text: !root.info.hasStats ? "Jogue um pouco nos seus mundos para a retrospectiva ganhar números."
                        : "de Minecraft em " + root.fmt(root.info.worlds) + (root.info.worlds === 1 ? " mundo" : " mundos")
                          + (root.info.scope === "all" ? ", " + root.fmt(root.info.games) + (root.info.games === 1 ? " jogo" : " jogos") : "")
                          + (root.info.sessions > 0 ? " e " + root.fmt(root.info.sessions) + (root.info.sessions === 1 ? " sessão." : " sessões.") : ".")
                }
            }
        }
    }

    Component {
        id: worldSlide
        ColumnLayout {
            spacing: 14
            Rise { Kicker { text: "Seu mundo favorito" } }
            Rise { delay: 160; Big { width: stage.column; text: root.info.topWorld.name; wrapMode: Text.WordWrap; font.pixelSize: 76 } }
            Rise {
                delay: 480
                Line {
                    width: stage.column
                    text: "Você passou " + root.timeText(root.info.topWorld.seconds) + " " + root.timeUnit(root.info.topWorld.seconds) + " lá" + (root.info.scope === "all" ? ", em " + root.info.topWorld.game : "") + "."
                }
            }
            Rise {
                delay: 760
                visible: root.info.scope === "all" && root.info.topGame.title !== ""
                Line {
                    width: stage.column
                    color: Theme.muted; font.pixelSize: 16
                    text: "E o jogo que mais te prendeu foi " + root.info.topGame.title + "."
                }
            }
        }
    }

    Component {
        id: travelSlide
        ColumnLayout {
            spacing: 14
            Rise { Kicker { text: "Você andou" } }
            Rise {
                delay: 140
                Row {
                    spacing: 14
                    Count { id: kmCount; target: root.km(root.info.distanceCm); decimals: root.km(root.info.distanceCm) < 10 ? 1 : 0; color: Qt.lighter(root.tone, 1.15); font.pixelSize: 110 }
                    Text { anchors.baseline: kmCount.baseline; text: "km"; color: Theme.text; font.family: Theme.fontDisplay; font.pixelSize: 34; font.weight: Font.DemiBold }
                }
            }
            Rise { delay: 420; Line { width: stage.column; text: root.distanceLine(root.info.distanceCm) } }
            Item { Layout.preferredHeight: 18 }
            Repeater {
                model: (root.info.travel || []).slice(0, 5)
                delegate: Rise {
                    required property var modelData
                    required property int index
                    delay: 640 + index * 100
                    Layout.fillWidth: true
                    implicitHeight: 34
                    RowLayout {
                        width: stage.column; height: 34
                        spacing: 14
                        Text { text: modelData.label; color: Theme.text; font.family: Theme.font; font.pixelSize: 15; font.weight: Font.DemiBold; Layout.preferredWidth: 140 }
                        Rectangle {
                            Layout.fillWidth: true; height: 6; radius: 3; color: "#1AFFFFFF"
                            Rectangle {
                                height: parent.height; radius: 3; color: root.tone
                                property real grow: 0
                                width: parent.width * grow * modelData.cm / Math.max(1, root.info.travel[0].cm)
                                NumberAnimation on grow { running: true; to: 1; duration: root.reduced ? 0 : 1100; easing.type: Easing.OutCubic }
                            }
                        }
                        Text {
                            text: root.km(modelData.cm) >= 1 ? Number(root.km(modelData.cm)).toLocaleString(Qt.locale("pt_BR"), "f", 1) + " km" : root.fmt(modelData.cm / 100) + " m"
                            color: Theme.secondary; font.family: Theme.font; font.pixelSize: 14
                            Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight
                        }
                    }
                }
            }
        }
    }

    Component {
        id: miningSlide
        ColumnLayout {
            spacing: 14
            Rise { Kicker { text: "Blocos minerados" } }
            Rise { delay: 140; Count { target: root.info.mined; color: Qt.lighter(root.tone, 1.15); font.pixelSize: 110 } }
            Rise {
                delay: 380
                Line {
                    width: stage.column
                    text: root.info.topMined.length ? "E o que mais saiu da sua picareta foi " + root.info.topMined[0].name + "." : ""
                }
            }
            Item { Layout.preferredHeight: 14 }
            Ranking { Layout.fillWidth: true; items: root.info.topMined; delay: 600 }
        }
    }

    Component {
        id: combatSlide
        ColumnLayout {
            spacing: 14
            Rise { Kicker { text: "Criaturas derrotadas" } }
            Rise { delay: 140; Count { target: root.info.mobKills; color: Qt.lighter(root.tone, 1.15); font.pixelSize: 110 } }
            Item { Layout.preferredHeight: 6 }
            Ranking { Layout.fillWidth: true; items: (root.info.topKilled || []).slice(0, 3); delay: 420 }
            Rise {
                delay: 900
                Layout.topMargin: 22
                visible: root.info.deaths > 0
                RowLayout {
                    width: stage.column
                    spacing: 16
                    Glyph {
                        visible: root.info.topKilledBy.length > 0
                        source: root.info.topKilledBy.length ? (root.info.topKilledBy[0].icon || "") : ""
                        label: root.info.topKilledBy.length ? root.info.topKilledBy[0].name : ""
                        size: 52
                    }
                    Line {
                        Layout.fillWidth: true
                        text: "Você morreu " + root.fmt(root.info.deaths) + (root.info.deaths === 1 ? " vez" : " vezes")
                            + (root.info.topKilledBy.length ? ". Quem mais te pegou: " + root.info.topKilledBy[0].name + "." : ".")
                    }
                }
            }
        }
    }

    Component {
        id: triviaSlide
        ColumnLayout {
            spacing: 14
            Rise { Kicker { text: "No meio do caminho" } }
            Rise { delay: 120; Big { text: "Os detalhes"; font.pixelSize: 64 } }
            Item { Layout.preferredHeight: 18 }
            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 14; rowSpacing: 14
                Repeater {
                    model: root.trivia(root.info)
                    delegate: Rise {
                        required property var modelData
                        required property int index
                        delay: 320 + index * 110
                        Layout.fillWidth: true
                        Layout.preferredHeight: 118
                        Rectangle {
                            width: parent.width; height: 118
                            radius: Theme.radiusLg
                            color: "#12FFFFFF"
                            border.width: 1; border.color: "#1AFFFFFF"
                            Column {
                                x: 18; anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 36
                                spacing: 4
                                Count { target: modelData.v; font.pixelSize: 40; font.letterSpacing: -1; color: Qt.lighter(root.tone, 1.2) }
                                Text { text: modelData.label; width: parent.width; wrapMode: Text.WordWrap; color: Theme.secondary; font.family: Theme.font; font.pixelSize: 14 }
                            }
                        }
                    }
                }
            }
            Rise {
                delay: 1100
                Layout.topMargin: 12
                visible: (root.info.topCrafted || []).length > 0
                RowLayout {
                    width: stage.column
                    spacing: 14
                    Glyph { source: root.info.topCrafted.length ? (root.info.topCrafted[0].icon || "") : ""; label: root.info.topCrafted.length ? root.info.topCrafted[0].name : ""; size: 44 }
                    Line {
                        Layout.fillWidth: true
                        font.pixelSize: 16
                        text: root.info.topCrafted.length ? "Seu item mais fabricado: " + root.info.topCrafted[0].name + " (" + root.fmt(root.info.topCrafted[0].count) + ")." : ""
                    }
                }
            }
        }
    }

    Component {
        id: finalSlide
        ColumnLayout {
            spacing: 22
            Rise {
                Layout.alignment: Qt.AlignHCenter
                delay: 0
                Item {
                    width: card.width * card.scale; height: card.height * card.scale
                    // The card is laid out at 540×675 and saved at 2×.
                    Rectangle {
                        id: card
                        width: 540; height: 675
                        scale: Math.min(1, (root.height - 260) / 675)
                        transformOrigin: Item.TopLeft
                        radius: 28
                        clip: true
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#2A1620" }
                            GradientStop { position: 0.55; color: "#17111A" }
                            GradientStop { position: 1; color: "#100D12" }
                        }
                        border.width: 1; border.color: "#24FFFFFF"
                        SoftGlow { x: -120; y: -140; width: 420; height: 420; radius: 210; color: Theme.accent; blurMax: 64; opacity: 0.32 }
                        SoftGlow { x: 300; y: 430; width: 360; height: 360; radius: 180; color: "#A08AE6"; blurMax: 64; opacity: 0.22 }
                        Column {
                            x: 40; y: 40
                            width: card.width - 80
                            spacing: 0
                            Image {
                                height: 26; width: Math.round(height * 378 / 120)
                                source: Theme.brandSource("logo-primary")
                                sourceSize.height: 120
                                fillMode: Image.PreserveAspectFit
                                smooth: true; mipmap: true
                            }
                            Item { width: 1; height: 44 }
                            Text { text: "RETROSPECTIVA"; color: Theme.accentText; font.family: Theme.font; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 2.4 }
                            Item { width: 1; height: 8 }
                            Text { width: parent.width; text: root.info.title || ""; color: Theme.text; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight; font.family: Theme.fontDisplay; font.pixelSize: 34; font.weight: Font.Bold; font.letterSpacing: -0.8 }
                            Item { width: 1; height: 26 }
                            Row {
                                spacing: 10
                                Text { id: cardHours; text: root.timeText(root.seconds(root.info)); color: Theme.text; font.family: Theme.fontDisplay; font.pixelSize: 84; font.weight: Font.Bold; font.letterSpacing: -2.4 }
                                Text { anchors.baseline: cardHours.baseline; text: root.timeUnit(root.seconds(root.info)) + " jogad" + (root.timeUnit(root.seconds(root.info)) === "horas" ? "as" : "os"); color: Theme.secondary; font.family: Theme.font; font.pixelSize: 18; font.weight: Font.DemiBold }
                            }
                            Item { width: 1; height: 26 }
                            Grid {
                                columns: 2
                                columnSpacing: 16; rowSpacing: 16
                                Repeater {
                                    model: [{ v: Number(root.km(root.info.distanceCm || 0)).toLocaleString(Qt.locale("pt_BR"), "f", 1), l: "km percorridos" },
                                            { v: root.fmt(root.info.mined), l: "blocos minerados" },
                                            { v: root.fmt(root.info.mobKills), l: "criaturas derrotadas" },
                                            { v: root.fmt(root.info.deaths), l: "mortes" },
                                            { v: root.fmt(root.info.worlds), l: root.info.worlds === 1 ? "mundo" : "mundos" },
                                            { v: root.fmt((root.info.extras || {}).jumps), l: "pulos" }]
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: (card.width - 80 - 16) / 2; height: 78
                                        radius: 16
                                        color: "#10FFFFFF"
                                        border.width: 1; border.color: "#14FFFFFF"
                                        Column {
                                            x: 16; anchors.verticalCenter: parent.verticalCenter
                                            spacing: 2
                                            Text { text: modelData.v; color: Theme.text; font.family: Theme.fontDisplay; font.pixelSize: 26; font.weight: Font.Bold }
                                            Text { text: modelData.l; color: Theme.muted; font.family: Theme.font; font.pixelSize: 13 }
                                        }
                                    }
                                }
                            }
                        }
                        RowLayout {
                            x: 40; width: card.width - 80
                            anchors.bottom: parent.bottom; anchors.bottomMargin: 30
                            spacing: 10
                            Glyph {
                                visible: (root.info.topMined || []).length > 0
                                source: (root.info.topMined || []).length ? (root.info.topMined[0].icon || "") : ""
                                label: (root.info.topMined || []).length ? root.info.topMined[0].name : ""
                                size: 34
                            }
                            Text {
                                Layout.fillWidth: true
                                text: (root.info.topMined || []).length ? "Bloco do ano: " + root.info.topMined[0].name : ""
                                color: Theme.secondary; font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium
                                elide: Text.ElideRight
                            }
                            Text { text: new Date().toLocaleDateString(Qt.locale("pt_BR"), "MMMM yyyy"); color: Theme.faint; font.family: Theme.font; font.pixelSize: 12 }
                        }
                    }
                }
            }
            Rise {
                Layout.alignment: Qt.AlignHCenter
                delay: 420
                Row {
                    spacing: 8
                    ActionButton {
                        text: "Salvar imagem"; iconName: "download"; primary: true; reduced: root.reduced
                        onClicked: {
                            const folder = StandardPaths.writableLocation(StandardPaths.PicturesLocation).toString().replace(/^file:\/{3}/, "")
                            const path = decodeURIComponent(folder) + "/OmniLauncher-retrospectiva-" + Qt.formatDateTime(new Date(), "yyyy-MM-dd_HH-mm-ss") + ".png"
                            card.grabToImage(function(result) {
                                if (result.saveToFile(path)) { root.notify("Imagem salva em Imagens."); runner.openFile(decodeURIComponent(folder)) }
                                else root.notify("Não foi possível salvar a imagem.")
                            }, Qt.size(1080, 1350))
                        }
                    }
                    ActionButton {
                        text: "Copiar"; iconName: "copy"; reduced: root.reduced
                        onClicked: {
                            const path = decodeURIComponent(StandardPaths.writableLocation(StandardPaths.TempLocation).toString().replace(/^file:\/{3}/, "")) + "/omnilauncher-retrospectiva.png"
                            card.grabToImage(function(result) {
                                root.notify(result.saveToFile(path) && runner.copyImage(path) ? "Imagem copiada. É só colar." : "Não foi possível copiar a imagem.")
                            }, Qt.size(1080, 1350))
                        }
                    }
                    ActionButton { text: "Ver de novo"; quiet: true; reduced: root.reduced; onClicked: root.go(0) }
                }
            }
        }
    }
}
