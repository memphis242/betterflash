#include "DeckChecker.h"
#include "CardChecker.h"
#include "Summarizer.h"
#include "WebSearch.h"
#include "core/appcontroller.h"

#include <QQueue>
#include <QSet>
#include <QTimer>

namespace {
QVariantMap error(const QString &code, const QString &message, const QString &action, const QString &detail = {}) {
    return {{QStringLiteral("code"), code}, {QStringLiteral("message"), message},
            {QStringLiteral("action"), action}, {QStringLiteral("detail"), detail}};
}
}

DeckChecker::DeckChecker(AppController *app, Summarizer *provider, WebSearch *search,
                         CardChecker *cardChecker, QObject *parent)
    : QObject(parent), m_app(app), m_provider(provider), m_cardChecker(cardChecker) {
    Q_ASSERT(app && provider && search && cardChecker);
    // Register before the checker's configuration handlers so an interrupted
    // request is never mistaken for a completed card.
    connect(provider, &Summarizer::configurationChanged, this, &DeckChecker::configurationChanged);
    connect(provider, &Summarizer::actualCredentialChanged, this, &DeckChecker::configurationChanged);
    connect(search, &WebSearch::configurationChanged, this, &DeckChecker::configurationChanged);
    m_checker = new CardChecker(app, provider, search, this, this);
    connect(m_checker, &CardChecker::changed, this, &DeckChecker::checkerChanged);
    connect(app, &AppController::cardsSnapshotChanged, this, [this] {
        if (m_cards.isEmpty()) return;
        rebuildCurrentCards();
        QSet<QString> recorded;
        for (QVariant &value : m_results) {
            QVariantMap row = value.toMap();
            recorded.insert(row.value(QStringLiteral("cardId")).toString());
            if (!cardCurrent(row.value(QStringLiteral("snapshot")).toMap())) {
                row.insert(QStringLiteral("stale"), true);
                value = row;
            }
        }
        for (int index = 0; index < m_completed; ++index) {
            const QVariantMap card = m_cards.at(index).toMap();
            const QString id = card.value(QStringLiteral("id")).toString();
            if (!recorded.contains(id) && !cardCurrent(card)) {
                recorded.insert(id);
                appendResult(card, {}, error(QStringLiteral("DECK_CHECK_CARD_CHANGED"),
                    QStringLiteral("This card changed or became unavailable after it was checked."),
                    QStringLiteral("Check the current card again.")), true);
            }
        }
        emit changed();
    });
}

void DeckChecker::rebuildCurrentCards() {
    m_currentCards.clear();
    for (const QVariant &value : m_app->cards()) {
        const QVariantMap card = value.toMap();
        m_currentCards.insert(card.value(QStringLiteral("id")).toString(), card);
    }
}

bool DeckChecker::cardCurrent(const QVariantMap &card) const {
    const auto it = m_currentCards.constFind(card.value(QStringLiteral("id")).toString());
    if (it == m_currentCards.cend() || !it->value(QStringLiteral("loadError")).toMap().isEmpty()) return false;
    for (const QString &field : {QStringLiteral("front"), QStringLiteral("back"), QStringLiteral("kind")}) {
        if (it->value(field) != card.value(field)) return false;
    }
    return true;
}

void DeckChecker::start(const QString &deckId, const QString &mode) {
    if (m_busy) return;
    clear();
    m_provider->beginDiagnosticOperation(this);
    // A card check and deck check share one web search request at a time.
    if (m_cardChecker->busy()) {
        stopWithError(error(QStringLiteral("DECK_CHECK_BUSY"), QStringLiteral("A card check is already running."),
                            QStringLiteral("Finish or cancel that check before checking a deck.")));
        return;
    }
    if (mode != QStringLiteral("correctness") && mode != QStringLiteral("completeness")) {
        stopWithError(error(QStringLiteral("DECK_CHECK_MODE"), QStringLiteral("The requested deck check is unavailable."),
                            QStringLiteral("Choose correctness or completeness and try again.")));
        return;
    }
    QHash<QString, QStringList> children;
    bool found = false;
    for (const QVariant &value : m_app->decks()) {
        const QVariantMap deck = value.toMap();
        const QString id = deck.value(QStringLiteral("id")).toString();
        children[deck.value(QStringLiteral("parentId")).toString()].append(id);
        if (id == deckId && !id.isEmpty()) { found = true; m_deckName = deck.value(QStringLiteral("name")).toString(); }
    }
    if (!found) {
        stopWithError(error(QStringLiteral("DECK_CHECK_NOT_FOUND"), QStringLiteral("This deck no longer exists."),
                            QStringLiteral("Select an existing deck and try again.")));
        return;
    }
    m_mode = mode;
    QSet<QString> included{deckId};
    QQueue<QString> pending;
    pending.enqueue(deckId);
    while (!pending.isEmpty()) {
        const QString parent = pending.dequeue();
        for (const QString &child : children.value(parent)) {
            if (!child.isEmpty() && !included.contains(child)) { included.insert(child); pending.enqueue(child); }
        }
    }
    rebuildCurrentCards();
    for (const QVariant &value : m_app->cards()) {
        const QVariantMap card = value.toMap();
        if (included.contains(card.value(QStringLiteral("deckId")).toString())) m_cards.append(card);
    }
    if (m_cards.isEmpty()) {
        m_status = QStringLiteral("This deck and its subdecks have no cards to check.");
        emit changed();
        return;
    }
    m_busy = true;
    m_status = QStringLiteral("Preparing deck check...");
    emit changed();
    const quint64 generation = m_generation;
    QTimer::singleShot(0, this, [this, generation] { if (generation == m_generation) advance(); });
}

void DeckChecker::advance() {
    if (!m_busy) return;
    Q_ASSERT(m_completed >= 0 && m_completed <= m_cards.size());
    if (m_completed == m_cards.size()) {
        m_busy = false;
        m_status = QStringLiteral("Deck snapshot check complete.");
        emit changed();
        return;
    }
    const QVariantMap card = m_cards.at(m_completed).toMap();
    const QVariantMap loadError = card.value(QStringLiteral("loadError")).toMap();
    if (!loadError.isEmpty()) {
        appendResult(card, {}, error(QStringLiteral("DECK_CHECK_CARD_UNREADABLE"),
            QStringLiteral("This card could not be read and was not checked."),
            QStringLiteral("Repair the card, then check it again."),
            loadError.value(QStringLiteral("message")).toString()), true);
        ++m_completed;
        emit changed();
        const quint64 generation = m_generation;
        QTimer::singleShot(0, this, [this, generation] { if (generation == m_generation) advance(); });
        return;
    }
    if (!cardCurrent(card)) {
        appendResult(card, {}, error(QStringLiteral("DECK_CHECK_CARD_CHANGED"),
            QStringLiteral("This card changed or became unavailable before it could be checked."),
            QStringLiteral("Check the current card again.")), true);
        ++m_completed;
        emit changed();
        const quint64 generation = m_generation;
        QTimer::singleShot(0, this, [this, generation] { if (generation == m_generation) advance(); });
        return;
    }
    m_waiting = true;
    m_starting = true;
    m_checker->check(m_mode, card.value(QStringLiteral("front")).toString(),
                     card.value(QStringLiteral("back")).toString(), card.value(QStringLiteral("kind")).toString(),
                     true, card.value(QStringLiteral("id")).toString(), card);
    m_starting = false;
    checkerChanged();
}

void DeckChecker::checkerChanged() {
    if (!m_busy || !m_waiting || m_starting) return;
    if (m_checker->busy()) {
        m_status = m_checker->status();
        emit changed();
        return;
    }
    m_waiting = false;
    const QVariantMap card = m_cards.at(m_completed).toMap();
    const QVariantMap failure = m_checker->errorInfo();
    const QVariantMap report = m_checker->result();
    const bool stale = !cardCurrent(card);
    if (!failure.isEmpty() || stale || report.value(QStringLiteral("verdict")).toString() != QStringLiteral("no_issues"))
        appendResult(card, report, failure, stale);
    ++m_completed;
    if (!failure.isEmpty()) {
        stopWithError(error(QStringLiteral("DECK_CHECK_CARD_FAILED"),
            QStringLiteral("The deck check stopped because a card could not be checked."),
            QStringLiteral("Review the card's error below, then run the check again."),
            failure.value(QStringLiteral("code")).toString()));
        return;
    }
    emit changed();
    const quint64 generation = m_generation;
    QTimer::singleShot(0, this, [this, generation] { if (generation == m_generation) advance(); });
}

void DeckChecker::appendResult(const QVariantMap &card, const QVariantMap &report,
                               const QVariantMap &failure, const bool stale) {
    m_results.append(QVariantMap{{QStringLiteral("cardId"), card.value(QStringLiteral("id"))},
        {QStringLiteral("deckId"), card.value(QStringLiteral("deckId"))},
        {QStringLiteral("front"), card.value(QStringLiteral("front"))},
        {QStringLiteral("snapshot"), card}, {QStringLiteral("report"), report},
        {QStringLiteral("errorInfo"), failure}, {QStringLiteral("stale"), stale}});
}

void DeckChecker::stopWithError(const QVariantMap &failure) {
    m_busy = false;
    m_waiting = false;
    m_status.clear();
    m_errorInfo = failure;
    emit changed();
}

void DeckChecker::configurationChanged() {
    if (!m_busy) return;
    ++m_generation;
    m_busy = false;
    m_waiting = false;
    m_checker->cancel();
    stopWithError(error(QStringLiteral("DECK_CHECK_CONFIGURATION"),
        QStringLiteral("The deck check stopped because the model or web search settings changed."),
        QStringLiteral("Review the completed results, then run the check again with the new settings.")));
}

void DeckChecker::cancel() {
    if (!m_busy) return;
    ++m_generation;
    m_busy = false;
    m_waiting = false;
    m_checker->cancel();
    m_status = QStringLiteral("Deck check canceled. Completed results are available below.");
    emit changed();
}

void DeckChecker::clear() {
    cancel();
    ++m_generation;
    m_completed = 0;
    m_mode.clear(); m_deckName.clear(); m_status.clear();
    m_cards.clear(); m_results.clear(); m_errorInfo.clear(); m_currentCards.clear();
    emit changed();
}
