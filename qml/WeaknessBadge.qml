import QtQuick
import QtQuick.Controls

Item {
    id: badge
    property var assessment: ({})
    property bool compact: false
    property bool prominent: false
    readonly property string tier: String(assessment && assessment.tier || "unassessed")
    readonly property bool assessed: !!(assessment && assessment.assessed)
    readonly property string label: String(assessment && assessment.label || (tier === "unassessed" ? "Not assessed" : tier))
    readonly property int tierLevel: tier === "steady" ? 3 : tier === "watch" ? 2 : tier === "fragile" ? 1 : 0
    readonly property color tierColor: {
        switch (tier) {
        case "needs-study":
            return Theme.weaknessNeedsStudy;
        case "fragile":
            return Theme.weaknessFragile;
        case "watch":
            return Theme.weaknessWatch;
        case "steady":
            return Theme.weaknessSteady;
        default:
            return Theme.weaknessNeutral;
        }
    }
    implicitWidth: markerRow.implicitWidth + (prominent ? 44 : 20)
    implicitHeight: prominent ? 60 : compact ? 24 : 30
    visible: assessed || tier !== "unassessed"

    Rectangle {
        id: body
        anchors.fill: parent
        radius: height / 2
        color: Theme.transparent
        border.width: badge.prominent || tier === "needs-study" ? 2 : 1
        border.color: tierColor
        Row {
            id: markerRow
            anchors.centerIn: parent
            spacing: badge.prominent ? 10 : 6
            Repeater {
                model: compact ? 0 : 3
                Rectangle {
                    width: badge.prominent ? 5 : 3
                    height: badge.prominent ? 6 + index * 5 : 3 + index * 3
                    radius: 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: index < badge.tierLevel ? badge.tierColor : Theme.rule
                }
            }
            Label {
                text: badge.label + (badge.prominent && badge.assessed ? "  " + Math.round(Number(badge.assessment.recallStrength)) + "/100" : "")
                color: badge.tierColor
                font.family: Theme.monoFont
                font.pixelSize: badge.prominent ? 22 : compact ? 11 : 13
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
    HoverHandler {
        id: hover
    }
    ToolTip.visible: hover.hovered
    ToolTip.text: {
        if (!badge.assessed)
            return "Review this card to establish its recall strength.";
        const count = Number(badge.assessment.sampleCount || 0);
        const early = badge.assessment.provisional ? " Early estimate based on " + count + (count === 1 ? " review." : " reviews.") : "";
        return "Recall strength: " + badge.label + ", " + Math.round(Number(badge.assessment.recallStrength)) + "/100; higher is stronger." + early;
    }
}
