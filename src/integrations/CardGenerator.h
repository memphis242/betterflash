#pragma once

#include <QPointer>
#include <QVariantMap>
#include <QObject>
#include <QString>

class AppController;
class QJsonObject;
class Summarizer;

// Generates a reviewable draft without mutating the collection.
class CardGenerator final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY changed)
    Q_PROPERTY(QVariantMap draft READ draft NOTIFY changed)
    Q_PROPERTY(QVariantMap sourceCard READ sourceCard NOTIFY changed)
    Q_PROPERTY(bool deckGeneration READ deckGeneration NOTIFY changed)
    Q_PROPERTY(bool verifying READ verifying NOTIFY changed)
    Q_PROPERTY(QVariantMap verification READ verification NOTIFY changed)
public:
    CardGenerator(AppController *app, Summarizer *summarizer, QObject *parent = nullptr);
    ~CardGenerator() override;

    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString errorDetail() const { return m_errorDetail; }
    [[nodiscard]] QVariantMap draft() const { return m_draft; }
    [[nodiscard]] QVariantMap sourceCard() const { return m_source; }
    [[nodiscard]] bool deckGeneration() const { return m_deckGeneration; }
    [[nodiscard]] bool verifying() const { return m_verifying; }
    [[nodiscard]] QVariantMap verification() const { return m_verification; }

    Q_INVOKABLE void generate(const QString &cardId, const QString &instructions = QString{},
                              const QString &difficulty = QStringLiteral("similar"));
    Q_INVOKABLE void generateForDeck(const QString &deckId, const QString &instructions);
    Q_INVOKABLE void refine(const QVariantMap &draft, const QString &instructions,
                            const QString &difficulty = QStringLiteral("similar"));
    Q_INVOKABLE void verify(const QVariantMap &draft);
    [[nodiscard]] Q_INVOKABLE bool verificationMatches(const QVariantMap &draft) const;
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void changed();
    void generated();

private:
    void fail(const QString &code, const QString &message, const QString &detail = {});
    void invalidate(const QString &code, const QString &message);
    void configurationChanged();
    [[nodiscard]] QVariantMap findCard(const QString &id) const;
    [[nodiscard]] QString deckName(const QString &deckId) const;
    [[nodiscard]] QByteArray fingerprint(const QVariantMap &card) const;
    [[nodiscard]] QString systemInstructions() const;
    void requestCompletion(const QString &instructions, bool refinement, const QString &difficulty);
    void receiveContent(const QString &response, quint64 generation);
    void requestVerification(const QString &system, const QJsonObject &data, quint64 generation, bool comparison);
    void receiveVerification(const QString &response, quint64 generation, bool comparison);
    [[nodiscard]] bool sourceStillCurrent() const;
    void clearVerification();

    QPointer<AppController> m_app{};
    QPointer<Summarizer> m_summarizer{};
    quint64 m_job = 0;
    quint64 m_generation = 0;
    bool m_busy = false;
    QString m_error{};
    QString m_errorDetail{};
    QVariantMap m_source{};
    QVariantMap m_draft{};
    QVariantMap m_requestDraft{};
    bool m_isRefinement = false;
    bool m_verifying = false;
    bool m_solverUncertain = false;
    QVariantMap m_verification{};
    QVariantMap m_verificationDraft{};
    QByteArray m_verifiedFingerprint{};
    QString m_maskedQuestion{};
    QString m_independentAnswer{};
    QString m_solverReason{};
    QByteArray m_sourceFingerprint{};
    QString m_generationDeckId{};
    QString m_generationDeckName{};
    QByteArray m_generationDeckFingerprint{};
    bool m_deckGeneration = false;
};
