#pragma once

#include <QObject>
#include <QUrl>

class MediaStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit MediaStore(const QString &dataDirectory, QObject *parent = nullptr);
    QString rootPath() const { return m_rootPath; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    Q_INVOKABLE void importImage(const QUrl &file, const QString &alt = {});
    static bool validName(const QString &name);
signals:
    void changed();
    void imageImported(const QString &markdown);
private:
    const QString m_rootPath;
    bool m_busy = false;
    QString m_error;
};

