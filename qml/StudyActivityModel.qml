import QtQuick

Item {
    id: model

    property string deckId: ""
    property var deckIds: deckId ? [deckId] : []
    property bool active: true
    property var analytics: ({})
    property bool loading: false
    property var error: ({})

    readonly property string instanceToken: "study-activity-" + Date.now() + "-" + Math.random().toString(36).slice(2)
    property int requestSerial: 0
    property string pendingRequest: ""
    property string pendingScope: "[]"
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

    function scopeIds() {
        const values = sequenceValues(deckIds)
        return values.map(id => String(id)).filter((id, index, ids) => id && ids.indexOf(id) === index).sort()
    }

    function sequenceValues(value) {
        if (value === undefined || value === null || typeof value === "string") return []
        if (typeof value.length !== "number") return []
        const values = []
        for (let index = 0; index < value.length; ++index) values.push(value[index])
        return values
    }

    function scopeKey(ids) {
        return JSON.stringify(ids.map(id => String(id)).filter((id, index, values) => id && values.indexOf(id) === index).sort())
    }

    function reload() {
        if (!ready || !active)
            return;
        refreshTimer.stop();
        pendingRequest = instanceToken + "-" + (++requestSerial);
        const requestedScope = scopeIds();
        pendingScope = scopeKey(requestedScope);
        loading = true;
        error = ({});
        observedDay = localDay();
        if (app.loadStudyAnalytics(requestedScope, pendingRequest) === false) {
            pendingRequest = "";
            loading = false;
            error = {
                code: "STUDY_ANALYTICS",
                message: "Review activity could not be loaded. Try again when the collection is available.",
                detail: ""
            };
        }
    }

    function scopeChanged() {
        pendingRequest = "";
        pendingScope = "[]";
        analytics = ({});
        error = ({});
        loading = false;
        queueReload();
    }

    onDeckIdChanged: scopeChanged()
    onDeckIdsChanged: scopeChanged()

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

        function onStudyAnalyticsReady(deckIds, result) {
            if (!model.active || !result || !model.pendingRequest || result.requestId !== model.pendingRequest)
                return;
            if (model.scopeKey(model.sequenceValues(deckIds)) !== model.pendingScope)
                return;
            model.pendingRequest = "";
            model.loading = false;
            model.error = result.error || ({});
            if (!model.error.code)
                model.analytics = result;
        }
    }
}
