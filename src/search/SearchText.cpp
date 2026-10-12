#include "SearchText.h"

#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace betterflash::search {
namespace {

constexpr int maxCardTokens = 2048;

QString fold(QString value) {
    value = value.normalized(QString::NormalizationForm_D);
    QString result;
    result.reserve(value.size());
    for (const QChar character : value) {
        if (character.category() != QChar::Mark_NonSpacing
            && character.category() != QChar::Mark_SpacingCombining
            && character.category() != QChar::Mark_Enclosing) {
            result.append(character);
        }
    }
    return result.toCaseFolded().simplified();
}

QString markdownText(QString value) {
    static const QRegularExpression image(R"(!\[([^\]]*)\]\([^)]*\))");
    static const QRegularExpression link(R"(\[([^\]]+)\]\([^)]*\))");
    static const QRegularExpression url(R"((?:https?://|media:)[^\s)]+)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression html(R"(<\/?(?:p|br|div|span|em|strong|code|pre|h[1-6]|ul|ol|li|blockquote)(?:\s[^>]*)?>)",
                                          QRegularExpression::CaseInsensitiveOption);
    Q_ASSERT(html.isValid());
    value.replace(image, QStringLiteral("\\1"));
    value.replace(link, QStringLiteral("\\1"));
    value.replace(url, QStringLiteral(" "));
    value.replace(html, QStringLiteral(" "));
    value.replace(QStringLiteral("```"), QStringLiteral(" "));
    value.replace(QRegularExpression(R"((^|\s)[>#*_`~-]+(?=\S))"), QStringLiteral("\\1"));
    return value;
}

QStringList words(const QString &value) {
    QStringList result;
    static const QRegularExpression expression(QStringLiteral(R"([\p{L}\p{N}]+)"));
    auto match = expression.globalMatch(value);
    while (match.hasNext()) result.append(match.next().captured().toCaseFolded());
    return result;
}

QString normalizedField(const QString &value) { return fold(markdownText(value)); }

bool isBoundary(const QString &value, int index) {
    if (index < 0 || index >= value.size()) return true;
    const QChar c = value.at(index);
    return !(c.isLetterOrNumber() || c == QLatin1Char('_'));
}

bool wholePhrase(const QString &field, const QString &query) {
    if (query.isEmpty() || field == query) return !query.isEmpty();
    int position = field.indexOf(query);
    while (position >= 0) {
        if (isBoundary(field, position - 1) && isBoundary(field, position + query.size())) return true;
        position = field.indexOf(query, position + 1);
    }
    return false;
}

QString deckPath(const QString &id, const QHash<QString, QVariantMap> &byId) {
    QStringList names;
    QSet<QString> seen;
    QString current = id;
    for (int depth = 0; !current.isEmpty() && depth < 128 && !seen.contains(current); ++depth) {
        seen.insert(current);
        const QVariantMap deck = byId.value(current);
        if (deck.isEmpty()) break;
        names.prepend(deck.value(QStringLiteral("name")).toString());
        current = deck.value(QStringLiteral("parentId")).toString();
    }
    return names.join(QStringLiteral(" / "));
}

QString semanticHints(const QString &text) {
    QString lower = text.toCaseFolded();
    lower.replace(QStringLiteral("\\div"), QStringLiteral("÷"));
    lower.replace(QStringLiteral("\\times"), QStringLiteral("×"));
    lower.replace(QStringLiteral("\\cdot"), QStringLiteral("×"));
    QStringList hints;
    const bool percentage = lower.contains(QLatin1Char('%'));
    if (percentage) hints << QStringLiteral("percentage proportion percent calculation");
    if (lower.contains(QRegularExpression(QStringLiteral(R"(\b\d+(?:\.\d+)?\s*/\s*\d+(?:\.\d+)?\b)")))
        || lower.contains(QStringLiteral("÷")) || lower.contains(QStringLiteral("\\frac{")))
        hints << QStringLiteral("division quotient ratio inverse of multiplication");
    if (lower.contains(QRegularExpression(QStringLiteral(R"(\b\d+(?:\.\d+)?\s*[×*]\s*\d+(?:\.\d+)?\b)")))
        || lower.contains(QStringLiteral("×"))
        || lower.contains(QRegularExpression(QStringLiteral(R"(\b\d+\s*[a-zA-Z]\s*=)")))) {
        hints << QStringLiteral("multiplication product times");
    }
    if (percentage && lower.contains(QRegularExpression(QStringLiteral(R"(\b(?:of|times)\b)"))))
        hints << QStringLiteral("multiplication product times");
    if (lower.contains(QRegularExpression(QStringLiteral(R"(\b\d+\s*[a-zA-Z]\s*=)"))))
        hints << QStringLiteral("algebra equation solving division");
    if (lower.contains(QStringLiteral("∫")) || lower.contains(QStringLiteral("\\int")) || lower.contains(QStringLiteral("integral"))) hints << QStringLiteral("integral calculus antiderivative");
    if (lower.contains(QStringLiteral("∂")) || lower.contains(QStringLiteral("derivative"))) hints << QStringLiteral("derivative calculus rate of change");
    return hints.join(QLatin1Char(' '));
}

int damerauDistance(const QString &left, const QString &right, int limit) {
    if (left == right) return 0;
    if (qAbs(left.size() - right.size()) > limit) return limit + 1;
    const int columns = right.size() + 1;
    QList<int> previous(columns), current(columns), prior(columns);
    for (int j = 0; j < columns; ++j) previous[j] = j;
    for (int i = 1; i <= left.size(); ++i) {
        current[0] = i;
        int rowBest = current[0];
        for (int j = 1; j < columns; ++j) {
            const int substitution = previous[j - 1] + (left.at(i - 1) == right.at(j - 1) ? 0 : 1);
            current[j] = qMin(qMin(previous[j] + 1, current[j - 1] + 1), substitution);
            if (i > 1 && j > 1 && left.at(i - 1) == right.at(j - 2) && left.at(i - 2) == right.at(j - 1))
                current[j] = qMin(current[j], prior[j - 2] + 1);
            rowBest = qMin(rowBest, current[j]);
        }
        if (rowBest > limit) return limit + 1;
        prior = previous;
        previous = current;
    }
    return previous.last();
}

void sortHits(QList<SearchHit> &hits) {
    std::stable_sort(hits.begin(), hits.end(), [](const struct SearchHit &a, const struct SearchHit &b) {
        if (a.tier != b.tier) return a.tier < b.tier;
        if (a.score != b.score) return a.score > b.score;
        return a.id < b.id;
    });
}

} // namespace

QString normalizedQuery(const QString &query) {
    return fold(query);
}

QList<SearchDocument> documents(const QVariantList &cards, const QVariantList &decks, const QString &deckId) {
    QList<SearchDocument> result;
    QHash<QString, QVariantMap> byId;
    for (const QVariant &entry : decks) {
        const QVariantMap deck = entry.toMap();
        const QString id = deck.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) byId.insert(id, deck);
    }
    for (const QVariant &entry : cards) {
        const QVariantMap card = entry.toMap();
        if (!deckId.isEmpty() && card.value(QStringLiteral("deckId")).toString() != deckId) continue;
        const QString path = deckPath(card.value(QStringLiteral("deckId")).toString(), byId);
        const QString front = card.value(QStringLiteral("front")).toString();
        const QString back = card.value(QStringLiteral("back")).toString();
        const QString tags = card.value(QStringLiteral("tags")).toString();
        const QStringList fields{normalizedField(front), normalizedField(back), normalizedField(tags)};
        QString embedding = markdownText(front) + QLatin1Char('\n') + markdownText(back);
        if (!tags.trimmed().isEmpty()) embedding += QStringLiteral("\nTags: ") + tags;
        if (!path.isEmpty()) embedding += QStringLiteral("\nDeck: ") + path;
        const QString hints = semanticHints(markdownText(front) + QLatin1Char('\n') + markdownText(back));
        if (!hints.isEmpty()) embedding += QStringLiteral("\nConcepts: ") + hints;
        embedding = embedding.simplified();
        const QString hashInput = QStringLiteral("betterflash-search-v1\n") + embedding;
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(hashInput.toUtf8(), QCryptographicHash::Sha256).toHex());
        const QStringList allTokens = words(fields.join(QLatin1Char(' ')));
        const struct SearchDocument document{card.value(QStringLiteral("id")).toString(), hash, fields,
                                             allTokens.mid(0, maxCardTokens), embedding, card};
        result.append(document);
    }
    return result;
}

QList<SearchHit> literalMatches(const QList<SearchDocument> &docs, const QString &query) {
    const QString normalized = normalizedQuery(query);
    const QStringList queryWords = words(normalized);
    QList<SearchHit> hits;
    if (normalized.isEmpty()) return hits;
    for (const struct SearchDocument &doc : docs) {
        int tier = std::numeric_limits<int>::max();
        float score = 0.0F;
        for (const QString &field : doc.fields) {
            if (wholePhrase(field, normalized)) { tier = 0; score = 1.0F; break; }
        }
        if (tier != 0 && !queryWords.isEmpty()) {
            int prefixCount = 0;
            for (const QString &queryWord : queryWords)
                if (std::any_of(doc.tokens.cbegin(), doc.tokens.cend(), [&queryWord](const QString &token) { return token.startsWith(queryWord); })) ++prefixCount;
            if (prefixCount == queryWords.size()) { tier = 1; score = static_cast<float>(prefixCount) / qMax(1, doc.tokens.size()); }
        }
        if (tier == std::numeric_limits<int>::max()) {
            for (const QString &field : doc.fields) {
                if (field.contains(normalized)) { tier = 2; score = static_cast<float>(normalized.size()) / qMax(1, field.size()); break; }
            }
        }
        if (tier != std::numeric_limits<int>::max()) hits.append(SearchHit{doc.id, tier, score});
    }
    sortHits(hits);
    return hits;
}

QList<SearchHit> fuzzyMatches(const QList<SearchDocument> &docs, const QString &query) {
    const QString normalized = normalizedQuery(query);
    const QStringList queryWords = words(normalized);
    QList<SearchHit> hits;
    if (normalized.size() < 3 || queryWords.isEmpty()) return hits;
    QSet<QString> literalIds;
    for (const struct SearchHit &hit : literalMatches(docs, normalized)) literalIds.insert(hit.id);
    for (const struct SearchDocument &doc : docs) {
        if (literalIds.contains(doc.id)) continue;
        int totalDistance = 0;
        bool matched = true;
        for (const QString &queryWord : queryWords) {
            if (queryWord.size() < 4) { matched = false; break; }
            int best = std::numeric_limits<int>::max();
            for (const QString &token : doc.tokens) {
                if (token.size() < 4 || token.size() > 64) continue;
                if (token == queryWord) { best = 0; break; }
                const int limit = qMax(1, qMin(3, queryWord.size() / 3));
                best = qMin(best, damerauDistance(queryWord, token, limit));
            }
            if (best == std::numeric_limits<int>::max() || best > qMax(1, qMin(3, queryWord.size() / 3))) { matched = false; break; }
            totalDistance += best;
        }
        if (matched) hits.append(SearchHit{doc.id, 3, 1.0F / (1.0F + static_cast<float>(totalDistance))});
    }
    sortHits(hits);
    return hits;
}

} // namespace betterflash::search
