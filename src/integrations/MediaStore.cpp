#include "MediaStore.h"
#include "core/mediabackup.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QtConcurrent>

namespace {
struct ImportResult { QString reference; QString error; };
std::expected<QByteArray, QString> imageFormat(const QByteArray &data) {
    if (data.isEmpty() || data.size() > betterflash::model::maxImageBytes)
        return std::unexpected(QStringLiteral("IMAGE_SIZE: Choose an image smaller than 20 MiB."));
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QByteArray format = reader.format().toLower();
    const QList<QByteArray> supported{"png", "jpg", "jpeg", "webp", "gif"};
    if (!supported.contains(format) || !reader.canRead())
        return std::unexpected(QStringLiteral("IMAGE_FORMAT: Use a readable PNG, JPEG, WebP, or GIF image. For WebP on Fedora, install qt6-qtimageformats or export the image as PNG."));
    const QSize dimensions = reader.size();
    if (!dimensions.isValid() || static_cast<qint64>(dimensions.width()) * dimensions.height() > 32000000)
        return std::unexpected(QStringLiteral("IMAGE_DIMENSIONS: Choose an image with at most 32 million pixels."));
    reader.setScaledSize(dimensions.scaled(QSize(1600, 1600), Qt::KeepAspectRatio));
    if (reader.read().isNull())
        return std::unexpected(QStringLiteral("IMAGE_DECODE: This image cannot be decoded. Export it as a PNG and try again."));
    return format == "jpeg" ? QByteArray("jpg") : format;
}
struct ImportResult importFile(const QString &path, const QString &directory, QString alt) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {{}, QStringLiteral("IMAGE_READ: Choose a readable image file.")};
    if (file.size() <= 0 || file.size() > betterflash::model::maxImageBytes)
        return {{}, QStringLiteral("IMAGE_SIZE: Choose an image smaller than 20 MiB.")};
    const QByteArray data = file.read(betterflash::model::maxImageBytes + 1);
    if (data.size() != file.size()) return {{}, QStringLiteral("IMAGE_READ: The image changed while being read. Try again.")};
    const auto format = imageFormat(data);
    if (!format) return {{}, format.error()};
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    const QString extension = QString::fromLatin1(*format);
    const QString name = digest + QLatin1Char('.') + extension;
    const auto saved = betterflash::model::writeBackupMedia(directory, {{name, data}});
    if (!saved) return {{}, QStringLiteral("IMAGE_STORAGE: ") + saved.error()};
    alt.replace(QLatin1Char(']'), QLatin1String("\\]"));
    alt.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return {QStringLiteral("![%1](media:%2)").arg(alt.left(200), name), {}};
}
}
MediaStore::MediaStore(const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_rootPath(QDir(dataDirectory).filePath(QStringLiteral("media"))) {}
bool MediaStore::validName(const QString &name) {
    return betterflash::model::validMediaName(name);
}
std::expected<void, QString> MediaStore::validateImage(const QString &name, const QByteArray &bytes) {
    return betterflash::model::validateMediaImage(name, bytes);
}
void MediaStore::importImage(const QUrl &file, const QString &alt) {
    if (m_busy) return;
    m_error.clear();
    if (!file.isLocalFile()) {
        m_error = QStringLiteral("IMAGE_PATH: Choose a local image file.");
        emit changed(); return;
    }
    m_busy = true;
    emit changed();
    auto *const watcher = new QFutureWatcher<struct ImportResult>(this);
    connect(watcher, &QFutureWatcher<struct ImportResult>::finished, this, [this, watcher] {
        const struct ImportResult result = watcher->result();
        m_busy = false;
        m_error = result.error;
        emit changed();
        if (result.error.isEmpty()) emit imageImported(result.reference);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(importFile, file.toLocalFile(), m_rootPath, alt));
}
