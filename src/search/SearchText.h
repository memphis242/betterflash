#pragma once

#include <QCache>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <optional>

namespace betterflash::search {

struct SearchDocument {
    QString id;
    QString contentHash;
    QStringList fields;
    QStringList tokens;
    QString embeddingText;
    QVariantMap card;
};

struct SearchHit {
    QString id;
    int tier = 0;
    float score = 0.0F;
};

struct DocumentBatch {
    QList<SearchDocument> documents;
    int builtCount = 0;
};

class DocumentCache {
public:
#ifdef Q_OS_ANDROID
    static constexpr int defaultCostLimit = 64 * 1024 * 1024;
#else
    static constexpr int defaultCostLimit = 512 * 1024 * 1024;
#endif
    explicit DocumentCache(int costLimit = defaultCostLimit);
    std::optional<struct DocumentBatch> build(const QVariantList &cards, const QVariantList &decks,
                                               const QString &deckId,
                                               const std::function<bool()> &current = {},
                                               const QVariantList *collection = nullptr);
    void clear();
private:
    struct Entry {
        QString front;
        QString back;
        QString tags;
        QString path;
        struct SearchDocument document;
    };
    QCache<QString, struct Entry> m_entries;
    quint64 m_fullMisses = 0;
};

QList<SearchDocument> documents(const QVariantList &cards, const QVariantList &decks,
                                const QString &deckId);
QList<SearchHit> literalMatches(const QList<SearchDocument> &documents, const QString &query);
QList<SearchHit> fuzzyMatches(const QList<SearchDocument> &documents, const QString &query);
QString normalizedQuery(const QString &query);

} // namespace betterflash::search
