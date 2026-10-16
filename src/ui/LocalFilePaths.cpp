#include "LocalFilePaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

LocalFilePaths::LocalFilePaths(QObject *parent)
    : QObject(parent) {}

QUrl LocalFilePaths::fromLocalPath(const QString &path) const {
    if (path.trimmed().isEmpty())
        return {};
    return QUrl::fromLocalFile(path);
}

QString LocalFilePaths::initialFolder(const QString &path) const {
    const QString trimmedPath = path.trimmed();
    QString candidate;
    if (trimmedPath.isEmpty()) {
        candidate = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    } else {
        const QFileInfo info(trimmedPath);
        candidate = info.exists() && info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    }

    QDir directory(QDir::cleanPath(candidate));
    while (!directory.exists() || !QFileInfo(directory.absolutePath()).isReadable()) {
        const QString parentPath = QFileInfo(directory.absolutePath()).absolutePath();
        if (parentPath == directory.absolutePath()) {
            candidate = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
            return QDir::cleanPath(candidate);
        }
        directory.setPath(parentPath);
    }
    return QDir::cleanPath(directory.absolutePath());
}

QString LocalFilePaths::toLocalPath(const QUrl &url) const {
    if (!url.isLocalFile())
        return {};
    return url.toLocalFile();
}

bool LocalFilePaths::isLocalUrl(const QUrl &url) const {
    return url.isLocalFile();
}

bool LocalFilePaths::isReadableSelection(const QString &path, bool directory) const {
    const QFileInfo info(path);
    if (!info.exists() || !info.isReadable())
        return false;
    return directory ? info.isDir() : info.isFile();
}
