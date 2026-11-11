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
        objectName: "checkCardCorrectness"
        text: "Check correctness"
        hint: "Ask the configured model to check facts, logic, and whether the answer is correct. Uses this unsaved draft."
        Layout.fillWidth: true
        onClicked: actions.checkRequested("correctness")
    }
    AppButton {
        objectName: "checkCardCompleteness"
        text: "Check completeness"
        hint: "Ask the configured model to identify missing parts, qualifications, or partial truths within this card's scope. Uses this unsaved draft."
        Layout.fillWidth: true
        onClicked: actions.checkRequested("completeness")
    }
}
