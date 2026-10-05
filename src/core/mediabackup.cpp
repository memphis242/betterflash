#include "mediabackup.h"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QImageReader>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace betterflash::model {
namespace {
bool matchingHash(const QString &name,const QByteArray &bytes)
{ return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())==name.left(64); }
class FileDescriptor final {
public:
    explicit FileDescriptor(int value):m_value(value) {}
    ~FileDescriptor() {if (m_value>=0) ::close(m_value);}
    FileDescriptor(const FileDescriptor &)=delete;
    FileDescriptor &operator=(const FileDescriptor &)=delete;
    FileDescriptor(FileDescriptor &&other) noexcept:m_value(std::exchange(other.m_value,-1)) {}
    FileDescriptor &operator=(FileDescriptor &&)=delete;
    int value() const {return m_value;}
private:
    int m_value=-1;
};
std::expected<FileDescriptor,QString> openMediaDirectory(const QString &directory)
{
    const QByteArray path=QFile::encodeName(directory);
    const int descriptor=::open(path.constData(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (descriptor<0) return std::unexpected(QStringLiteral("Could not open the media directory. It must be a readable directory rather than a symbolic link: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
    return FileDescriptor(descriptor);
}
std::expected<QByteArray,QString> readImage(int directory,const QString &name)
{
    const QByteArray filename=QFile::encodeName(name);
    const FileDescriptor descriptor(::openat(directory,filename.constData(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK));
    struct stat info{};
    if (descriptor.value()<0||::fstat(descriptor.value(),&info)!=0||!S_ISREG(info.st_mode)||info.st_size<=0||info.st_size>maxImageBytes)
        return std::unexpected(QStringLiteral("Image %1 must be a regular file of at most 20 MiB; symbolic links are rejected.").arg(name));
    QFile file;
    if (!file.open(descriptor.value(),QIODevice::ReadOnly)) return std::unexpected(QStringLiteral("Could not read image %1: %2").arg(name,file.errorString()));
    const QByteArray bytes=file.read(maxImageBytes+1);
    if (file.error()!=QFileDevice::NoError)
        return std::unexpected(QStringLiteral("Could not read image %1: %2").arg(name,file.errorString()));
    const auto valid=validateMediaImage(name,bytes);
    if (!valid)
        return std::unexpected(valid.error());
    return bytes;
}
}
bool validMediaName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}\\.(png|jpg|jpeg|webp|gif)$"));
    return pattern.match(name).hasMatch();
}
std::expected<void,QString> validateMediaImage(const QString &name,const QByteArray &bytes)
{
    if (!validMediaName(name)||bytes.isEmpty()||bytes.size()>maxImageBytes||!matchingHash(name,bytes))
        return std::unexpected(QStringLiteral("Image %1 must contain at most 20 MiB and match its SHA256 filename.").arg(name));
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);reader.setDecideFormatFromContent(true);
    const QByteArray format=reader.format().toLower();
    static const QList<QByteArray> supported{"png","jpg","jpeg","webp","gif"};
    if (!supported.contains(format)||!reader.canRead())
        return std::unexpected(QStringLiteral("Image %1 is not a readable PNG, JPEG, WebP, or GIF. Insert a supported image again. For WebP on Fedora, install qt6-qtimageformats or convert the image to PNG.").arg(name));
    const QString detected=QString::fromLatin1(format=="jpeg"?QByteArray("jpg"):format);
    const QString extension=name.section(QLatin1Char('.'),1);
    if (detected!=(extension==QStringLiteral("jpeg")?QStringLiteral("jpg"):extension))
        return std::unexpected(QStringLiteral("Image %1 has content that does not match its filename extension. Insert the image again.").arg(name));
    const QSize dimensions=reader.size();
    if (!dimensions.isValid()||dimensions.isEmpty()||static_cast<qint64>(dimensions.width())*dimensions.height()>32000000)
        return std::unexpected(QStringLiteral("Image %1 must have valid dimensions and at most 32 million pixels. Resize it before inserting it.").arg(name));
    reader.setScaledSize(dimensions.scaled(QSize(1600,1600),Qt::KeepAspectRatio).expandedTo(QSize(1,1)));
    if (reader.read().isNull())
        return std::unexpected(QStringLiteral("Image %1 cannot be decoded. Export it as PNG and insert it again.").arg(name));
    return {};
}
std::expected<QStringList,QString> mediaReferences(const QList<struct Card> &cards)
{
    static const QRegularExpression pattern(QStringLiteral("media:([^\\s\\)\\]>\\\"'`]+)"));
    QSet<QString> unique;
    for (const struct Card &card:cards) {
        for (const QString &side:{card.front,card.back}) {
            auto matches=pattern.globalMatch(side);
            while (matches.hasNext()) {
                const QString name=matches.next().captured(1);
                if (!validMediaName(name)) return std::unexpected(QStringLiteral("Card %1 has an invalid media reference. Insert the image again using the image control.").arg(card.id));
                unique.insert(name);
            }
        }
    }
    QStringList names=unique.values();std::sort(names.begin(),names.end());return names;
}
std::expected<QStringList,QString> cardMediaReferences(const struct Card &card)
{
    const auto references=mediaReferences({card});
    if (!references) return std::unexpected(references.error());
    if (references->size()>maxSyncImages)
        return std::unexpected(QStringLiteral("A card can reference at most 64 distinct images. Split the content into smaller cards."));
    return *references;
}
std::expected<QList<struct MediaAttachment>,QString> mediaFromJson(const QJsonArray &media)
{
    QList<struct MediaAttachment> result;QSet<QString> names;qint64 total=0;
    if (media.size()>maxCollectionRecords) return std::unexpected(QStringLiteral("Media record limit exceeded."));
    for (const QJsonValue &value:media) {
        if (!value.isObject()||!value.toObject().value("name").isString()||!value.toObject().value("data").isString())
            return std::unexpected(QStringLiteral("Every media entry needs a filename and base64 data string."));
        const QString name=value.toObject().value("name").toString();
        const QString data=value.toObject().value("data").toString();
        if (!validMediaName(name)||names.contains(name)||data.size()>(maxImageBytes+2)/3*4)
            return std::unexpected(QStringLiteral("Media filename is invalid, duplicated, or the image exceeds 20 MiB."));
        const QByteArray encoded=data.toLatin1();
        const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded||decoded.decoded.isEmpty()||decoded.decoded.size()>maxImageBytes
            ||QString::fromLatin1(encoded)!=data||decoded.decoded.toBase64()!=encoded||!matchingHash(name,decoded.decoded))
            return std::unexpected(QStringLiteral("Image %1 has invalid base64 data or a mismatched SHA256 digest.").arg(name));
        const auto valid=validateMediaImage(name,decoded.decoded);
        if (!valid) return std::unexpected(valid.error());
        total+=decoded.decoded.size();
        if (total>maxMediaBytes) return std::unexpected(QStringLiteral("Decoded image data exceeds the 64 MiB backup limit."));
        names.insert(name);result.append(MediaAttachment{name,decoded.decoded});
    }
    return result;
}
std::expected<QList<struct MediaAttachment>,QString> readBackupMedia(const QString &directory,const QStringList &names)
{
    QList<struct MediaAttachment> result;qint64 total=0;
    if (names.isEmpty()) return result;
    const auto root=openMediaDirectory(directory);
    if (!root) return std::unexpected(root.error());
    QSet<QString> seen;
    for (const QString &name:names) {
        if (!validMediaName(name)) return std::unexpected(QStringLiteral("Invalid media filename."));
        if (seen.contains(name)) continue;
        seen.insert(name);
        const auto bytes=readImage(root->value(),name);
        if (!bytes) return std::unexpected(bytes.error());
        total+=bytes->size();
        if (total>maxMediaBytes) return std::unexpected(QStringLiteral("Referenced images exceed 64 MiB. Split the card or export a smaller collection."));
        result.append(MediaAttachment{name,*bytes});
    }
    return result;
}
std::expected<void,QString> writeBackupMedia(const QString &directory,const QList<struct MediaAttachment> &media)
{
    if (media.isEmpty()) return {};
    QSet<QString> names;qint64 total=0;
    for (const struct MediaAttachment &image:media) {
        if (names.contains(image.name))
            return std::unexpected(QStringLiteral("An image filename, size, or SHA256 digest is invalid, or the filename is duplicated."));
        const auto valid=validateMediaImage(image.name,image.bytes);
        if (!valid) return std::unexpected(valid.error());
        names.insert(image.name);total+=image.bytes.size();
        if (total>maxMediaBytes) return std::unexpected(QStringLiteral("Image data exceeds the 64 MiB storage batch limit."));
    }
    if (!QDir().mkpath(directory)) return std::unexpected(QStringLiteral("Could not create the media directory."));
    const auto root=openMediaDirectory(directory);
    if (!root) return std::unexpected(root.error());
    const int directoryDescriptor=root->value();
    const auto exists=[&](const QString &name)->std::expected<bool,QString> {
        struct stat info{};const QByteArray filename=QFile::encodeName(name);
        if (::fstatat(directoryDescriptor,filename.constData(),&info,AT_SYMLINK_NOFOLLOW)==0) return true;
        if (errno==ENOENT) return false;
        return std::unexpected(QStringLiteral("Could not inspect image %1: %2").arg(name,QString::fromLocal8Bit(std::strerror(errno))));
    };
    // Check every existing destination before adding any content-addressed files.
    for (const struct MediaAttachment &image:media) {
        const auto present=exists(image.name);
        if (!present) return std::unexpected(present.error());
        if (*present) {
            const auto existing=readImage(directoryDescriptor,image.name);
            if (!existing||*existing!=image.bytes) return std::unexpected(existing?QStringLiteral("An existing image has different bytes."):existing.error());
        }
    }
    for (const struct MediaAttachment &image:media) {
        const auto present=exists(image.name);
        if (!present) return std::unexpected(present.error());
        if (*present) {
            const auto existing=readImage(directoryDescriptor,image.name);
            if (!existing||*existing!=image.bytes) return std::unexpected(existing?QStringLiteral("An existing image has different bytes."):existing.error());
            continue;
        }
        const QString temporaryName=QStringLiteral(".import-%1").arg(uuid());
        const QString temporary=QStringLiteral("/proc/self/fd/%1/%2").arg(directoryDescriptor).arg(temporaryName);
        QSaveFile file(temporary);
        if (!file.open(QIODevice::WriteOnly)||!file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)
            ||file.write(image.bytes)!=image.bytes.size()||!file.commit())
            return std::unexpected(QStringLiteral("Could not save image %1: %2").arg(image.name,file.errorString()));
        const QByteArray sourceName=QFile::encodeName(temporaryName),targetName=QFile::encodeName(image.name);
        // An exclusive hard link publishes a complete file without replacing an existing destination.
        const int linked=::linkat(directoryDescriptor,sourceName.constData(),directoryDescriptor,targetName.constData(),0);
        const int error=errno;
        const bool removed=::unlinkat(directoryDescriptor,sourceName.constData(),0)==0;
        if (linked!=0) {
            if (error!=EEXIST) return std::unexpected(QStringLiteral("Could not publish image %1: %2").arg(image.name,QString::fromLocal8Bit(std::strerror(error))));
            const auto existing=readImage(directoryDescriptor,image.name);
            if (!existing||*existing!=image.bytes) return std::unexpected(existing?QStringLiteral("Concurrent image creation produced different content."):existing.error());
        }
        if (!removed) return std::unexpected(QStringLiteral("The image was saved, but its temporary file could not be removed. Check the media directory permissions."));
    }
    if (::fsync(directoryDescriptor)!=0) return std::unexpected(QStringLiteral("Could not make the image filenames durable. Check storage and try again: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
    return {};
}
std::expected<QString,QString> preserveDamagedMedia(const QString &directory,const QString &name)
{
    if (!validMediaName(name)) return std::unexpected(QStringLiteral("Invalid media filename."));
    const QByteArray directoryPath=QFile::encodeName(directory);
    const int descriptor=::open(directoryPath.constData(),O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if (descriptor<0&&errno==ENOENT) return QString();
    if (descriptor<0) return std::unexpected(QStringLiteral("Could not open the media directory. It must be a readable directory rather than a symbolic link: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
    const FileDescriptor root(descriptor);
    const int directoryDescriptor=root.value();
    const QByteArray source=QFile::encodeName(name);
    struct stat info{};
    if (::fstatat(directoryDescriptor,source.constData(),&info,AT_SYMLINK_NOFOLLOW)!=0) {
        if (errno==ENOENT) return QString();
        return std::unexpected(QStringLiteral("Could not inspect image %1: %2").arg(name,QString::fromLocal8Bit(std::strerror(errno))));
    }
    if (!S_ISREG(info.st_mode))
        return std::unexpected(QStringLiteral("Image %1 is not a regular file; it cannot be preserved safely.").arg(name));
    const QString damaged=QStringLiteral("%1.damaged-%2").arg(name,uuid());
    const QByteArray target=QFile::encodeName(damaged);
    if (::renameat(directoryDescriptor,source.constData(),directoryDescriptor,target.constData())!=0)
        return std::unexpected(QStringLiteral("Could not preserve damaged image %1: %2").arg(name,QString::fromLocal8Bit(std::strerror(errno))));
    if (::fsync(directoryDescriptor)!=0)
        return std::unexpected(QStringLiteral("The damaged image was renamed, but its filename could not be made durable: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
    return damaged;
}
QJsonArray mediaToJson(const QList<struct MediaAttachment> &media)
{
    QJsonArray array;
    for (const struct MediaAttachment &image:media) array.append(QJsonObject{{"name",image.name},{"data",QString::fromLatin1(image.bytes.toBase64())}});
    return array;
}
}
