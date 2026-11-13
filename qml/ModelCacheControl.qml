import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: control
    required property var operation
    property bool policyEnabled: true
    readonly property var provider: typeof ai === "undefined" ? null : ai
    readonly property bool available: !!provider && !!operation
        && typeof provider.operationFresh === "function"
        && typeof provider.operationCacheInfo === "function"
    property bool fresh: false
    property var cacheInfo: ({})
    readonly property var failure: provider && provider.cacheErrorInfo ? provider.cacheErrorInfo : ({})
    readonly property bool operationBusy: !!operation && !!operation.busy
    spacing: 4

    function refresh() {
        fresh = available ? provider.operationFresh(operation) : false
        cacheInfo = available ? provider.operationCacheInfo(operation) : ({})
    }
    function requestFresh() {
        if (!available || operationBusy || !policyEnabled) return
        provider.setOperationFresh(operation, true)
        refresh()
    }
    Component.onCompleted: refresh()
    onOperationChanged: refresh()
    Connections {
        target: control.provider
        function onCachePolicyChanged() { control.refresh() }
        function onCacheChanged() { control.refresh() }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        AppSwitch {
            objectName: "modelFreshResponse"
            text: "Fresh response"
            checked: control.fresh
            enabled: control.available && control.policyEnabled && !control.operationBusy
            onToggled: control.provider.setOperationFresh(control.operation, checked)
            ToolTip.visible: hovered || activeFocus
            ToolTip.text: "Bypass saved model replies for every turn of the next operation. New requests may use provider quota. Turn off to prefer matching cached model replies. Retrieval tools still run when needed; web searches can use Tavily credits."
            Accessible.description: ToolTip.text
        }
        Label {
            visible: !control.operationBusy && Number(control.cacheInfo.hits || 0) > 0
            text: "Used cached model responses"
            textFormat: Text.PlainText
            color: Theme.inkMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            ToolTip.visible: statusHover.hovered
            ToolTip.text: Number(control.cacheInfo.hits || 0) + " of " + Number(control.cacheInfo.requests || 0)
                + " model request attempts used cached replies matching the full input and configuration. Cached model replies may still require live retrieval; web searches can use Tavily credits."
            HoverHandler { id: statusHover }
        }
    }
    Rectangle {
        visible: control.available && Number(control.cacheInfo.requests || 0) > 0
            && !!control.failure.message
        Layout.fillWidth: true
        implicitHeight: Math.min(cacheWarningBody.implicitHeight, 180) + 20
        color: Theme.errorSurface
        border.color: Theme.warning
        radius: 6
        ScrollView {
            id: cacheWarningViewport
            anchors.fill: parent
            anchors.margins: 10
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                id: cacheWarningBody
                width: cacheWarningViewport.availableWidth
                spacing: 6
                ErrorBlock {
                    Layout.fillWidth: true
                    message: (control.failure.message || "")
                        + (control.failure.action ? " " + control.failure.action : "")
                    code: control.failure.code || ""
                    details: control.failure.detail || ""
                    textSize: 12
                }
                AppButton {
                    text: "Dismiss cache warning"
                    hint: "Dismiss this cache notice. A usable model response is kept."
                    onClicked: control.provider.acknowledgeCacheError()
                }
            }
        }
    }

}
