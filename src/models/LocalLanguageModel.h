#pragma once

#include "ModelDownload.h"
#include <QJsonObject>
#include <atomic>
#include <expected>
#include <memory>

struct LocalLanguageResult {
    QString text{};
    QString accelerator{};
};
struct LocalLanguageSpec {
    QString filename{};
    QString label{};
    struct ModelDownloadAsset asset{};
};

class LocalLanguageModel final {
public:
    [[nodiscard]] static struct LocalLanguageSpec spec(const QString &profile);
    [[nodiscard]] static QString automaticProfile();
    [[nodiscard]] static std::expected<struct LocalLanguageResult, QString> generate(
        const QString &path, const QString &acceleration, const QString &profile,
        const QJsonObject &payload, const std::shared_ptr<std::atomic_bool> &canceled);
};
