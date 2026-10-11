#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace betterflash::search {

struct SearchDocument {
    QString id;
    QString contentHash;
    QStringList fields;
    QStringList tokens;
    QString embeddingText;
    QVariantMap card;
};

struct SearchHit {
    QString id;
    int tier = 0;
    float score = 0.0F;
};

QList<SearchDocument> documents(const QVariantList &cards, const QVariantList &decks,
                                const QString &deckId);
QList<SearchHit> literalMatches(const QList<SearchDocument> &documents, const QString &query);
QList<SearchHit> fuzzyMatches(const QList<SearchDocument> &documents, const QString &query);
QString normalizedQuery(const QString &query);

} // namespace betterflash::search
