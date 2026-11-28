import QtQuick

// A media tile on its way into the service list (#99): MediaPane drags it, ServicePane reads
// what to add from it.
Item {
    property string assetId
    property string mediaTitle
    Drag.keys: ["application/x-visualis-media"]
}
