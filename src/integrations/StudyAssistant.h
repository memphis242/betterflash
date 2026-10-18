#pragma once

#include <QPointer>
#include <QVariantMap>
#include <QObject>
#include <QString>
#include <QJsonObject>

class AppController;
class Summarizer;

// Provides explicitly requested, card-scoped study guidance. The class keeps
// the evidence used for a request so a response cannot silently outlive it.
class StudyAssistant final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString result READ result NOTIFY changed)
    Q_PROPERTY(QVariantMap error READ error NOTIFY changed)
    Q_PROPERTY(QString cardId READ cardId NOTIFY changed)
    Q_PROPERTY(QString variantId READ variantId NOTIFY changed)
    Q_PROPERTY(bool stale READ stale NOTIFY changed)
    Q_PROPERTY(QString prompt READ prompt CONSTANT)
    Q_PROPERTY(QString disclosure READ disclosure CONSTANT)
public:
    StudyAssistant(Summarizer *summarizer, AppController *app, QObject *parent = nullptr);
    ~StudyAssistant() override;

    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString result() const { return m_result; }
    [[nodiscard]] QVariantMap error() const { return m_error; }
    [[nodiscard]] QString cardId() const { return m_cardId; }
    [[nodiscard]] QString variantId() const { return m_variantId; }
    [[nodiscard]] bool stale() const { return m_stale; }
    [[nodiscard]] QString prompt() const { return m_prompt; }
    [[nodiscard]] QString disclosure() const { return m_disclosure; }

    Q_INVOKABLE void requestHelp(const QString &cardId, const QString &variantId = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    void fail(const QString &code, const QString &message, const QString &detail = {});
    void invalidate(const QString &code, const QString &message);
    void configurationChanged();
    [[nodiscard]] QVariantMap findCard(const QString &id) const;
    [[nodiscard]] QJsonObject evidenceFor(const QVariantMap &card, const QString &requestedVariant,
                                          QString *resolvedVariant) const;
    [[nodiscard]] QByteArray fingerprint(const QVariantMap &card, const QString &variantId) const;
    [[nodiscard]] QString systemInstructions() const;

    QPointer<Summarizer> m_summarizer{};
    QPointer<AppController> m_app{};
    quint64 m_job = 0;
    quint64 m_generation = 0;
    bool m_busy = false;
    bool m_stale = false;
    QString m_result{};
    QVariantMap m_error{};
    QString m_cardId{};
    QString m_variantId{};
    QByteArray m_evidenceFingerprint{};
    QString m_prompt = QStringLiteral("Study guidance is generated only when you request help for a selected card.");
    QString m_disclosure = QStringLiteral(
        "Optional language-model assistance. It uses the selected card and its weakness evidence, does not change cards or schedules, and may send card text to the configured provider.");
};
