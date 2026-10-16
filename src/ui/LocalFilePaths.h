#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class LocalFilePaths final : public QObject {
    Q_OBJECT

public:
    explicit LocalFilePaths(QObject *parent = nullptr);

    Q_INVOKABLE QUrl fromLocalPath(const QString &path) const;
    Q_INVOKABLE QString initialFolder(const QString &path) const;
    Q_INVOKABLE QString toLocalPath(const QUrl &url) const;
    Q_INVOKABLE bool isLocalUrl(const QUrl &url) const;
    Q_INVOKABLE bool isReadableSelection(const QString &path, bool directory) const;
};
