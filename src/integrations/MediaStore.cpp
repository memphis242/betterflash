#include "MediaStore.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtConcurrent>

namespace {
struct ImportResult { QString reference; QString error; };
struct ImportResult importFile(const QString &path, const QString &directory, QString alt) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {{}, QStringLiteral("IMAGE_READ: Choose a readable image file.")};
    if (file.size() <= 0 || file.size() > 20 * 1024 * 1024)
        return {{}, QStringLiteral("IMAGE_SIZE: Choose an image smaller than 20 MiB.")};
    const QByteArray data = file.readAll();
    if (data.size() != file.size()) return {{}, QStringLiteral("IMAGE_READ: The image changed while being read. Try again.")};
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QByteArray format = reader.format().toLower();
    const QList<QByteArray> supported{"png", "jpg", "jpeg", "webp", "gif"};
    if (!supported.contains(format) || !reader.canRead())
        return {{}, QStringLiteral("IMAGE_FORMAT: Use a PNG, JPEG, WebP, or GIF image.")};
    const QSize dimensions = reader.size();
    if (!dimensions.isValid() || static_cast<qint64>(dimensions.width()) * dimensions.height() > 32000000)
        return {{}, QStringLiteral("IMAGE_DIMENSIONS: Choose an image with at most 32 million pixels.")};
    reader.setScaledSize(dimensions.scaled(QSize(1600, 1600), Qt::KeepAspectRatio));
    if (reader.read().isNull())
        return {{}, QStringLiteral("IMAGE_DECODE: This image cannot be decoded. Export it as a PNG and try again.")};
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    const QString extension = format == "jpeg" ? QStringLiteral("jpg") : QString::fromLatin1(format);
    const QString name = digest + QLatin1Char('.') + extension;
    if (!QDir().mkpath(directory)) return {{}, QStringLiteral("IMAGE_STORAGE: Cannot create the collection's image directory.")};
    const QString output = QDir(directory).filePath(name);
    if (!QFile::exists(output)) {
        QSaveFile destination(output);
        if (!destination.open(QIODevice::WriteOnly) || destination.write(data) != data.size() || !destination.commit())
            return {{}, QStringLiteral("IMAGE_STORAGE: Cannot save the image. Check free space and directory permissions.")};
    }
    alt.replace(QLatin1Char(']'), QLatin1String("\\]"));
    alt.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return {QStringLiteral("![%1](media:%2)").arg(alt.left(200), name), {}};
}
}
MediaStore::MediaStore(const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_rootPath(QDir(dataDirectory).filePath(QStringLiteral("media"))) {}
bool MediaStore::validName(const QString &name) {
    static const QRegularExpression pattern(QStringLiteral("^[a-f0-9]{64}\\.(png|jpg|jpeg|webp|gif)$"));
    return pattern.match(name).hasMatch();
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

