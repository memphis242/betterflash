pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var ui
    property string checkMode: "correctness"
    property string checkedFront: ""
    property string checkedBack: ""
    property string checkedKind: "basic"
    property string modelLabel: ""
    property bool searchAllowed: false
    property bool savedCard: false
    property string checkedCardId: ""
    readonly property var report: cardChecker.result
    property var contextFailure: ({})
    readonly property var failure: contextFailure.message ? contextFailure : cardChecker.errorInfo
    readonly property bool hasResult: !!report.verdict
    readonly property color resultColor: report.verdict === "no_issues" ? Theme.success : Theme.warning
    title: checkMode === "correctness" ? "Correctness check" : "Completeness check"
    objectName: "cardCheckDialog"
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Math.min(820, ui.width - 2 * ui.gutter)
    height: Math.min(720, ui.height - 2 * ui.gutter)
    padding: width < 600 ? 14 : 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: Theme.surface
        radius: 12
        border.color: Theme.rule
    }
    Overlay.modal: Rectangle { color: Theme.scrim }
    function openFor(mode, front, back, kind, saved = false, cardId = "") {
        cardChecker.clear()
        checkMode = mode
        checkedFront = front
        checkedBack = back
        checkedKind = kind
        savedCard = saved
        checkedCardId = cardId
        open()
        runCheck()
    }
    function runCheck(allowSearch = true) {
        contextFailure = ({})
        const saved = checkedCardId ? ui.cardById(checkedCardId) : ({})
        if (savedCard && checkedCardId && (!saved.id || saved.front !== checkedFront || saved.back !== checkedBack || saved.kind !== checkedKind)) {
            cardChecker.clear()
            contextFailure = ({code: "CARD_CHECK_STALE", message: "This saved card changed or was deleted after the check was opened.", action: "Return to the card and start a new check of its current content. Previous follow-up marks were kept."})
            return
        }
        modelLabel = ai.provider === "local" ? ai.localModelLabel : ai.model
        searchAllowed = allowSearch && webSearch.enabled
        resultScroll.contentItem.contentY = 0
        cardChecker.check(checkMode, checkedFront, checkedBack, checkedKind, allowSearch, checkedCardId, saved)
    }
    onClosed: cardChecker.clear()
    header: Item {
        implicitHeight: 64
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: dialog.padding
            anchors.rightMargin: dialog.padding
            spacing: 12
            Label {
                text: dialog.title
                textFormat: Text.PlainText
                color: Theme.ink
                font.pixelSize: 23
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            GlyphButton {
                glyph: "close"
                hint: cardChecker.busy ? "Close and cancel this check" : "Return to the card draft"
                onClicked: dialog.close()
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 12
        ModelCacheControl { id: checkCache; operation: cardChecker; Layout.fillWidth: true }
        Label {
            text: "LLMs can be wrong. Their knowledge may have a cutoff date and miss recent changes. Web search can help, but does not guarantee correctness or completeness."
            textFormat: Text.PlainText
            color: Theme.warning
            font.pixelSize: 15
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            text: dialog.checkMode === "correctness"
                ? (dialog.savedCard ? "Checks this saved card's factual and logical accuracy." : "Checks the current draft's factual and logical accuracy.") + " Attached images are not inspected."
                : (dialog.savedCard ? "Looks for missing parts and qualifications in this saved card's answer, within its question's scope." : "Looks for missing parts and qualifications needed to answer this card's question fully, within its scope.") + " Attached images are not inspected."
            textFormat: Text.PlainText
            color: Theme.ink
            font.pixelSize: 15
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            text: "Model: " + dialog.modelLabel
                + "\n" + (dialog.hasResult
                    ? (dialog.report.searchCount > 0
                        ? "Web search used: " + dialog.report.searchCount + " searches; " + (dialog.report.sources || []).length + " sources retrieved."
                            + ((dialog.report.sources || []).length === 0 ? " No usable source snippets were available to the model." : "")
                            + (dialog.report.evidenceTruncated ? " Source snippets were shortened to fit the model's context." : "")
                        : "No web search was performed. This result uses the model's intrinsic knowledge.")
                    : dialog.searchAllowed
                        ? "Web search allowed through Tavily (up to 3 searches)."
                        : webSearch.enabled
                            ? "Web search skipped for this check. Uses the model's intrinsic knowledge."
                            : "Web search is off. Uses the model's intrinsic knowledge. Enable search in Settings > Web search.")
            textFormat: Text.PlainText
            color: Theme.inkMuted
            font.pixelSize: 13
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            visible: cardChecker.busy || (!dialog.hasResult && !dialog.failure.message && !!cardChecker.status)
            text: cardChecker.status
            textFormat: Text.PlainText
            color: Theme.ink
            font.pixelSize: 17
            font.bold: true
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        ProgressBar {
            visible: cardChecker.busy
            indeterminate: true
            Layout.fillWidth: true
        }
        AppButton {
            visible: cardChecker.busy
            text: "Cancel check"
            fill: Theme.cancelActionFill
            hoverFill: Theme.cancelActionHover
            outline: Theme.error
            focusOutline: Theme.error
            Layout.fillWidth: true
            onClicked: cardChecker.cancel()
        }
        AppButton {
            visible: !cardChecker.busy && !!dialog.failure.message && dialog.searchAllowed
            text: "Retry without web search"
            hint: "Check this card snapshot using the model's intrinsic knowledge. Keeps your global web search setting."
            Layout.fillWidth: true
            onClicked: dialog.runCheck(false)
        }
        ScrollView {
            id: resultScroll
            objectName: "cardCheckResultScroll"
            clip: true
            contentWidth: availableWidth
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: resultScroll.availableWidth
                spacing: 14
                Rectangle {
                    visible: !!dialog.failure.message
                    Layout.fillWidth: true
                    implicitHeight: checkErrorContent.implicitHeight + 24
                    color: Theme.surfaceRaised
                    border.color: Theme.error
                    radius: 6
                    ColumnLayout {
                        id: checkErrorContent
                        x: 12; y: 12; width: parent.width - 24
                        spacing: 10
                        Label {
                            text: dialog.failure.message || ""
                            textFormat: Text.PlainText
                            color: Theme.error
                            font.pixelSize: 18
                            font.bold: true
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Accessible.role: Accessible.AlertMessage
                        }
                        Label {
                            text: dialog.failure.action || ""
                            textFormat: Text.PlainText
                            color: Theme.ink
                            font.pixelSize: 16
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        ErrorBlock {
                            Layout.fillWidth: true
                            code: dialog.failure.code || ""
                            message: code.length ? "Check details" : ""
                            details: dialog.failure.detail || ""
                            textSize: 14
                        }
                    }
                }
                Rectangle {
                    visible: dialog.hasResult
                    Layout.fillWidth: true
                    implicitHeight: resultHeading.implicitHeight + 24
                    color: Theme.surfaceRaised
                    border.color: dialog.resultColor
                    radius: 6
                    ColumnLayout {
                        id: resultHeading
                        x: 12; y: 12; width: parent.width - 24
                        spacing: 10
                        Label {
                            text: {
                                switch (dialog.report.verdict) {
                                case "no_issues": return dialog.checkMode === "correctness" ? "No issues identified" : "No omissions identified within this card's scope"
                                case "issues": return dialog.checkMode === "correctness" ? "Possible correctness issues" : "Possible gaps or missing qualifications"
                                default: return "The model could not reach a clear conclusion"
                                }
                            }
                            textFormat: Text.PlainText
                            color: dialog.resultColor
                            font.pixelSize: 19
                            font.bold: true
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        TextEdit {
                            text: dialog.report.summary || ""
                            textFormat: TextEdit.PlainText
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            font.family: Theme.uiFont
                            font.pixelSize: 16
                            color: Theme.ink
                            Layout.fillWidth: true
                        }
                    }
                }
                Repeater {
                    model: dialog.report.findings || []
                    delegate: ColumnLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.rule }
                        Label {
                            text: (index + 1) + ". " + modelData.claim
                            textFormat: Text.PlainText
                            color: Theme.ink
                            font.pixelSize: 17
                            font.bold: true
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        TextEdit {
                            text: modelData.explanation + (modelData.suggestion ? "\n\nSuggested adjustment: " + modelData.suggestion : "")
                                + ((modelData.sourceIds || []).length ? "\n\nSources: " + modelData.sourceIds.join(", ") : "")
                            textFormat: TextEdit.PlainText
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            font.family: Theme.uiFont
                            font.pixelSize: 16
                            color: Theme.ink
                            Layout.fillWidth: true
                        }
                    }
                }
                Label {
                    visible: (dialog.report.sources || []).length > 0
                    text: "Retrieved sources"
                    textFormat: Text.PlainText
                    color: Theme.ink
                    font.pixelSize: 17
                    font.bold: true
                }
                Repeater {
                    model: dialog.report.sources || []
                    delegate: AppButton {
                        required property var modelData
                        text: modelData.id + ": " + modelData.title
                        hint: modelData.url
                        Accessible.description: modelData.url
                        Layout.fillWidth: true
                        onClicked: Qt.openUrlExternally(modelData.url)
                    }
                }
            }
        }
    }
    footer: Flow {
        padding: 12
        spacing: 10
        AppButton {
            text: "Run fresh"
            visible: !cardChecker.busy
            enabled: !dialog.contextFailure.message
            hint: "Run this card check again without using cached model responses. May use provider quota."
            onClicked: { checkCache.requestFresh(); dialog.runCheck(dialog.searchAllowed) }
        }
        AppButton {
            text: "Run again"
            visible: !cardChecker.busy
            hint: "Run the same check on this card snapshot again"
            onClicked: dialog.runCheck(dialog.searchAllowed)
        }
        AppButton {
            text: "Back to card"
            onClicked: dialog.close()
        }
    }
}
