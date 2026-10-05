#pragma once

#include "cardmodel.h"

#include <QJsonArray>
#include <expected>

namespace betterflash::model {
inline constexpr qint64 maxImageBytes = 20 * 1024 * 1024;
inline constexpr qint64 maxMediaBytes = 64 * 1024 * 1024;
inline constexpr qint64 maxBackupBytes = 128 * 1024 * 1024;
inline constexpr int maxSyncImages = 64;
static_assert(maxMediaBytes >= maxImageBytes);
static_assert(maxBackupBytes > maxMediaBytes * 4 / 3);
bool validMediaName(const QString &name);
std::expected<void,QString> validateMediaImage(const QString &name,const QByteArray &bytes);
std::expected<QStringList,QString> mediaReferences(const QList<struct Card> &cards);
std::expected<QStringList,QString> cardMediaReferences(const struct Card &card);
std::expected<QList<struct MediaAttachment>,QString> mediaFromJson(const QJsonArray &media);
std::expected<QList<struct MediaAttachment>,QString> readBackupMedia(const QString &directory,const QStringList &names);
std::expected<void,QString> writeBackupMedia(const QString &directory,const QList<struct MediaAttachment> &media);
std::expected<QString,QString> preserveDamagedMedia(const QString &directory,const QString &name);
QJsonArray mediaToJson(const QList<struct MediaAttachment> &media);
}
