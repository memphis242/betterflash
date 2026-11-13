#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <optional>
#include <QHash>
#include <atomic>
#include <memory>

class ModelResponseCache final {
public:
    struct Lookup {
        std::optional<QJsonObject> message{};
        QVariantMap error{};
        QByteArray token{};
    };
    struct AssetFingerprint {
        QByteArray identity{};
        QByteArray digest{};
        QVariantMap error{};
    };
    explicit ModelResponseCache(const QString &dataDirectory);
    [[nodiscard]] static QByteArray assetIdentity(const QString &path);
    [[nodiscard]] struct AssetFingerprint fingerprintAsset(const QString &path, const std::shared_ptr<std::atomic_bool> &canceled);
    [[nodiscard]] struct Lookup lookup(const QByteArray &key) const;
    [[nodiscard]] QVariantMap store(const QByteArray &key, const QJsonObject &message) const;
    [[nodiscard]] QVariantMap discard(const QByteArray &key, const QByteArray &token) const;
    [[nodiscard]] QString directory() const { return m_directory; }
private:
    const QString m_parentDirectory;
    const QString m_directory;
    QHash<QByteArray, QByteArray> m_verifiedAssets{};
};
