#pragma once

#include <QDateTime>
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>
#include <QVariantList>
#include <expected>

namespace betterflash::model {

inline constexpr int maxTextLength = 1024 * 1024;
inline constexpr int maxCollectionRecords = 100000;
inline constexpr qsizetype maxSyncEventBytes = 3 * 1024 * 1024;
inline constexpr qsizetype maxSyncBatchBytes = 3584 * 1024;
static_assert(maxSyncBatchBytes > maxSyncEventBytes);

struct ClozePart {
    int start = 0;
    int length = 0;
    int group = 1;
    QString answer;
    QString hint;
};
struct Deck {
    QString id;
    QString name;
    QString description;
    QString createdAt;
    QString parentId = {};
};
struct Card {
    QString id;
    QString deckId;
    QString kind;
    QString front;
    QString back;
    QString tags;
    int pointCount = 1;
};
struct Variant {
    QString id;
    QString cardId;
    QString key;
    QString due;
    int reviewCount = 0;
    double stability = 1.0;
    double difficulty = 0.5;
};
struct Review {
    QString id;
    QString cardId;
    QString variantId;
    QString deckName;
    int grade = 0;
    double recallFraction = 0.0;
    double responseSeconds = 0.0;
    QString reviewedAt;
    QString due;
};
struct DeletedRecord {
    QString type;
    QString id;
};
struct MediaAttachment {
    QString name;
    QByteArray bytes;
};
struct Collection {
    QList<struct Deck> decks;
    QList<struct Card> cards;
    QList<struct Variant> variants;
    QList<struct Review> reviews;
    QList<struct DeletedRecord> deleted;
    QList<struct MediaAttachment> media;
};

QString uuid();
bool validUuid(const QString &value);
QString nowUtc();
std::expected<QString, QString> utcInstant(const QString &value);
std::expected<QList<struct ClozePart>, QString> parseCloze(const QString &source);
std::expected<QStringList, QString> variantKeys(const struct Card &card);
QString variantId(const QString &cardId, const QString &key);
QString question(const struct Card &card, const QString &key);
QString answer(const struct Card &card, const QString &key);
double readingBudget(const QString &question);
QString gradeLabel(int grade);
QVariantMap toMap(const struct Deck &record);
QVariantMap toMap(const struct Card &record);
QVariantMap toMap(const struct Variant &record);
QVariantMap toMap(const struct Review &record);
std::expected<struct Deck, QString> deckFromMap(const QVariantMap &value);
std::expected<struct Card, QString> cardFromMap(const QVariantMap &value);
std::expected<QList<struct Card>,QString> atomicCardsFromProposals(const struct Card &source,const QVariantList &proposals);
std::expected<struct Variant, QString> variantFromMap(const QVariantMap &value);
std::expected<struct Review, QString> reviewFromMap(const QVariantMap &value);
std::expected<struct Collection, QString> collectionFromJson(const QJsonObject &value);

}
