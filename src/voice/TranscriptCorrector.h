#pragma once

#include "integrations/Summarizer.h"

#include <QHash>
#include <QPointer>
#include <QSettings>
#include <QStringList>
#include <functional>

class TranscriptCorrector final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY configurationChanged)
    Q_PROPERTY(QString vocabulary READ vocabulary WRITE setVocabulary NOTIFY configurationChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString configuration READ configuration NOTIFY configurationChanged)

public:
    explicit TranscriptCorrector(QObject *parent = nullptr);
    explicit TranscriptCorrector(Summarizer *summarizer, QObject *parent = nullptr);
    ~TranscriptCorrector() override;

    bool enabled() const { return m_enabled; }
    QString vocabulary() const { return m_vocabulary; }
    bool ready() const;
    bool busy() const { return !m_jobs.isEmpty(); }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString configuration() const;

    void setEnabled(bool enabled);
    void setVocabulary(const QString &vocabulary);
    void attachSummarizer(Summarizer *summarizer);

    using Completion = std::function<void(std::expected<QString, struct ProviderError>)>;
    [[nodiscard]] quint64 correct(const QString &raw, QObject *context, Completion callback);
    void cancel(quint64 id);
    void cancelAll();

signals:
    void configurationChanged();
    void readyChanged();
    void changed();

private:
    struct Job {
        quint64 id = 0;
        quint64 summarizerId = 0;
        QString raw;
        QPointer<QObject> context;
        Completion callback;
        QMetaObject::Connection contextConnection;
    };

    void finish(quint64 id, std::expected<QString, struct ProviderError> result);
    void failImmediate(quint64 id, const ProviderError &error);
    void clearErrorIfReady();
    [[nodiscard]] QJsonObject buildPayload(const QString &raw) const;
    [[nodiscard]] std::expected<QString, struct ProviderError> validateOutput(const QString &raw,
        const QString &modelOutput) const;
    [[nodiscard]] static bool preservesNumbers(const QString &raw, const QString &corrected);
    [[nodiscard]] static QStringList numericTokens(const QString &text);

    QPointer<Summarizer> m_summarizer;
    QSettings m_settings;
    QHash<quint64, Job> m_jobs;
    QList<QMetaObject::Connection> m_summarizerConnections;
    quint64 m_sequence = 0;
    bool m_enabled = false;
    QString m_vocabulary;
    QString m_status;
    QString m_error;
    QString m_credentialVersion;
};
