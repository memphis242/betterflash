#pragma once

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>
#include <atomic>
#include <memory>

class QNetworkReply;

struct ModelDownloadAsset {
    QUrl url{};
    QString filename{};
    qint64 bytes = 0;
    QByteArray sha256{};
    // Archive contents below this prefix are installed into extractDirectory.
    QString archivePrefix{};
    QString extractDirectory{};
};

class ModelDownload final : public QObject {
    Q_OBJECT
public:
    explicit ModelDownload(QString directory, QObject *parent = nullptr);
    ~ModelDownload() override;
    void configure(const QList<struct ModelDownloadAsset> &assets);
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool downloading() const { return m_downloading; }
    [[nodiscard]] double progress() const { return m_progress; }
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QString error() const { return m_error; }
    [[nodiscard]] QString directory() const { return m_directory; }
    void start();
    void cancel();
signals:
    void changed();
    void finished(bool success);
private:
    void next();
    void verifyAndInstall();
    void fail(const QString &message);
    void updateProgress(qint64 current);
    const QString m_directory;
    QList<struct ModelDownloadAsset> m_assets{};
    QNetworkAccessManager m_network{};
    QPointer<QNetworkReply> m_reply{};
    std::unique_ptr<QSaveFile> m_file{};
    std::unique_ptr<QCryptographicHash> m_hash{};
    std::shared_ptr<std::atomic_bool> m_cancel = std::make_shared<std::atomic_bool>(false);
    quint64 m_generation = 0;
    int m_index = 0;
    qint64 m_received = 0;
    qint64 m_completedBytes = 0;
    qint64 m_totalBytes = 0;
    bool m_downloading = false;
    double m_progress = 0;
    QString m_status{};
    QString m_error{};
};
