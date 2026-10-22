import QtQuick

// Shared drag contract for every floating app surface. Child controls are
// siblings above this area and keep their own input. Adaptive apps stay fixed.
MouseArea {
    id: handle
    required property Item windowItem
    property bool movable: true
    enabled: movable
    property point pressPosition
    property point windowPosition
    property bool moving: false
    function moveWindow(mouse) {
        if (!windowItem.parent) return
        const p = mapToItem(windowItem.parent, mouse.x, mouse.y)
        const dx = p.x - pressPosition.x, dy = p.y - pressPosition.y
        if (!moving && Math.hypot(dx, dy) < Qt.styleHints.startDragDistance) return
        moving = true
        windowItem.x = Math.max(-windowItem.width + 120, Math.min(windowItem.parent.width - 120, windowPosition.x + dx))
        windowItem.y = Math.max(47, Math.min(windowItem.parent.height - 60, windowPosition.y + dy))
    }
    onPressed: mouse => {
        pressPosition = mapToItem(windowItem.parent, mouse.x, mouse.y)
        windowPosition = Qt.point(windowItem.x, windowItem.y)
        moving = false
    }
    onPositionChanged: mouse => { if (pressed) moveWindow(mouse) }
    onReleased: mouse => moveWindow(mouse)
}
