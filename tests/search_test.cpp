#include "search/SearchText.h"

#include <QtTest>
#include <atomic>

using namespace betterflash::search;

namespace {
QVariantMap card(const QString &id, const QString &deck, const QString &front) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("deckId"), deck},
            {QStringLiteral("front"), front}, {QStringLiteral("back"), QStringLiteral("answer")},
            {QStringLiteral("tags"), QStringLiteral("math")}};
}
QVariantMap deck(const QString &id, const QString &name, const QString &parent = {}) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("parentId"), parent}};
}
}

class SearchTest : public QObject {
    Q_OBJECT
private slots:
    void cacheTracksContentAndSnapshot();
    void cacheTracksAncestryAndScope();
    void cancelledBuildStopsAndRecovers();
    void oversizedScanRetainsWorkingSet();
};

void SearchTest::cacheTracksContentAndSnapshot() {
    DocumentCache cache;
    QVariantList cards{card("a", "d", "First"), card("b", "d", "Second")};
    const auto first = cache.build(cards, {}, {});
    QVERIFY(first); QCOMPARE(first->builtCount, 2);
    auto updated = cards[0].toMap(); updated.insert("reviewCount", 9); cards[0] = updated;
    const auto reused = cache.build(cards, {}, {});
    QVERIFY(reused); QCOMPARE(reused->builtCount, 0);
    QCOMPARE(reused->documents[0].card.value("reviewCount").toInt(), 9);
    updated.insert("front", "Changed"); cards[0] = updated;
    const auto changed = cache.build(cards, {}, {});
    QVERIFY(changed); QCOMPARE(changed->builtCount, 1);
    QCOMPARE(literalMatches(changed->documents, "changed").size(), 1);
    cards.removeFirst();
    const auto removed = cache.build(cards, {}, {});
    QVERIFY(removed); QCOMPARE(removed->documents.size(), 1);
    cards.prepend(updated);
    const auto restored = cache.build(cards, {}, {});
    QVERIFY(restored); QCOMPARE(restored->builtCount, 1);
}

void SearchTest::cacheTracksAncestryAndScope() {
    DocumentCache cache;
    const QVariantList cards{card("a", "child", "First"), card("b", "other", "Second")};
    QVariantList decks{deck("root", "Root"), deck("child", "Child", "root"), deck("other", "Other")};
    QVERIFY(cache.build(cards, decks, {}));
    const auto scoped = cache.build({cards[0]}, decks, "child", {}, &cards);
    QVERIFY(scoped); QCOMPARE(scoped->builtCount, 0); QCOMPARE(scoped->documents.size(), 1);
    const auto switched = cache.build({cards[1]}, decks, "other", {}, &cards);
    QVERIFY(switched); QCOMPARE(switched->builtCount, 0);
    decks[0] = deck("root", "Renamed");
    const auto renamed = cache.build(cards, decks, {});
    QVERIFY(renamed); QCOMPARE(renamed->builtCount, 1);
    QVERIFY(renamed->documents[0].embeddingText.contains("Renamed / Child"));
    decks[1] = deck("child", "Child", "other");
    const auto moved = cache.build(cards, decks, {});
    QVERIFY(moved); QCOMPARE(moved->builtCount, 1);
    QVERIFY(moved->documents[0].embeddingText.contains("Other / Child"));
}

void SearchTest::cancelledBuildStopsAndRecovers() {
    DocumentCache cache;
    QVariantList cards;
    for (int i = 0; i < 100; ++i) cards.append(card(QString::number(i), "d", "First"));
    int checks = 0;
    std::atomic<quint64> epoch{1};
    const auto cancelled = cache.build(cards, {}, {}, [&checks, &epoch] {
        if (++checks == 20) epoch.store(2);
        return epoch.load() == 1;
    });
    QVERIFY(!cancelled); QVERIFY(checks == 20);
    const auto resumed = cache.build(cards, {}, {});
    QVERIFY(resumed); QCOMPARE(resumed->documents.size(), 100);
    QVERIFY(resumed->builtCount > 0); QVERIFY(resumed->builtCount < 100);
}

void SearchTest::oversizedScanRetainsWorkingSet() {
    DocumentCache cache(10000);
    QVariantList cards;
    for (int i = 0; i < 100; ++i) cards.append(card(QString::number(i), "d", "First"));
    const auto first = cache.build(cards, {}, {});
    const auto second = cache.build(cards, {}, {});
    QVERIFY(first); QVERIFY(second);
    QCOMPARE(first->builtCount, 100);
    QVERIFY(second->builtCount > 0); QVERIFY(second->builtCount < 100);
}

QTEST_GUILESS_MAIN(SearchTest)
#include "search_test.moc"
