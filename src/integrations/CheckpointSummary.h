#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class Summarizer;

class CheckpointSummary final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    explicit CheckpointSummary(Summarizer *summarizer, QObject *parent = nullptr);
    ~CheckpointSummary() override;

    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString summary() const { return m_summary; }
    [[nodiscard]] QString status() const { return m_status; }

    Q_INVOKABLE void generate(const QVariantMap &preview, const QString &deckName);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    void fail(const QString &code, const QString &message);
    void configurationChanged();
    [[nodiscard]] bool prepareBatches(const QJsonArray &items, bool merging);
    void startNextBatch(quint64 generation);
    [[nodiscard]] QString prompt(const QJsonArray &items, bool merging) const;

    QPointer<Summarizer> m_summarizer{};
    quint64 m_job = 0;
    quint64 m_generation = 0;
    bool m_busy = false;
    QString m_error{};
    QString m_summary{};
    QString m_status{};
    QStringList m_batches{};
    QStringList m_results{};
    QJsonObject m_scope{};
    QString m_instructions{};
    qsizetype m_inputLimit = 0;
    qint64 m_submittedBytes = 0;
    int m_nextBatch = 0;
    int m_calls = 0;
    int m_round = 0;
};
