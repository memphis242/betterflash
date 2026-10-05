#pragma once

#include <QNetworkAccessManager>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

class AppController;
class Summarizer;
class QNetworkReply;

class Atomicizer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool applying READ applying NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString reason READ reason NOTIFY changed)
    Q_PROPERTY(QString decision READ decision NOTIFY changed)
    Q_PROPERTY(QVariantList proposals READ proposals NOTIFY changed)
    Q_PROPERTY(QVariantMap sourceCard READ sourceCard NOTIFY changed)
    Q_PROPERTY(bool hasProposal READ hasProposal NOTIFY changed)
public:
    Atomicizer(AppController *app, Summarizer *provider, QObject *parent = nullptr);
    ~Atomicizer() override;
    bool busy() const { return m_busy; }
    bool applying() const { return m_applying; }
    QString error() const { return m_error; }
    QString status() const { return m_status; }
    QString reason() const { return m_reason; }
    QString decision() const { return m_decision; }
    QVariantList proposals() const { return m_proposals; }
    QVariantMap sourceCard() const { return m_source; }
    bool hasProposal() const { return m_decision == QStringLiteral("split") && m_proposals.size() >= 2; }
    Q_INVOKABLE void propose(const QString &cardId);
    Q_INVOKABLE bool apply(const QVariantList &editedProposals);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
signals:
    void changed();
    void applied();
private:
    void fail(const QString &code, const QString &message);
    void receive(QNetworkReply *reply);
    AppController *const m_app;
    Summarizer *const m_provider;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QVariantMap m_source;
    QVariantList m_proposals;
    QString m_status;
    QString m_reason;
    QString m_decision;
    QString m_error;
    bool m_busy = false;
    bool m_applying = false;
};
