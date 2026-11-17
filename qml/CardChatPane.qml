pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: pane
    required property var ui
    signal closeRequested()

    property var drafts: ({})
    property string draftKey: ""
    property bool restoringDraft: false
    property int previousMessageCount: 0
    property bool followTranscript: true
    property bool updatingTranscript: false
    property int transcriptRevision: 0
    property var transcriptMessages: []
    property string transcriptSnapshot: ""
    readonly property var failure: cardChat.errorInfo
    readonly property bool modalDialogOpen: confirmDialog.visible || evidenceDialog.visible
    readonly property string chatWarning: "The model receives this card and your messages. It can retrieve related saved cards and, if enabled, search the web. Chats stay on this device. LLMs can be wrong and miss recent changes."
    onVisibleChanged: if (!visible && evidenceDialog) evidenceDialog.close()

    function protectedDraftThreadIds(cardId) {
        const prefix = cardId + ":"
        const protectedIds = []
        Object.keys(drafts).forEach(key => {
            if (key.startsWith(prefix) && String(drafts[key] || "").trim())
                protectedIds.push(key.slice(prefix.length))
        })
        if (draftKey.startsWith(prefix) && composer.text.trim()) {
            const currentId = draftKey.slice(prefix.length)
            if (protectedIds.indexOf(currentId) < 0) protectedIds.push(currentId)
        }
        return protectedIds
    }

    function focusComposer() {
        if (!composer.readOnly) composer.forceActiveFocus()
    }
    function synchronizeDraft() {
        const next = cardChat.cardId && cardChat.threadId ? cardChat.cardId + ":" + cardChat.threadId : ""
        if (next === draftKey) return
        if (draftKey) {
            const saved = Object.assign({}, drafts)
            saved[draftKey] = composer.text
            drafts = saved
        }
        draftKey = next
        restoringDraft = true
        composer.text = next ? (drafts[next] || "") : ""
        restoringDraft = false
        followTranscript = true
        previousMessageCount = cardChat.messages.length
        Qt.callLater(scrollToBottom)
    }
    function scrollToBottom() {
        if (!followTranscript || transcript.moving || transcript.dragging || transcript.flicking || transcriptBar.pressed) return
        transcript.positionViewAtEnd()
    }
    function cancelDeferredTranscriptPosition() {
        ++transcriptRevision
        updatingTranscript = false
    }
    function sendQuestion() {
        const question = composer.text
        if (!question.trim() || question.length > 4000 || cardChat.busy || cardChat.stale || cardChat.pendingReply || cardChat.dirty || !cardChat.threadId) return
        if (cardChat.send(question)) {
            composer.text = ""
            followTranscript = true
            Qt.callLater(scrollToBottom)
        }
    }
    function refreshChatState() {
        const previousKey = draftKey
        synchronizeDraft()
        const rows = cardChat.messages
        const snapshot = JSON.stringify(rows)
        if (snapshot !== transcriptSnapshot || previousKey !== draftKey) {
            transcript.forceLayout()
            const anchorIndex = transcript.indexAt(1, transcript.contentY + 1)
            const anchorItem = anchorIndex >= 0 ? transcript.itemAtIndex(anchorIndex) : null
            const anchorOffset = anchorItem ? transcript.contentY - anchorItem.y : 0
            const shouldFollow = followTranscript
            const revision = ++transcriptRevision
            const threadKey = draftKey
            updatingTranscript = true
            transcriptSnapshot = snapshot
            transcriptMessages = rows
            previousMessageCount = rows.length
            Qt.callLater(function() {
                if (revision !== pane.transcriptRevision || threadKey !== pane.draftKey
                    || snapshot !== pane.transcriptSnapshot) return
                if (transcript.moving || transcript.dragging || transcript.flicking || transcriptBar.pressed) {
                    pane.updatingTranscript = false
                    return
                }
                transcript.forceLayout()
                if (shouldFollow) pane.scrollToBottom()
                else if (anchorIndex >= 0 && anchorIndex < transcript.count) {
                    transcript.positionViewAtIndex(anchorIndex, ListView.Beginning)
                    transcript.forceLayout()
                    const item = transcript.itemAtIndex(anchorIndex)
                    if (item) transcript.contentY = item.y + anchorOffset
                }
                pane.updatingTranscript = false
            })
        }
        const index = threadPicker.indexOfValue(cardChat.threadId)
        if (!threadPicker.popup.visible && threadPicker.currentIndex !== index)
            threadPicker.currentIndex = index
    }
    Connections {
        target: cardChat
        function onChanged() { pane.refreshChatState() }
    }
    Component.onCompleted: refreshChatState()

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: "Ask AI"
                color: Theme.ink
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            GlyphButton { glyph: "close"; hint: "Close Ask AI"; onClicked: pane.closeRequested() }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppComboBox {
                id: threadPicker
                objectName: "cardChatThreads"
                model: cardChat.threads
                textRole: "title"
                valueRole: "id"
                enabled: !cardChat.busy && !cardChat.dirty && count > 0
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                comboHint: "Choose a conversation for this card. Chats are saved on this device."
                displayText: currentIndex < 0 ? "No chat selected" : currentText
                onActivated: {
                    if (!cardChat.selectThread(currentValue || ""))
                        currentIndex = indexOfValue(cardChat.threadId)
                }
            }
            AppButton {
                text: "New chat"
                hint: "Start a new conversation about this card"
                enabled: !!pane.ui.cardById(cardChat.cardId).id && !cardChat.busy && !cardChat.dirty
                onClicked: cardChat.newThread()
            }
            AppButton {
                text: "Delete"
                hint: "Delete this chat"
                enabled: !!cardChat.threadId && !cardChat.busy && !cardChat.dirty
                onClicked: {
                    confirmDialog.targetThread = cardChat.threadId
                    confirmDialog.action = "delete"
                    confirmDialog.open()
                }
            }
        }
        Label {
            text: pane.chatWarning
            textFormat: Text.PlainText
            color: Theme.inkMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            text: "Model: " + (ai.provider === "local" ? ai.localModelLabel : ai.model) + ". Only read-only retrieval tools are available."
            color: Theme.inkMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Label {
            visible: cardChat.busy
            text: cardChat.status || "Thinking..."
            color: Theme.ink
            font.bold: true
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        ProgressBar { visible: cardChat.busy; indeterminate: true; Layout.fillWidth: true }
        Flow {
            visible: cardChat.busy
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: cardChat.toolActivity || []
                delegate: ChatEvidenceBadge {
                    required property var modelData
                    evidence: modelData
                    maximumWidth: Math.max(0, pane.width)
                    onClicked: evidenceDialog.openFor(evidence, false)
                }
            }
        }
        Label {
            visible: cardChat.stale
            text: "This card changed since this chat began. Start a new chat to use its current content."
            color: Theme.warning
            font.bold: true
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        Rectangle {
            visible: !!pane.failure.message
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(160, Math.max(60, pane.height * 0.22), chatError.implicitHeight + 24)
            color: Theme.errorSurface
            border.color: Theme.error
            border.width: 1
            radius: 6
            ScrollView {
                id: alerts
                anchors.fill: parent
                anchors.margins: 12
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ErrorBlock {
                    id: chatError
                    width: alerts.availableWidth
                    code: pane.failure.code || ""
                    message: pane.failure.message || ""
                    action: pane.failure.action || ""
                    details: pane.failure.detail || ""
                    textSize: 14
                }
            }
        }
        ListView {
            id: transcript
            objectName: "cardChatTranscript"
            clip: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            model: pane.transcriptMessages
            reuseItems: true
            cacheBuffer: 0
            spacing: 12
            boundsBehavior: Flickable.StopAtBounds
            readonly property real messageWidth: Math.max(0, width - transcriptBar.width - 4)
            function updateFollowPosition() {
                if (!pane.updatingTranscript)
                    pane.followTranscript = contentY + height >= originY + contentHeight - 32
            }
            onContentYChanged: {
                if (moving || dragging || flicking || transcriptBar.pressed) {
                    if (pane.updatingTranscript) pane.cancelDeferredTranscriptPosition()
                    updateFollowPosition()
                }
            }
            onMovementStarted: {
                pane.cancelDeferredTranscriptPosition()
                pane.followTranscript = false
            }
            onMovementEnded: updateFollowPosition()
            onContentHeightChanged: Qt.callLater(pane.scrollToBottom)
            onHeightChanged: Qt.callLater(pane.scrollToBottom)
            ScrollBar.vertical: ScrollBar {
                id: transcriptBar
                policy: ScrollBar.AsNeeded
                onPressedChanged: {
                    if (pressed) {
                        pane.cancelDeferredTranscriptPosition()
                        pane.followTranscript = false
                    }
                    else transcript.updateFollowPosition()
                }
            }
            header: ColumnLayout {
                width: transcript.messageWidth
                spacing: 12
                Label {
                    visible: !cardChat.messages.length && !pane.failure.message
                    text: "Ask a question about this card to start the conversation."
                    color: Theme.inkMuted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Label {
                    visible: !!cardChat.contextNotice
                    text: cardChat.contextNotice
                    textFormat: Text.PlainText
                    color: Theme.inkMuted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
            delegate: ColumnLayout {
                id: entry
                required property var modelData
                required property int index
                property bool pooled: false
                width: transcript.messageWidth
                spacing: 7
                ListView.onPooled: pooled = true
                ListView.onReused: pooled = false
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.rule }
                Label {
                    text: entry.modelData.role === "user" ? "You" : "Assistant"
                    color: Theme.ink
                    font.bold: true
                    Layout.fillWidth: true
                }
                Loader {
                    active: !entry.pooled
                    Layout.fillWidth: true
                    Layout.preferredHeight: item ? item.implicitHeight : 20
                    sourceComponent: MarkdownPane {
                        markdown: entry.modelData.content || ""
                        baseFontSize: 15
                        selectable: true
                        mediaRoot: ""
                        onImplicitHeightChanged: Qt.callLater(pane.scrollToBottom)
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: entry.modelData.toolCalls || []
                        delegate: ChatEvidenceBadge {
                            required property var modelData
                            evidence: modelData
                            maximumWidth: transcript.messageWidth
                            onClicked: evidenceDialog.openFor(evidence, false)
                        }
                    }
                    Repeater {
                        model: entry.modelData.sources || []
                        delegate: ChatEvidenceBadge {
                            required property var modelData
                            evidence: modelData
                            source: true
                            maximumWidth: transcript.messageWidth
                            onClicked: evidenceDialog.openFor(evidence, true)
                        }
                    }
                }
            }
        }
        Flow {
            visible: cardChat.pendingReply && !cardChat.busy || cardChat.dirty || pane.failure.code === "CARD_CHAT_STORAGE"
            Layout.fillWidth: true
            spacing: 8
            AppButton {
                visible: cardChat.pendingReply && !cardChat.busy
                text: "Retry"
                enabled: !cardChat.stale && !cardChat.dirty
                hint: "Retry the unanswered question"
                onClicked: cardChat.retry()
            }
            AppButton {
                visible: cardChat.pendingReply && !cardChat.busy
                text: "Discard question"
                enabled: !cardChat.dirty
                hint: "Remove the unanswered question and keep the earlier conversation"
                onClicked: cardChat.discardPending()
            }
            AppButton {
                visible: pane.failure.code === "CARD_CHAT_STORAGE" && !cardChat.dirty
                text: "Reload saved chats"
                enabled: !cardChat.busy
                hint: "Refresh conversations saved on this device before retrying the action. Keeps unsent question drafts."
                onClicked: cardChat.reload()
            }
            AppButton {
                visible: cardChat.dirty
                text: "Retry saving"
                enabled: !cardChat.busy
                onClicked: cardChat.retrySave()
            }
            AppButton {
                visible: cardChat.dirty
                text: "Discard unsaved changes"
                enabled: !cardChat.busy
                onClicked: { confirmDialog.action = "discard"; confirmDialog.open() }
            }
        }
        ModelCacheControl { operation: cardChat; Layout.fillWidth: true }
        Label {
            text: "Your question"
            color: Theme.ink
            font.bold: true
            Layout.fillWidth: true
        }
        ScrollView {
            id: composerViewport
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            contentWidth: availableWidth
            clip: true
            TextArea {
                id: composer
                ContextMenu.menu: TextEditingMenu { editor: composer }
                objectName: "cardChatComposer"
                width: composerViewport.availableWidth
                height: Math.max(composerViewport.availableHeight, implicitHeight)
                readOnly: cardChat.busy || cardChat.pendingReply || cardChat.stale || cardChat.dirty || !cardChat.threadId
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                padding: 10
                color: Theme.ink
                placeholderText: "Ask about this card..."
                Accessible.name: "Your question"
                background: Rectangle {
                    radius: 6
                    color: Theme.editorField
                    border.color: composer.activeFocus ? Theme.accent : Theme.rule
                    border.width: composer.activeFocus ? 2 : 1
                }
                HoverHandler { cursorShape: Qt.IBeamCursor }
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                        pane.sendQuestion()
                        event.accepted = true
                    }
                }
                onTextChanged: {
                    if (pane.restoringDraft || !pane.draftKey) return
                    const updated = Object.assign({}, pane.drafts)
                    updated[pane.draftKey] = text
                    pane.drafts = updated
                }
            }
        }
        Label {
            text: pane.chatWarning
            textFormat: Text.PlainText
            color: Theme.inkMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: composer.text.length > 3600 ? composer.text.length + " / 4000" : "Ctrl+Enter to send"
                color: composer.text.length > 4000 ? Theme.error : Theme.inkMuted
                font.pixelSize: 12
                Layout.fillWidth: true
            }
            AppButton {
                visible: cardChat.busy
                text: "Cancel"
                fill: Theme.cancelActionFill
                hoverFill: Theme.cancelActionHover
                outline: Theme.error
                onClicked: cardChat.cancel()
            }
            AppButton {
                visible: !cardChat.busy
                objectName: "sendCardChat"
                text: "Send"
                hint: webSearch.enabled ? "Send this question. The model may use up to 3 Tavily searches from your quota." : "Send this question using the configured model and read-only saved-card retrieval."
                primary: true
                enabled: !!cardChat.threadId && !cardChat.pendingReply && !cardChat.stale && !cardChat.dirty && composer.text.trim().length > 0 && composer.text.length <= 4000
                onClicked: pane.sendQuestion()
            }
        }
    }
    ChatEvidenceDialog {
        id: evidenceDialog
        ui: pane.ui
    }
    Dialog {
        id: confirmDialog
        property string action: ""
        property string targetThread: ""
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: Math.min(480, pane.ui.width - 2 * pane.ui.gutter)
        padding: 20
        closePolicy: Popup.NoAutoClose
        title: action === "delete" ? "Delete chat?" : "Discard unsaved changes?"
        background: Rectangle { radius: 12; color: Theme.surface; border.color: Theme.rule }
        Overlay.modal: Rectangle { color: Theme.scrim }
        contentItem: Label {
            text: confirmDialog.action === "delete" ? "This removes the selected conversation from this device." : "Restore the last saved chat history and discard changes that could not be saved."
            color: Theme.ink
            wrapMode: Text.Wrap
        }
        footer: RowLayout {
            spacing: 10
            Item { Layout.fillWidth: true }
            AppButton { text: "Cancel"; onClicked: confirmDialog.close() }
            AppButton {
                text: confirmDialog.action === "delete" ? "Delete chat" : "Discard changes"
                onClicked: {
                    if (confirmDialog.action === "delete") cardChat.deleteThread(confirmDialog.targetThread)
                    else cardChat.discardUnsaved()
                    confirmDialog.close()
                }
            }
        }
    }
}
