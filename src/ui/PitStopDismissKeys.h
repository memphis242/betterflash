#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>

class PitStopDismissKeys : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickItem *scope READ scope WRITE setScope NOTIFY scopeChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
public:
    explicit PitStopDismissKeys(QObject *parent = nullptr);
    [[nodiscard]] QQuickItem *scope() const { return m_scope; }
    [[nodiscard]] bool enabled() const { return m_enabled; }
    void setScope(QQuickItem *scope);
    void setEnabled(bool enabled);
signals:
    void scopeChanged();
    void enabledChanged();
    void dismissRequested();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QPointer<QQuickItem> m_scope{};
    QPointer<QQuickWindow> m_pressedWindow{};
    QSet<int> m_pressedKeys{};
    bool m_enabled = false;
};
