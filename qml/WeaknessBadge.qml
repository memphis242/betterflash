import QtQuick
import QtQuick.Controls

Item {
    id: badge
    property var assessment: ({})
    property bool compact: false
    readonly property string tier: String(assessment && assessment.tier || "unassessed")
    readonly property bool assessed: !!(assessment && assessment.assessed)
    readonly property string label: String(assessment && assessment.label || (tier === "unassessed" ? "Not assessed" : tier))
    readonly property int tierLevel: tier === "needs-study" ? 3 : tier === "fragile" ? 2 : tier === "watch" ? 1 : 0
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
    implicitWidth: markerRow.implicitWidth + 20
    implicitHeight: compact ? 24 : 30
    visible: assessed || tier !== "unassessed"

    Rectangle {
        id: body
        anchors.fill: parent
        radius: height / 2
        color: Theme.transparent
        border.width: tier === "needs-study" ? 2 : 1
        border.color: tierColor
        Row {
            id: markerRow
            anchors.centerIn: parent
            spacing: 6
            Repeater {
                model: compact ? 0 : 3
                Rectangle {
                    width: 3
                    height: 3 + index * 3
                    radius: 2
                    anchors.verticalCenter: parent.verticalCenter
                    color: index < badge.tierLevel ? badge.tierColor : Theme.rule
                }
            }
            Label {
                text: badge.label + (badge.assessed && badge.assessment.provisional ? " · early" : "")
                color: badge.tierColor
                font.family: Theme.monoFont
                font.pixelSize: compact ? 11 : 13
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
        const score = Number(badge.assessment.score);
        const evidence = badge.assessment.reasons instanceof Array && badge.assessment.reasons.length ? " " + badge.assessment.reasons.join(" ") : "";
        const provisional = badge.assessment.provisional ? " This is provisional while review history is sparse." : "";
        return (badge.assessment.recommendation || badge.label) + (Number.isFinite(score) ? " Weakness score " + Math.round(score) + "/100." : ".") + evidence + provisional;
    }
}
