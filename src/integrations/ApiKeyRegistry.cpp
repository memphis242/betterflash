#include "ApiKeyRegistry.h"

#include "ApiKeyBinding.h"
#include "SecretStore.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QUrl>

#include <algorithm>
#include <map>
#include <utility>

namespace {
constexpr int kMaximumEntries = 64;
constexpr int kRegistryVersion = 1;

QString sourceName(const QString &source) {
    return source + QStringLiteral("-api-key");
}

QString sourcePageName(const QString &source) {
    if (source == QStringLiteral("speech-recognition")) return QStringLiteral("Speech recognition");
    if (source == QStringLiteral("speech-output")) return QStringLiteral("Speech output");
    if (source == QStringLiteral("language-model")) return QStringLiteral("Language model");
    return QStringLiteral("Search");
}

QString entryId(const QString &source, const QString &origin) {
    const QByteArray digest = QCryptographicHash::hash(origin.toUtf8(), QCryptographicHash::Sha256).toHex();
    return sourceName(source) + QLatin1Char('@') + QString::fromLatin1(digest);
}

QString serviceFor(const QString &source, const QString &origin, const bool legacy) {
    if (legacy) {
        if (source == QStringLiteral("speech-recognition")) return QStringLiteral("groq");
        if (source == QStringLiteral("speech-output")) return QStringLiteral("speech-output");
        if (source == QStringLiteral("language-model")) {
            const QString host = QUrl(origin).host().toLower();
            const QByteArray digest = QCryptographicHash::hash(host.toUtf8(), QCryptographicHash::Sha256).toHex();
            return QStringLiteral("llm-") + QString::fromLatin1(digest.left(16));
        }
        if (source == QStringLiteral("embedding")) {
            const QUrl url(origin);
            const int port = url.port(url.scheme() == QStringLiteral("https") ? 443 : 80);
            const QString legacyScope = url.scheme() + QStringLiteral("://") + url.host() + QLatin1Char(':') + QString::number(port);
            const QByteArray digest = QCryptographicHash::hash(legacyScope.toUtf8(), QCryptographicHash::Sha256).toHex();
            return QStringLiteral("embeddings-") + QString::fromLatin1(digest.left(24));
        }
    }
    const QByteArray digest = QCryptographicHash::hash((source + QLatin1Char('\n') + origin).toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("api-key-") + QString::fromLatin1(digest.left(32));
}

bool isLoopback(const QString &host) {
    return host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1");
}

bool parseOriginMap(const QString &serialized, QHash<QString, QString> *origins) {
    const QByteArray bytes = serialized.toUtf8();
    if (bytes.isEmpty() || bytes.size() > 4096 || bytes.contains('\n') || bytes.contains('\r')) return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return false;
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != 1 || !root.value(QStringLiteral("origins")).isObject()) return false;
    const QJsonObject object = root.value(QStringLiteral("origins")).toObject();
    if (object.size() > 4) return false;
    QHash<QString, QString> parsed;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString source = it.key();
        const QString origin = it.value().toString();
        if ((source != QStringLiteral("speech-recognition") && source != QStringLiteral("speech-output")
             && source != QStringLiteral("language-model") && source != QStringLiteral("embedding"))
            || origin.size() > 512 || ApiKeyRegistry::canonicalOrigin(origin) != origin) return false;
        parsed.insert(source, origin);
    }
    *origins = parsed;
    return true;
}
}

struct ApiKeyRegistry::Entry {
    QString id;
    QString source;
    QString origin;
    QString service;
    QString environmentVariable;
    bool legacy = false;
    SecretStore *store = nullptr;
};

struct ApiKeyRegistry::Private {
    struct ConfiguredEntry {
        QString source;
        QString origin;
    };
    std::map<QString, std::unique_ptr<Entry>> entries;
    QList<ConfiguredEntry> configuredEntries;
    QHash<QString, QString> selections;
    QSettings settings;
    SecretStore *originMap = nullptr;
    QHash<QString, QString> legacyOrigins;
    bool originMapResolved = false;
    bool originMapWritePending = false;
    QString migrationError;
};

ApiKeyRegistry::ApiKeyRegistry(QObject *parent)
    : QObject(parent), d(std::make_unique<Private>()) {
    d->originMap = new SecretStore(QStringLiteral("api-key-origin-map-v1"), {}, this);
    connect(d->originMap, &SecretStore::changed, this, [this] {
        if (d->originMapResolved) { emit changed(); return; }
        const QString raw = d->originMap->key();
        if (d->originMapWritePending) {
            if (d->originMap->storageState() == QStringLiteral("saved")) {
                d->originMapWritePending = false;
                QHash<QString, QString> parsed;
                if (!parseOriginMap(raw, &parsed)) {
                    d->migrationError = QStringLiteral("KEY_MIGRATION: The legacy API-key origin record could not be validated; new origin-scoped storage is being used.");
                } else {
                    d->legacyOrigins = parsed;
                }
                d->originMapResolved = true;
                migrateLegacy();
                emit changed();
            } else if (d->originMap->storageState() == QStringLiteral("session-only")) {
                d->originMapWritePending = false;
                d->migrationError = QStringLiteral("KEY_MIGRATION: Unlock the desktop keyring and restart to preserve legacy API-key access.");
                d->originMapResolved = true;
                migrateLegacy();
                emit changed();
            }
            return;
        }
        if (raw.isEmpty() && d->originMap->storageState() == QStringLiteral("missing")) {
            QHash<QString, QString> derived;
            const QString groq = QStringLiteral("https://api.groq.com:443");
            derived.insert(QStringLiteral("speech-recognition"), groq);
            const QString ttsEndpoint = d->settings.value(QStringLiteral("voice/remoteTts/endpoint"), QStringLiteral("https://api.groq.com/openai/v1/audio/speech")).toString();
            const QString ttsOrigin = canonicalOrigin(ttsEndpoint);
            if (!ttsOrigin.isEmpty()) derived.insert(QStringLiteral("speech-output"), ttsOrigin);
            const QString llmEndpoint = d->settings.value(QStringLiteral("llm/endpoint"), QStringLiteral("https://api.groq.com/openai/v1/chat/completions")).toString();
            const QString llmOrigin = canonicalOrigin(llmEndpoint);
            if (!llmOrigin.isEmpty()) derived.insert(QStringLiteral("language-model"), llmOrigin);
            const QString embeddingOrigin = canonicalOrigin(d->settings.value(QStringLiteral("search/endpoint")).toString());
            if (!embeddingOrigin.isEmpty()) derived.insert(QStringLiteral("embedding"), embeddingOrigin);
            QJsonObject origins;
            for (auto it = derived.cbegin(); it != derived.cend(); ++it) origins.insert(it.key(), it.value());
            const QJsonObject map{{QStringLiteral("version"), 1}, {QStringLiteral("origins"), origins}};
            const QByteArray serialized = QJsonDocument(map).toJson(QJsonDocument::Compact);
            if (serialized.size() <= 4096) {
                d->legacyOrigins = derived;
                d->originMapWritePending = true;
                d->originMap->setKey(QString::fromUtf8(serialized));
            } else {
                d->migrationError = QStringLiteral("KEY_MIGRATION: Legacy API-key metadata is too large to preserve safely.");
                d->originMapResolved = true;
                migrateLegacy();
                emit changed();
            }
            return;
        }
        if (!raw.isEmpty() && d->originMap->storageState() == QStringLiteral("saved")) {
            QHash<QString, QString> parsed;
            if (parseOriginMap(raw, &parsed)) {
                d->legacyOrigins = parsed;
                d->originMapResolved = true;
                migrateLegacy();
                emit changed();
                return;
            }
        }
        if (d->originMap->storageState() != QStringLiteral("loading")) {
            d->migrationError = raw.isEmpty()
                ? QStringLiteral("KEY_MIGRATION: Unlock the desktop keyring and restart to preserve legacy API-key access.")
                : QStringLiteral("KEY_MIGRATION: The legacy API-key origin record is invalid; new origin-scoped storage is being used.");
            d->originMapResolved = true;
            migrateLegacy();
            emit changed();
        }
    });
    QSettings &settings = d->settings;
    const int count = std::min(settings.beginReadArray(QStringLiteral("apiKeys/entries")), kMaximumEntries);
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        const QString source = settings.value(QStringLiteral("source")).toString().trimmed();
        const QString origin = settings.value(QStringLiteral("origin")).toString().trimmed();
        const QString id = settings.value(QStringLiteral("id")).toString().trimmed();
        const QString canonical = canonicalOrigin(origin);
        if (!validSource(source) || canonical.isEmpty() || canonical != origin || id != entryId(source, canonical)) continue;
        d->configuredEntries.append({source, canonical});
    }
    settings.endArray();

    const QStringList selectionKeys = settings.value(QStringLiteral("apiKeys/selections/keys")).toStringList();
    for (const QString &key : selectionKeys.mid(0, kMaximumEntries)) {
        if (key.size() > 800) continue;
        const QString source = key.section(QLatin1Char('|'), 0, 0);
        const QString origin = key.section(QLatin1Char('|'), 1, 1);
        const QString id = settings.value(QStringLiteral("apiKeys/selections/%1").arg(key)).toString();
        const QString canonical = canonicalOrigin(origin);
        if (validSource(source) && canonical == origin && !id.isEmpty() && id.size() <= 256)
            d->selections.insert(source + QLatin1Char('|') + canonical, id);
    }
}

ApiKeyRegistry::~ApiKeyRegistry() = default;

QString ApiKeyRegistry::error() const { return d->migrationError; }

QString ApiKeyRegistry::canonicalOrigin(const QString &endpoint) {
    const QString trimmed = endpoint.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 1024) return {};
    const QUrl url(trimmed);
    const QString scheme = url.scheme().toLower();
    const QString host = url.host().toLower();
    const bool loopback = isLoopback(host);
    if (!url.isValid() || host.isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()
        || !(scheme == QStringLiteral("https") || (scheme == QStringLiteral("http") && loopback))) return {};
    const int defaultPort = scheme == QStringLiteral("https") ? 443 : 80;
    const int port = url.port(defaultPort);
    if (port <= 0 || port > 65535) return {};
    const QString renderedHost = host.contains(QLatin1Char(':')) ? QLatin1Char('[') + host + QLatin1Char(']') : host;
    return scheme + QStringLiteral("://") + renderedHost + QLatin1Char(':') + QString::number(port);
}

bool ApiKeyRegistry::validSource(const QString &source) {
    return source == QStringLiteral("speech-recognition") || source == QStringLiteral("speech-output")
        || source == QStringLiteral("language-model") || source == QStringLiteral("embedding");
}

ApiKeyRegistry::Entry *ApiKeyRegistry::ensureEntry(const QString &source, const QString &origin, const bool legacy) {
    const QString canonical = canonicalOrigin(origin);
    if (!d->originMapResolved || !validSource(source) || canonical.isEmpty() || canonical.size() > 512) return nullptr;
    const QString id = entryId(source, canonical);
    if (const auto it = d->entries.find(id); it != d->entries.end()) return it->second.get();
    if (d->entries.size() >= kMaximumEntries) return nullptr;
    auto entry = std::make_unique<Entry>();
    const bool pinnedLegacy = legacy && d->legacyOrigins.value(source) == canonical && d->migrationError.isEmpty();
    entry->id = id; entry->source = source; entry->origin = canonical; entry->legacy = pinnedLegacy;
    entry->service = serviceFor(source, canonical, pinnedLegacy);
    entry->environmentVariable = pinnedLegacy && source == QStringLiteral("speech-recognition") ? QStringLiteral("GROQ_API_KEY")
        : pinnedLegacy && source == QStringLiteral("speech-output") ? QStringLiteral("GROQ_TTS_API_KEY")
        : pinnedLegacy && source == QStringLiteral("language-model")
            ? (QUrl(canonical).host() == QStringLiteral("api.groq.com") ? QStringLiteral("GROQ_API_KEY") : QStringLiteral("BETTERFLASH_LLM_API_KEY"))
            : QString();
    entry->store = new SecretStore(entry->service, entry->environmentVariable, this);
    Entry *const result = entry.get();
    d->entries.emplace(id, std::move(entry));
    connectEntry(result);
    return result;
}

QString ApiKeyRegistry::ensureOwnEntryId(const QString &source, const QString &origin, QString *error) {
    if (!d->originMapResolved) {
        if (error) *error = QStringLiteral("KEY_LOADING: API-key storage is still being initialized.");
        return {};
    }
    Entry *const item = ensureEntry(source, origin);
    if (!item) {
        if (error) *error = QStringLiteral("KEY_REGISTRY: The key registry has reached its entry limit or the endpoint is invalid.");
        return {};
    }
    if (!persist(error)) return {};
    return item->id;
}

void ApiKeyRegistry::connectEntry(Entry *const entry) {
    Q_ASSERT(entry && entry->store);
    connect(entry->store, &SecretStore::changed, this, [this] { emit changed(); });
}

ApiKeyBinding *ApiKeyRegistry::binding(const QString &source, QObject *parent) {
    if (!validSource(source)) return nullptr;
    return new ApiKeyBinding(this, source, parent);
}

ApiKeyRegistry::Entry *ApiKeyRegistry::entry(const QString &id) const {
    const auto it = d->entries.find(id);
    return it == d->entries.end() ? nullptr : it->second.get();
}

bool ApiKeyRegistry::entryMatches(const QString &id, const QString &origin) const {
    const Entry *const item = entry(id);
    return item && item->origin == origin;
}

QString ApiKeyRegistry::entrySource(const QString &id) const {
    const Entry *const item = entry(id);
    return item ? item->source : QString();
}

SecretStore *ApiKeyRegistry::entryStore(const QString &id) const {
    const Entry *const item = entry(id);
    return item ? item->store : nullptr;
}

bool ApiKeyRegistry::canSelectKey(const QString &id, const QString &endpoint) const {
    const QString origin = canonicalOrigin(endpoint);
    const Entry *const item = entry(id);
    return item && !origin.isEmpty() && item->origin == origin;
}

QString ApiKeyRegistry::selectionFor(const QString &source, const QString &origin) const {
    return d->selections.value(source + QLatin1Char('|') + origin);
}

bool ApiKeyRegistry::selectFor(const QString &source, const QString &origin, const QString &id, QString *error) {
    const QString key = source + QLatin1Char('|') + origin;
    const auto previous = d->selections.constFind(key);
    const bool hadPrevious = previous != d->selections.cend();
    const QString previousId = hadPrevious ? previous.value() : QString();
    if (id.isEmpty()) d->selections.remove(key);
    else d->selections.insert(key, id);
    if (!persist(error)) {
        if (hadPrevious) d->selections.insert(key, previousId);
        else d->selections.remove(key);
        return false;
    }
    emit changed();
    return true;
}

QVariantList ApiKeyRegistry::choicesFor(const QString &source, const QString &origin, const QString &selectedId) const {
    QVariantList choices;
    QList<Entry *> entries;
    for (const auto &value : d->entries)
        if (value.second->origin == origin && (value.second->source == source || value.second->store->hasKey() || value.second->store->storageState() == QStringLiteral("environment"))) entries.append(value.second.get());
    if (const Entry *const selected = entry(selectedId); selected && selected->origin == origin
        && !entries.contains(const_cast<Entry *>(selected))) entries.append(const_cast<Entry *>(selected));
    std::sort(entries.begin(), entries.end(), [source](const Entry *a, const Entry *b) {
        const bool aOwn = a->source == source;
        const bool bOwn = b->source == source;
        return aOwn != bOwn ? aOwn : a->id < b->id;
    });
    for (const Entry *const item : entries) {
        QVariantMap choice;
        choice.insert(QStringLiteral("id"), item->id);
        choice.insert(QStringLiteral("name"), sourceName(item->source));
        choice.insert(QStringLiteral("sourcePage"), sourcePageName(item->source));
        choice.insert(QStringLiteral("source"), item->source);
        choice.insert(QStringLiteral("origin"), item->origin);
        choice.insert(QStringLiteral("available"), item->store->hasKey());
        choice.insert(QStringLiteral("storageState"), item->store->storageState());
        choice.insert(QStringLiteral("storageStatus"), item->store->status());
        choice.insert(QStringLiteral("selected"), item->id == selectedId);
        const QString state = item->store->storageState();
        const QString availability = state == QStringLiteral("environment") ? QStringLiteral("Environment")
            : state == QStringLiteral("session-only") ? QStringLiteral("Session only")
            : state == QStringLiteral("saving") ? QStringLiteral("Saving")
            : state == QStringLiteral("removing") ? QStringLiteral("Removing")
            : item->store->hasKey() ? QStringLiteral("Saved") : QStringLiteral("Unavailable");
        choice.insert(QStringLiteral("label"), (item->source == source ? QStringLiteral("This page: ") : QString())
            + sourceName(item->source) + QStringLiteral(" (") + item->origin + QStringLiteral(") - ") + availability);
        choices.append(choice);
    }
    if (!selectedId.isEmpty() && (!entry(selectedId) || !entryMatches(selectedId, origin))) {
        QVariantMap missing;
        missing.insert(QStringLiteral("id"), selectedId);
        missing.insert(QStringLiteral("name"), QStringLiteral("Unavailable API key"));
        missing.insert(QStringLiteral("sourcePage"), QStringLiteral("Unavailable source"));
        missing.insert(QStringLiteral("origin"), origin);
        missing.insert(QStringLiteral("available"), false);
        missing.insert(QStringLiteral("missing"), true);
        missing.insert(QStringLiteral("selected"), true);
        missing.insert(QStringLiteral("label"), QStringLiteral("Unavailable API key (") + origin + QStringLiteral(")"));
        choices.append(missing);
    }
    return choices;
}

QVariantList ApiKeyRegistry::keyChoices() const {
    QVariantList choices;
    QList<Entry *> entries;
    for (const auto &value : d->entries) entries.append(value.second.get());
    std::sort(entries.begin(), entries.end(), [](const Entry *a, const Entry *b) { return a->id < b->id; });
    for (const Entry *const item : entries) {
        QVariantMap choice;
        choice.insert(QStringLiteral("id"), item->id);
        choice.insert(QStringLiteral("name"), sourceName(item->source));
        choice.insert(QStringLiteral("sourcePage"), sourcePageName(item->source));
        choice.insert(QStringLiteral("origin"), item->origin);
        choice.insert(QStringLiteral("available"), item->store->hasKey());
        choice.insert(QStringLiteral("storageState"), item->store->storageState());
        choice.insert(QStringLiteral("storageStatus"), item->store->status());
        choice.insert(QStringLiteral("label"), sourceName(item->source) + QStringLiteral(" (") + item->origin + QStringLiteral(")"));
        choices.append(choice);
    }
    return choices;
}

void ApiKeyRegistry::migrateLegacy() {
    if (!d->originMapResolved) return;
    for (const Private::ConfiguredEntry &configured : std::as_const(d->configuredEntries))
        ensureEntry(configured.source, configured.origin, true);
    for (auto it = d->legacyOrigins.cbegin(); it != d->legacyOrigins.cend(); ++it)
        ensureEntry(it.key(), it.value(), true);
    const QString llmOrigin = d->legacyOrigins.value(QStringLiteral("language-model"));
    const QString embeddingOrigin = d->legacyOrigins.value(QStringLiteral("embedding"));
    const bool reuse = d->settings.value(QStringLiteral("search/reuseLlmKey"), false).toBool();
    const QString llmId = entryId(QStringLiteral("language-model"), llmOrigin);
    const bool migrateSelection = d->settings.value(QStringLiteral("apiKeys/migrationVersion"), 0).toInt() < kRegistryVersion;
    if (migrateSelection && reuse && !llmOrigin.isEmpty() && embeddingOrigin == llmOrigin && entry(llmId)
        && !d->selections.contains(QStringLiteral("embedding|") + embeddingOrigin))
        d->selections.insert(QStringLiteral("embedding|") + embeddingOrigin, llmId);
    d->settings.setValue(QStringLiteral("apiKeys/migrationVersion"), kRegistryVersion);
    persist();
}

bool ApiKeyRegistry::persist(QString *error) {
    QSettings &settings = d->settings;
    settings.remove(QStringLiteral("apiKeys/entries"));
    settings.beginWriteArray(QStringLiteral("apiKeys/entries"));
    QList<Entry *> entries;
    for (const auto &value : d->entries) entries.append(value.second.get());
    std::sort(entries.begin(), entries.end(), [](const Entry *a, const Entry *b) { return a->id < b->id; });
    int index = 0;
    for (const Entry *const entry : entries) {
        if (index >= kMaximumEntries) break;
        settings.setArrayIndex(index++);
        settings.setValue(QStringLiteral("id"), entry->id);
        settings.setValue(QStringLiteral("source"), entry->source);
        settings.setValue(QStringLiteral("origin"), entry->origin);
    }
    settings.endArray();
    QStringList selectionKeys;
    for (auto it = d->selections.cbegin(); it != d->selections.cend(); ++it) {
        const QString source = it.key().section(QLatin1Char('|'), 0, 0);
        const QString origin = it.key().section(QLatin1Char('|'), 1, 1);
        if (source.isEmpty() || !validSource(source) || canonicalOrigin(origin) != origin || it.value().size() > 256) continue;
        if (selectionKeys.size() >= kMaximumEntries) break;
        selectionKeys.append(it.key());
        settings.setValue(QStringLiteral("apiKeys/selections/%1").arg(it.key()), it.value());
    }
    settings.setValue(QStringLiteral("apiKeys/selections/keys"), selectionKeys);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) *error = QStringLiteral("KEY_SETTINGS: Unable to save API-key metadata.");
        return false;
    }
    return true;
}
