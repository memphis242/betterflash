#pragma once

#include <QObject>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>

class AppController;
class CardChecker;
class Summarizer;
class WebSearch;

class DeckChecker final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QString deckName READ deckName NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(int completed READ completed NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(QVariantMap errorInfo READ errorInfo NOTIFY changed)
public:
    DeckChecker(AppController *app, Summarizer *provider, WebSearch *search,
                CardChecker *cardChecker, QObject *parent = nullptr);
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString mode() const { return m_mode; }
    QString deckName() const { return m_deckName; }
    int total() const { return int(m_cards.size()); }
    int completed() const { return m_completed; }
    QVariantList results() const { return m_results; }
    QVariantMap errorInfo() const { return m_errorInfo; }
    Q_INVOKABLE void start(const QString &deckId, const QString &mode);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
signals:
    void changed();
private:
    void advance();
    void checkerChanged();
    void configurationChanged();
    void rebuildCurrentCards();
    bool cardCurrent(const QVariantMap &card) const;
    void appendResult(const QVariantMap &card, const QVariantMap &report,
                      const QVariantMap &failure, bool stale);
    void stopWithError(const QVariantMap &failure);
    AppController *const m_app;
    CardChecker *const m_cardChecker;
    CardChecker *m_checker = nullptr;
    bool m_busy = false;
    bool m_waiting = false;
    bool m_starting = false;
    int m_completed = 0;
    quint64 m_generation = 0;
    QString m_status{};
    QString m_mode{};
    QString m_deckName{};
    QVariantList m_cards{};
    QVariantList m_results{};
    QVariantMap m_errorInfo{};
    QHash<QString, QVariantMap> m_currentCards{};
};
