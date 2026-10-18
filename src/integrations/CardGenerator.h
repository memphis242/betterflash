#pragma once

#include <QPointer>
#include <QVariantMap>
#include <QObject>
#include <QString>

class AppController;
class Summarizer;

// Generates a reviewable draft without mutating the collection.
class CardGenerator final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantMap draft READ draft NOTIFY changed)
    Q_PROPERTY(QVariantMap sourceCard READ sourceCard NOTIFY changed)
public:
    CardGenerator(AppController *app, Summarizer *summarizer, QObject *parent = nullptr);
    ~CardGenerator() override;

    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QVariantMap draft() const { return m_draft; }
    [[nodiscard]] QVariantMap sourceCard() const { return m_source; }

    Q_INVOKABLE void generate(const QString &cardId);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void changed();
    void generated();

private:
    void fail(const QString &code, const QString &message);
    void invalidate(const QString &code, const QString &message);
    void configurationChanged();
    [[nodiscard]] QVariantMap findCard(const QString &id) const;
    [[nodiscard]] QString deckName(const QString &deckId) const;
    [[nodiscard]] QByteArray fingerprint(const QVariantMap &card) const;
    [[nodiscard]] QString systemInstructions() const;
    void receiveContent(const QString &response, quint64 generation);

    QPointer<AppController> m_app{};
    QPointer<Summarizer> m_summarizer{};
    quint64 m_job = 0;
    quint64 m_generation = 0;
    bool m_busy = false;
    QString m_error{};
    QVariantMap m_source{};
    QVariantMap m_draft{};
    QByteArray m_sourceFingerprint{};
};
