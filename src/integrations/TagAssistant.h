#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class AppController;
class Summarizer;

class TagAssistant final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap errorInfo READ errorInfo NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(QStringList suggestedVocabulary READ suggestedVocabulary NOTIFY changed)
    Q_PROPERTY(bool vocabularyStale READ vocabularyStale NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(int completed READ completed NOTIFY changed)
    Q_PROPERTY(QString scopeName READ scopeName NOTIFY changed)
public:
    TagAssistant(AppController *app, Summarizer *provider, QObject *parent = nullptr);
    ~TagAssistant() override;
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QVariantMap errorInfo() const { return m_errorInfo; }
    [[nodiscard]] QVariantList results() const { return m_results; }
    [[nodiscard]] QStringList suggestedVocabulary() const { return m_suggestedVocabulary; }
    [[nodiscard]] bool vocabularyStale() const { return m_vocabularyStale; }
    [[nodiscard]] int total() const { return int(m_cards.size()); }
    [[nodiscard]] int completed() const { return m_completed; }
    [[nodiscard]] QString scopeName() const { return m_scopeName; }
    Q_INVOKABLE void prepare(const QString &deckId, const QString &cardId, bool includeSubdecks);
    Q_INVOKABLE void startVocabulary(const QStringList &existingVocabulary);
    Q_INVOKABLE void startTagging(const QStringList &vocabulary);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
signals:
    void changed();
private:
    void start(const QString &mode, const QStringList &vocabulary);
    void advance();
    void receive(const QString &response, quint64 generation);
    void refreshCurrent();
    [[nodiscard]] bool cardCurrent(const QVariantMap &card) const;
    void configurationChanged();
    void fail(const QString &code, const QString &message, const QString &action, const QString &detail = {});
    AppController *const m_app;
    QPointer<Summarizer> m_provider{};
    quint64 m_job = 0;
    quint64 m_generation = 0;
    bool m_busy = false;
    bool m_vocabularyStale = false;
    int m_completed = 0;
    QString m_scopeName{};
    QString m_status{};
    QString m_mode{};
    QVariantMap m_errorInfo{};
    QVariantList m_cards{};
    QVariantList m_results{};
    QStringList m_vocabulary{};
    QStringList m_suggestedVocabulary{};
    QHash<QString, QVariantMap> m_currentCards{};
};
