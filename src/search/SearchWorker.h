#pragma once

#include "LocalEmbedder.h"
#include "SearchText.h"
#include <QObject>
#include <QSqlDatabase>
#include <atomic>
#include <memory>

class SearchWorker final : public QObject {
    Q_OBJECT
public:
    explicit SearchWorker(QString cachePath, std::shared_ptr<std::atomic<quint64>> epoch);
    ~SearchWorker() override;
    void search(quint64 epoch, const QVariantList &cards, const QVariantList &decks,
                const QString &deck, const QString &query);
    void semantic(quint64 epoch, const QString &profile, const QString &directory,
                  const QString &scope, bool remote);
    void acceptRemote(quint64 epoch, const QVariantList &vectors);
    void clearCache(quint64 epoch);
signals:
    void literalReady(quint64 epoch, const QVariantList &hits);
    void fuzzyReady(quint64 epoch, const QVariantList &hits);
    void semanticReady(quint64 epoch, const QVariantList &hits);
    void progress(quint64 epoch, int completed, int total);
    void remoteNeeded(quint64 epoch, const QStringList &texts);
    void failed(quint64 epoch, const QString &error);
private:
    struct Job { QString key; QString text; bool query = false; };
    bool current(quint64 epoch) const;
    bool openCache();
    QVector<float> cached(const QString &key);
    bool save(const QString &key, const QVector<float> &vector);
    bool pruneCache();
    void nextRemote();
    void finish();
    const QString m_cachePath;
    const std::shared_ptr<std::atomic<quint64>> m_currentEpoch;
    quint64 m_epoch = 0;
    QString m_query;
    QString m_scope;
    QString m_queryKey;
    QString m_loadedProfile;
    QString m_loadedDirectory;
    QSqlDatabase m_cache;
    QList<struct betterflash::search::SearchDocument> m_documents;
    QList<struct Job> m_jobs;
    QList<struct Job> m_inFlight;
    QHash<QString, QStringList> m_documentKeys;
    QHash<QString, QVector<float>> m_vectors;
    std::unique_ptr<LocalEmbedder> m_embedder;
    int m_completed = 0;
    int m_total = 0;
    QString m_cacheError;
    int m_cacheWrites = 0;
};
