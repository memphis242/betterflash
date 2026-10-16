#include "ApiKeyBinding.h"

#include "ApiKeyRegistry.h"
#include "SecretStore.h"

#include <utility>

ApiKeyBinding::ApiKeyBinding(ApiKeyRegistry *const registry, QString source, QObject *parent)
    : QObject(parent), m_registry(registry), m_source(std::move(source)) {
    Q_ASSERT(m_registry);
    connect(m_registry, &ApiKeyRegistry::changed, this, &ApiKeyBinding::refresh);
    refresh();
}

ApiKeyBinding::~ApiKeyBinding() = default;

QString ApiKeyBinding::ownKeyName() const { return m_source + QStringLiteral("-api-key"); }
QString ApiKeyBinding::ownKeyId() const { return m_ownId; }
QString ApiKeyBinding::selectedKeyId() const { return m_selectedId; }
QString ApiKeyBinding::sourcePage() const {
    const QString selectedSource = m_registry->entryMatches(m_selectedId, m_origin) ? m_registry->entrySource(m_selectedId) : QString();
    const QString source = selectedSource.isEmpty() ? (m_selectedId.isEmpty() ? m_source : QString()) : selectedSource;
    if (source == QStringLiteral("speech-recognition")) return QStringLiteral("Speech recognition");
    if (source == QStringLiteral("speech-output")) return QStringLiteral("Speech output");
    if (source == QStringLiteral("language-model")) return QStringLiteral("Language model");
    return source == QStringLiteral("embedding") ? QStringLiteral("Search") : QStringLiteral("Unavailable source");
}
QString ApiKeyBinding::keyName() const {
    const QString source = m_registry->entryMatches(m_selectedId, m_origin) ? m_registry->entrySource(m_selectedId) : QString();
    return source.isEmpty() ? (m_selectedId.isEmpty() ? ownKeyName() : QStringLiteral("Unavailable API key"))
                            : source + QStringLiteral("-api-key");
}
bool ApiKeyBinding::reusingKey() const { return !m_selectedId.isEmpty() && m_selectedId != m_ownId; }
QVariantList ApiKeyBinding::keyChoices() const { return m_registry->choicesFor(m_source, m_origin, m_selectedId); }
bool ApiKeyBinding::hasKey() const {
    SecretStore *const selected = m_registry->entryStore(m_selectedId);
    return m_registry->entryMatches(m_selectedId, m_origin) && selected && selected->hasKey();
}
QString ApiKeyBinding::storageState() const {
    SecretStore *const selected = m_registry->entryStore(m_selectedId);
    return m_registry->entryMatches(m_selectedId, m_origin) && selected ? selected->storageState()
        : (m_selectedId.isEmpty() ? QStringLiteral("missing") : QStringLiteral("unavailable"));
}
QString ApiKeyBinding::storageStatus() const {
    SecretStore *const selected = m_registry->entryStore(m_selectedId);
    return m_registry->entryMatches(m_selectedId, m_origin) && selected ? selected->status()
        : (m_selectedId.isEmpty() ? QStringLiteral("Enter a key to enable this service.")
                                   : QStringLiteral("KEY_SELECTION: The selected API key is unavailable."));
}
QString ApiKeyBinding::key() const {
    SecretStore *const selected = m_registry->entryStore(m_selectedId);
    return m_registry->entryMatches(m_selectedId, m_origin) && selected ? selected->key() : QString();
}

QString ApiKeyBinding::error() const {
    if (!m_error.isEmpty()) return m_error;
    if (!m_selectedId.isEmpty() && !m_registry->entryMatches(m_selectedId, m_origin))
        return QStringLiteral("KEY_SELECTION: The selected API key is unavailable for this provider.");
    return m_registry->error();
}

void ApiKeyBinding::setEndpoint(const QString &endpoint) {
    const QString origin = ApiKeyRegistry::canonicalOrigin(endpoint);
    if (origin == m_origin && !m_ownId.isEmpty()) return;
    m_origin = origin;
    QString ownError;
    m_ownId = m_registry->ensureOwnEntryId(m_source, m_origin, &ownError);
    m_selectedId = m_ownId;
    const QString stored = m_registry->selectionFor(m_source, m_origin);
    if (!stored.isEmpty()) m_selectedId = stored;
    m_error = m_origin.isEmpty() ? QStringLiteral("KEY_ENDPOINT: Enter a valid HTTPS endpoint, or HTTP on localhost.") : ownError;
    emit changed();
}

bool ApiKeyBinding::selectKey(const QString &id) {
    if (m_origin.isEmpty()) { m_error = QStringLiteral("KEY_ENDPOINT: Enter a valid remote endpoint first."); emit changed(); return false; }
    if (!m_registry->canSelectKey(id, m_origin)) { m_error = QStringLiteral("KEY_SELECTION: Choose a key saved for this provider."); emit changed(); return false; }
    QString metadataError;
    if (!m_registry->selectFor(m_source, m_origin, id, &metadataError)) {
        m_error = metadataError;
        emit changed();
        return false;
    }
    m_selectedId = id;
    m_error.clear();
    emit changed();
    return true;
}

bool ApiKeyBinding::saveKey(const QString &key) {
    if (m_ownId.isEmpty()) { m_error = QStringLiteral("KEY_ENDPOINT: Enter a valid remote endpoint first."); emit changed(); return false; }
    const QString cleaned = key.trimmed();
    if (cleaned.isEmpty() || cleaned.size() > 4096 || cleaned.contains(QLatin1Char('\n')) || cleaned.contains(QLatin1Char('\r'))) {
        m_error = QStringLiteral("KEY_INVALID: Enter a single API key without line breaks."); emit changed(); return false;
    }
    SecretStore *const own = m_registry->entryStore(m_ownId);
    Q_ASSERT(own);
    if (!own) { m_error = QStringLiteral("KEY_REGISTRY: The key entry is unavailable."); emit changed(); return false; }
    QString metadataError;
    if (!m_registry->selectFor(m_source, m_origin, m_ownId, &metadataError)) {
        m_error = metadataError;
        emit changed();
        return false;
    }
    own->setKey(cleaned);
    m_selectedId = m_ownId;
    m_error.clear();
    emit changed();
    return true;
}

bool ApiKeyBinding::clearKey() {
    if (m_origin.isEmpty()) {
        m_error = QStringLiteral("KEY_ENDPOINT: Enter a valid remote endpoint first."); emit changed(); return false;
    }
    if (m_selectedId.isEmpty() || m_selectedId != m_ownId) {
        m_error = QStringLiteral("KEY_SHARED: Select this page's own key before clearing it."); emit changed(); return false;
    }
    SecretStore *const own = m_registry->entryStore(m_ownId);
    Q_ASSERT(own);
    if (!own) { m_error = QStringLiteral("KEY_REGISTRY: The key entry is unavailable."); emit changed(); return false; }
    own->clear();
    m_error.clear();
    emit changed();
    return true;
}

void ApiKeyBinding::refresh() {
    if (m_origin.isEmpty()) return;
    if (m_ownId.isEmpty()) {
        QString loadingError;
        const QString ownId = m_registry->ensureOwnEntryId(m_source, m_origin, &loadingError);
        if (ownId.isEmpty()) {
            m_error = loadingError;
            emit changed();
            return;
        }
        m_ownId = ownId;
        m_selectedId = ownId;
        m_error.clear();
    }
    const QString stored = m_registry->selectionFor(m_source, m_origin);
    if (!stored.isEmpty()) m_selectedId = stored;
    emit changed();
}
