#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QHash>
#include <QString>
#include <expected>

class CardChatStore final {
public:
    explicit CardChatStore(const QString &dataDirectory);

    [[nodiscard]] std::expected<QJsonArray, QString> load(const QString &cardId) const;
    [[nodiscard]] std::expected<void, QString> save(const QString &cardId,
                                                    const QJsonArray &threads) const;

private:
    [[nodiscard]] QString pathFor(const QString &cardId) const;
    const QString m_rootDirectory;
    mutable QHash<QString, QByteArray> m_baselines;
};
