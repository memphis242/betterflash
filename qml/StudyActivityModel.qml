import QtQuick

Item {
    id: model

    property string deckId: ""
    property bool active: true
    property var analytics: ({})
    property bool loading: false
    property var error: ({})

    readonly property string instanceToken: "study-activity-" + Date.now() + "-" + Math.random().toString(36).slice(2)
    property int requestSerial: 0
    property string pendingRequest: ""
    property string observedDay: localDay()
    property bool ready: false

    implicitWidth: 0
    implicitHeight: 0
    visible: false

    function localDay() {
        return Qt.formatDate(new Date(), "yyyy-MM-dd");
    }

    function queueReload() {
        if (ready && active)
            refreshTimer.restart();
    }

    function reload() {
        if (!ready || !active)
            return;
        refreshTimer.stop();
        pendingRequest = instanceToken + "-" + (++requestSerial);
        loading = true;
        error = ({});
        observedDay = localDay();
        if (app.loadStudyAnalytics(deckId, pendingRequest) === false) {
            pendingRequest = "";
            loading = false;
            error = {
                code: "STUDY_ANALYTICS",
                message: "Review activity could not be loaded. Try again when the collection is available.",
                detail: ""
            };
        }
    }

    onDeckIdChanged: {
        pendingRequest = "";
        analytics = ({});
        error = ({});
        loading = false;
        queueReload();
    }

    onActiveChanged: {
        if (active) {
            queueReload();
        } else {
            refreshTimer.stop();
            pendingRequest = "";
            loading = false;
        }
    }

    Component.onCompleted: {
        ready = true;
        queueReload();
    }

    Timer {
        id: refreshTimer
        interval: 150
        onTriggered: model.reload()
    }

    Timer {
        interval: 60000
        repeat: true
        running: model.active && model.ready
        onTriggered: {
            if (model.observedDay !== model.localDay())
                model.queueReload();
        }
    }

    Connections {
        target: app

        function onHistoryChanged() {
            model.queueReload();
        }
        function onCardsChanged() {
            model.queueReload();
        }
        function onDecksChanged() {
            model.queueReload();
        }
        function onBusyChanged() {
            if (!app.busy)
                model.queueReload();
        }

        function onStudyAnalyticsReady(deckId, result) {
            if (!model.active || !result || !model.pendingRequest || deckId !== model.deckId || result.requestId !== model.pendingRequest)
                return;
            model.pendingRequest = "";
            model.loading = false;
            model.error = result.error || ({});
            if (!model.error.code)
                model.analytics = result;
        }
    }
}
