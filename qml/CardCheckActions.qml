import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridLayout {
    id: actions
    signal checkRequested(string mode)
    columns: width < 360 ? 1 : 2
    columnSpacing: 10
    rowSpacing: 8
    AppButton {
        fill: Theme.checkActionFill
        hoverFill: Theme.checkActionHover
        outline: Theme.checkActionOutline
        focusOutline: Theme.checkActionOutline
        foreground: Theme.checkActionInk
        objectName: "checkCardCorrectness"
        text: "Check correctness"
        hint: "Ask the configured model to check facts, logic, and whether the answer is correct. Uses this unsaved draft."
        Layout.fillWidth: true
        onClicked: actions.checkRequested("correctness")
    }
    AppButton {
        fill: Theme.completenessActionFill
        hoverFill: Theme.completenessActionHover
        outline: Theme.checkActionOutline
        focusOutline: Theme.checkActionOutline
        foreground: Theme.checkActionInk
        objectName: "checkCardCompleteness"
        text: "Check completeness"
        hint: "Ask the configured model to identify missing parts, qualifications, or partial truths within this card's scope. Uses this unsaved draft."
        Layout.fillWidth: true
        onClicked: actions.checkRequested("completeness")
    }
}
