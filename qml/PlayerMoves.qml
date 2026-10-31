import QtQuick

// What the player does standing on the skins page, as in a fitting room: on new clothes it
// looks at its arms, checks the outfit from both sides, shows off with a twirl, or turns
// to see the cape on its back; now and then, while nothing happens, it shifts its weight
// or glances around. Every move ends back in `standPose`, the head following the cursor.
Item {
    id: root
    property var player: null
    property bool reduced: false
    readonly property bool busy: lookArms.running || checkOutfit.running || twirl.running || capeCheck.running
        || glance.running || shift.running || settleAnim.running

    readonly property var standPose: ({
        rise: 0, advance: 0, tilt: 0, bodyPitch: 0, bodyRoll: 0, headPitch: 0, headYaw: 0, headRoll: 0,
        rightArmPitch: 0, rightArmYaw: 0, rightArmRoll: -3, leftArmPitch: 0, leftArmYaw: 0, leftArmRoll: -3,
        rightLegPitch: 0, leftLegPitch: 0, capeSwing: 9
    })

    function stop() {
        for (const animation of [lookArms, checkOutfit, twirl, capeCheck, glance, shift, settleAnim])
            animation.stop()
    }
    // Back to standing, facing the viewer, from wherever it is.
    function settle() {
        stop()
        player.turn = ((player.turn % 360) + 540) % 360 - 180
        settleAnim.start()
    }
    // "arms" (a new arm width), "outfit" (a new skin) or "cape".
    function react(kind) {
        if (!player) return
        stop()
        if (reduced) { settle(); return }
        player.turn = ((player.turn % 360) + 540) % 360 - 180
        const pick = Math.random()
        const move = kind === "cape" ? capeCheck
            : kind === "arms" ? lookArms
            : pick < 0.4 ? checkOutfit : pick < 0.7 ? lookArms : twirl
        move.start()
    }
    function idle() {
        if (busy || reduced) return
        (Math.random() < 0.55 ? glance : shift).start()
    }

    component Tween: NumberAnimation { duration: 260; easing.type: Easing.InOutSine }

    ParallelAnimation {
        id: settleAnim
        Tween { target: root.player; property: "turn"; to: 0; duration: 420; easing.type: Easing.OutCubic }
        Tween { target: root.player; properties: "rise,advance,tilt,bodyPitch,bodyRoll,headPitch,headYaw,headRoll"; to: 0; duration: 380; easing.type: Easing.OutCubic }
        Tween { target: root.player; properties: "rightArmPitch,rightArmYaw,leftArmPitch,leftArmYaw,rightLegPitch,leftLegPitch"; to: 0; duration: 380; easing.type: Easing.OutCubic }
        Tween { target: root.player; properties: "rightArmRoll,leftArmRoll"; to: -3; duration: 380; easing.type: Easing.OutCubic }
        Tween { target: root.player; property: "capeSwing"; to: 9; duration: 380 }
        Tween { target: root.player; property: "follow"; to: 1; duration: 600 }
    }

    // Holds out one arm, then the other, turning each to look it over; a nod.
    SequentialAnimation {
        id: lookArms
        ParallelAnimation {
            Tween { target: root.player; property: "follow"; to: 0; duration: 200 }
            Tween { target: root.player; property: "turn"; to: 0; duration: 300 }
            Tween { target: root.player; property: "rightArmPitch"; to: 78; duration: 340; easing.type: Easing.OutBack }
            Tween { target: root.player; property: "rightArmYaw"; to: 26; duration: 340 }
            Tween { target: root.player; property: "headYaw"; to: -24; duration: 340 }
            Tween { target: root.player; property: "headPitch"; to: 22; duration: 340 }
            Tween { target: root.player; property: "bodyRoll"; to: 3; duration: 340 }
        }
        SequentialAnimation {
            loops: 2
            Tween { target: root.player; property: "rightArmRoll"; to: 20; duration: 170 }
            Tween { target: root.player; property: "rightArmRoll"; to: -10; duration: 170 }
        }
        ParallelAnimation {
            Tween { target: root.player; property: "rightArmPitch"; to: 0; duration: 320 }
            Tween { target: root.player; property: "rightArmYaw"; to: 0; duration: 320 }
            Tween { target: root.player; property: "rightArmRoll"; to: -3; duration: 320 }
            Tween { target: root.player; property: "leftArmPitch"; to: 78; duration: 340; easing.type: Easing.OutBack }
            Tween { target: root.player; property: "leftArmYaw"; to: 26; duration: 340 }
            Tween { target: root.player; property: "headYaw"; to: 24; duration: 380 }
            Tween { target: root.player; property: "bodyRoll"; to: -3; duration: 380 }
        }
        SequentialAnimation {
            loops: 2
            Tween { target: root.player; property: "leftArmRoll"; to: 20; duration: 170 }
            Tween { target: root.player; property: "leftArmRoll"; to: -10; duration: 170 }
        }
        ParallelAnimation {
            Tween { target: root.player; property: "leftArmPitch"; to: 0; duration: 300 }
            Tween { target: root.player; property: "leftArmYaw"; to: 0; duration: 300 }
            Tween { target: root.player; property: "leftArmRoll"; to: -3; duration: 300 }
            Tween { target: root.player; property: "headYaw"; to: 0; duration: 300 }
            Tween { target: root.player; property: "headPitch"; to: -4; duration: 300 }
            Tween { target: root.player; property: "bodyRoll"; to: 0; duration: 300 }
        }
        Tween { target: root.player; property: "headPitch"; to: 14; duration: 150; easing.type: Easing.OutQuad }
        Tween { target: root.player; property: "headPitch"; to: 0; duration: 240; easing.type: Easing.OutBack }
        ScriptAction { script: root.settle() }
    }

    // Looks down at itself, turns to one side and the other, then: ta-da.
    SequentialAnimation {
        id: checkOutfit
        ParallelAnimation {
            Tween { target: root.player; property: "follow"; to: 0; duration: 200 }
            Tween { target: root.player; property: "turn"; to: 0; duration: 300 }
            Tween { target: root.player; property: "headPitch"; to: 36; duration: 320 }
            Tween { target: root.player; property: "bodyPitch"; to: 6; duration: 320 }
            Tween { target: root.player; properties: "rightArmRoll,leftArmRoll"; to: -14; duration: 320 }
            Tween { target: root.player; properties: "rightArmPitch,leftArmPitch"; to: -10; duration: 320 }
        }
        ParallelAnimation {
            Tween { target: root.player; property: "turn"; to: -34; duration: 440 }
            Tween { target: root.player; property: "headYaw"; to: 12; duration: 440 }
        }
        PauseAnimation { duration: 140 }
        ParallelAnimation {
            Tween { target: root.player; property: "turn"; to: 34; duration: 640 }
            Tween { target: root.player; property: "headYaw"; to: -12; duration: 640 }
        }
        PauseAnimation { duration: 140 }
        ParallelAnimation {
            Tween { target: root.player; property: "turn"; to: 0; duration: 380 }
            Tween { target: root.player; property: "headYaw"; to: 0; duration: 380 }
            Tween { target: root.player; property: "headPitch"; to: -8; duration: 380 }
            Tween { target: root.player; property: "bodyPitch"; to: -2; duration: 380 }
            Tween { target: root.player; properties: "rightArmPitch,leftArmPitch"; to: 0; duration: 300 }
            Tween { target: root.player; properties: "rightArmRoll,leftArmRoll"; to: -3; duration: 300 }
        }
        // Ta-da.
        ParallelAnimation {
            Tween { target: root.player; property: "rise"; to: 1.6; duration: 180; easing.type: Easing.OutQuad }
            Tween { target: root.player; properties: "rightArmRoll,leftArmRoll"; to: -62; duration: 240; easing.type: Easing.OutBack }
            Tween { target: root.player; properties: "rightArmPitch,leftArmPitch"; to: 14; duration: 240 }
            Tween { target: root.player; property: "headRoll"; to: -8; duration: 240 }
        }
        Tween { target: root.player; property: "rise"; to: 0; duration: 300; easing.type: Easing.OutBounce }
        PauseAnimation { duration: 380 }
        ScriptAction { script: root.settle() }
    }

    // A crouch, a spin in the air with the arms out, and a pose.
    SequentialAnimation {
        id: twirl
        ParallelAnimation {
            Tween { target: root.player; property: "follow"; to: 0; duration: 140 }
            Tween { target: root.player; property: "turn"; to: 0; duration: 160 }
            Tween { target: root.player; property: "rise"; to: -1.2; duration: 160; easing.type: Easing.OutQuad }
            Tween { target: root.player; property: "bodyPitch"; to: 10; duration: 160 }
            Tween { target: root.player; property: "headPitch"; to: 8; duration: 160 }
            Tween { target: root.player; properties: "rightArmPitch,leftArmPitch"; to: -24; duration: 160 }
        }
        ParallelAnimation {
            Tween { target: root.player; property: "turn"; to: 360; duration: 700; easing.type: Easing.OutCubic }
            SequentialAnimation {
                Tween { target: root.player; property: "rise"; to: 3.4; duration: 230; easing.type: Easing.OutQuad }
                Tween { target: root.player; property: "rise"; to: 0; duration: 420; easing.type: Easing.OutBounce }
            }
            Tween { target: root.player; property: "bodyPitch"; to: 0; duration: 230 }
            Tween { target: root.player; property: "headPitch"; to: -10; duration: 230 }
            Tween { target: root.player; properties: "rightArmPitch,leftArmPitch"; to: 0; duration: 230 }
            SequentialAnimation {
                Tween { target: root.player; properties: "rightArmRoll,leftArmRoll"; to: -74; duration: 230; easing.type: Easing.OutQuad }
                Tween { target: root.player; properties: "rightArmRoll,leftArmRoll"; to: -20; duration: 420; easing.type: Easing.OutBack }
            }
            SequentialAnimation {
                Tween { target: root.player; property: "rightLegPitch"; to: 28; duration: 230 }
                Tween { target: root.player; property: "rightLegPitch"; to: 0; duration: 300 }
            }
            SequentialAnimation {
                Tween { target: root.player; property: "leftLegPitch"; to: -18; duration: 230 }
                Tween { target: root.player; property: "leftLegPitch"; to: 0; duration: 300 }
            }
            Tween { target: root.player; property: "capeSwing"; to: 40; duration: 300; easing.type: Easing.OutQuad }
        }
        ScriptAction { script: root.player.turn = 0 }
        // The pose: chin up, head tilted, the right arm up.
        ParallelAnimation {
            Tween { target: root.player; property: "capeSwing"; to: 9; duration: 420; easing.type: Easing.OutBack }
            Tween { target: root.player; property: "headRoll"; to: 10; duration: 260; easing.type: Easing.OutBack }
            Tween { target: root.player; property: "headPitch"; to: -6; duration: 260 }
            Tween { target: root.player; property: "rightArmPitch"; to: 150; duration: 300; easing.type: Easing.OutBack }
            Tween { target: root.player; property: "rightArmRoll"; to: 16; duration: 300 }
            Tween { target: root.player; property: "bodyRoll"; to: -3; duration: 260 }
        }
        PauseAnimation { duration: 520 }
        ScriptAction { script: root.settle() }
    }

    // Turns its back to show the cape, looks over its shoulder, gives it a swish.
    SequentialAnimation {
        id: capeCheck
        ParallelAnimation {
            Tween { target: root.player; property: "follow"; to: 0; duration: 200 }
            Tween { target: root.player; property: "turn"; to: 160; duration: 620; easing.type: Easing.InOutCubic }
            Tween { target: root.player; property: "headYaw"; to: -58; duration: 620 }
            Tween { target: root.player; property: "headPitch"; to: 10; duration: 620 }
            Tween { target: root.player; properties: "rightArmRoll,leftArmRoll"; to: -12; duration: 620 }
        }
        SequentialAnimation {
            loops: 2
            ParallelAnimation {
                Tween { target: root.player; property: "capeSwing"; to: 38; duration: 190; easing.type: Easing.OutQuad }
                Tween { target: root.player; property: "rise"; to: 1.4; duration: 190; easing.type: Easing.OutQuad }
                Tween { target: root.player; property: "bodyRoll"; to: 4; duration: 190 }
            }
            ParallelAnimation {
                Tween { target: root.player; property: "capeSwing"; to: 9; duration: 340; easing.type: Easing.OutBack }
                Tween { target: root.player; property: "rise"; to: 0; duration: 340; easing.type: Easing.OutBounce }
                Tween { target: root.player; property: "bodyRoll"; to: -2; duration: 340 }
            }
        }
        PauseAnimation { duration: 260 }
        ParallelAnimation {
            Tween { target: root.player; property: "turn"; to: 360; duration: 640; easing.type: Easing.InOutCubic }
            Tween { target: root.player; property: "headYaw"; to: 0; duration: 640 }
            Tween { target: root.player; property: "headPitch"; to: 0; duration: 640 }
            Tween { target: root.player; property: "bodyRoll"; to: 0; duration: 640 }
        }
        ScriptAction { script: root.player.turn = 0 }
        // A nod at the result.
        Tween { target: root.player; property: "headPitch"; to: 12; duration: 150; easing.type: Easing.OutQuad }
        Tween { target: root.player; property: "headPitch"; to: 0; duration: 240; easing.type: Easing.OutBack }
        ScriptAction { script: root.settle() }
    }

    // Idle: a look to each side…
    SequentialAnimation {
        id: glance
        Tween { target: root.player; property: "follow"; to: 0; duration: 300 }
        Tween { target: root.player; property: "headYaw"; to: -38; duration: 440 }
        PauseAnimation { duration: 520 }
        Tween { target: root.player; property: "headYaw"; to: 34; duration: 640 }
        PauseAnimation { duration: 420 }
        ScriptAction { script: root.settle() }
    }
    // …or its weight from one foot to the other.
    SequentialAnimation {
        id: shift
        ParallelAnimation {
            Tween { target: root.player; property: "tilt"; to: 2.5; duration: 620 }
            Tween { target: root.player; property: "bodyRoll"; to: -2.5; duration: 620 }
            Tween { target: root.player; property: "headRoll"; to: 4; duration: 620 }
            Tween { target: root.player; property: "leftLegPitch"; to: 8; duration: 620 }
        }
        PauseAnimation { duration: 900 }
        ScriptAction { script: root.settle() }
    }
}
