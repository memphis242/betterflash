import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: notice
    required property var service
    property bool acknowledgeOnDismiss: true
    signal dismissRequested()
    readonly property var failure: service && service.cacheErrorInfo ? service.cacheErrorInfo : ({})
    spacing: 10
    Label {
        text: "Matching model replies are reused from this device's cache. Fresh response bypasses the cache for every model turn in an operation. Cached replies may still require live retrieval, and web searches can use Tavily credits. Model tests always contact the configured backend. Cached replies may contain card content, stay on this device, and expire after 30 days. Storage is bounded to 128 MiB and 512 entries."
        textFormat: Text.PlainText
        color: Theme.inkMuted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        font.pixelSize: 14
    }
    Rectangle {
        visible: !!notice.failure.message
        Layout.fillWidth: true
        implicitHeight: Math.min(cacheError.implicitHeight, 220) + 24
        color: Theme.errorSurface
        border.color: Theme.warning
        radius: 6
        ScrollView {
            id: errorViewport
            anchors.fill: parent
            anchors.margins: 12
            contentWidth: availableWidth
            clip: true
            ErrorBlock {
                id: cacheError
                width: errorViewport.availableWidth
                message: (notice.failure.message || "")
                    + (notice.failure.action ? " " + notice.failure.action : "")
                code: notice.failure.code || ""
                details: notice.failure.detail || ""
                textSize: 15
            }
        }
    }
    AppButton {
        visible: !!notice.failure.message
        text: "Dismiss cache warning"
        hint: "Dismiss this notice. Fresh model responses remain available if the cache cannot be used."
        onClicked: {
            if (notice.acknowledgeOnDismiss) notice.service.acknowledgeCacheError()
            else notice.dismissRequested()
        }
    }
}
