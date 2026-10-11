#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVector>

#include <expected>
#include <memory>

struct ModelAsset {
    QString filename;
    QUrl url;
    QByteArray sha256;
    qint64 bytes = 0;
};

struct LocalModelSpec {
    QString id;
    QString label;
    int dimensions = 0;
    QList<ModelAsset> assets;
};

class LocalEmbedder final {
public:
    LocalEmbedder();
    ~LocalEmbedder();
    LocalEmbedder(LocalEmbedder&&) noexcept;
    LocalEmbedder& operator=(LocalEmbedder&&) noexcept;
    LocalEmbedder(const LocalEmbedder&) = delete;
    LocalEmbedder& operator=(const LocalEmbedder&) = delete;

    static LocalModelSpec spec(const QString& profile);

    std::expected<void, QString> load(const QString& directory, const QString& profile);
    std::expected<QVector<float>, QString> encode(const QString& text, bool query) const;

private:
    struct State;
    std::unique_ptr<State> m_state;
};
