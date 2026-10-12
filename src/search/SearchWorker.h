#pragma once

#include "LocalEmbedder.h"
#include "SearchText.h"
#include <QCache>
#include <QObject>
#include <QSqlDatabase>
#include <QSet>
#include <array>
#include <atomic>
#include <memory>

class SearchWorker final : public QObject {
    Q_OBJECT
public:
    static constexpr int channelCount = 2;
    using Epochs = std::array<std::shared_ptr<std::atomic<quint64>>, channelCount>;
    explicit SearchWorker(QString cachePath, Epochs epochs,
                          std::shared_ptr<std::atomic<quint64>> testEpoch);
    ~SearchWorker() override;
    void search(int channel, quint64 epoch, const QVariantList &cards, const QVariantList &decks,
                const QString &deck, const QString &query);
    void semantic(int channel, quint64 epoch, const QString &profile, const QString &directory,
                  const QString &scope, bool remote);
    void acceptRemote(int channel, quint64 epoch, const QVariantList &vectors);
    void clearCache();
    void testLocal(quint64 epoch, const QString &profile, const QString &directory);
signals:
    void literalReady(int channel, quint64 epoch, const QVariantList &hits);
    void fuzzyReady(int channel, quint64 epoch, const QVariantList &hits);
    void semanticReady(int channel, quint64 epoch, const QVariantList &hits);
    void progress(int channel, quint64 epoch, int completed, int total);
    void remoteNeeded(int channel, quint64 epoch, const QStringList &texts);
    void failed(int channel, quint64 epoch, const QString &error);
    void testFinished(quint64 epoch, bool success, const QString &message);
private:
    struct Job { QString key; QString text; bool query = false; };
    struct QueryState {
        quint64 epoch = 0;
        QString query;
        QString scope;
        QString queryKey;
        QString profile;
        QString directory;
        QList<struct betterflash::search::SearchDocument> documents;
        QSet<QString> textMatchIds;
        QList<struct Job> jobs;
        QList<struct Job> inFlight;
        QHash<QString, QStringList> documentKeys;
        QHash<QString, QVector<float>> vectors;
        int completed = 0;
        int total = 0;
    };
    struct QueryState &state(int channel);
    bool current(int channel, quint64 epoch) const;
    bool openCache();
    QVector<float> cached(const QString &scope, const QString &key, QString &error);
    bool save(const QString &scope, const QString &key, const QVector<float> &vector);
    bool pruneCache();
    void localStep(int channel, quint64 epoch);
    void nextRemote(int channel, quint64 epoch);
    void finish(int channel, quint64 epoch);
    const QString m_cachePath;
    const Epochs m_currentEpochs;
    const std::shared_ptr<std::atomic<quint64>> m_testEpoch;
    std::array<struct QueryState, channelCount> m_states;
    QString m_loadedProfile;
    QString m_loadedDirectory;
    QSqlDatabase m_cache;
    QCache<QString, QVector<float>> m_memoryCache{32 * 1024 * 1024};
    std::unique_ptr<LocalEmbedder> m_embedder;
    int m_cacheWrites = 0;
};
