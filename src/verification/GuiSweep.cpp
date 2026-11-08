#include "GuiSweep.h"
#include "core/appcontroller.h"
#include "core/cardmodel.h"
#include "integrations/Atomicizer.h"
#include "voice/VoiceController.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStyleHints>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <QWheelEvent>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <cstdio>

namespace {
QStringList *qmlWarnings = nullptr;
QtMessageHandler previousHandler = nullptr;

void captureMessage(const QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString category = QString::fromUtf8(context.category ? context.category : "");
    const bool qml = category.contains(QStringLiteral("qml"), Qt::CaseInsensitive)
        || message.contains(QStringLiteral("QML "))
        || message.contains(QStringLiteral("qrc:/qt/qml/BetterFlash"));
    if (qmlWarnings && qml && (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg))
        qmlWarnings->append(message);
    if (previousHandler) previousHandler(type, context, message);
    else if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        std::fprintf(stderr,"%s\n",message.toLocal8Bit().constData());
}

bool waitUntil(const std::function<bool()> &predicate, int milliseconds = 5000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < milliseconds) QTest::qWait(20);
    return predicate();
}

QObject *flickableFor(QObject *object)
{
    if (!object) return nullptr;
    if (object->metaObject()->indexOfProperty("contentY") >= 0) return object;
    QObject *const content = object->property("contentItem").value<QObject *>();
    if (content && content != object) {
        if (QObject *const result = flickableFor(content)) return result;
    }
    const auto descendants = object->findChildren<QObject *>();
    for (QObject *const descendant : descendants)
        if (descendant->metaObject()->indexOfProperty("contentY") >= 0) return descendant;
    return nullptr;
}

QObject *visualObject(QQuickItem *item, const QString &name)
{
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    for (QQuickItem *const child : item->childItems())
        if (QObject *const result = visualObject(child,name)) return result;
    return nullptr;
}

class LoopbackProvider final {
public:
    LoopbackProvider()
    {
        if (!m_server.listen(QHostAddress::LocalHost,0))
            throw std::runtime_error("Cannot create the isolated loopback provider.");
        QObject::connect(&m_server,&QTcpServer::newConnection,&m_server,[this] {
            while (QTcpSocket *const socket = m_server.nextPendingConnection()) {
                const auto input = std::make_shared<QByteArray>();
                QObject::connect(socket,&QTcpSocket::readyRead,&m_server,[this,socket,input] {
                    input->append(socket->readAll());
                    const qsizetype headerEnd = input->indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;
                    const QByteArray headers = input->left(headerEnd);
                    static const QRegularExpression size(QStringLiteral("(?im)^content-length:\\s*(\\d+)\\s*$"));
                    const auto match = size.match(QString::fromLatin1(headers));
                    if (!match.hasMatch()) return;
                    const qsizetype length = match.captured(1).toLongLong();
                    if (input->size() < headerEnd + 4 + length) return;
                    ++requests;
                    static const QRegularExpression authorization(QStringLiteral("(?im)^authorization:\\s*Bearer gui-fixture-only\\s*$"));
                    onlyFixtureCredentials = onlyFixtureCredentials && authorization.match(QString::fromLatin1(headers)).hasMatch();
                    lastBody = input->mid(headerEnd + 4,length);
                    socket->disconnect(&m_server);
                    if (!respond) return;
                    const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                        + QByteArray::number(m_payload.size()) + "\r\nConnection: close\r\n\r\n" + m_payload;
                    socket->write(response);
                    socket->disconnectFromHost();
                });
                QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            }
        });
    }
    QString endpoint() const { return QStringLiteral("http://127.0.0.1:%1/chat").arg(m_server.serverPort()); }
    void proposal(const QJsonObject &value)
    {
        const QString content = QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact));
        m_payload = QJsonDocument(QJsonObject{{"choices",QJsonArray{QJsonObject{{"message",QJsonObject{{"content",content}}}}}}})
            .toJson(QJsonDocument::Compact);
    }
    int requests = 0;
    bool respond = true;
    bool onlyFixtureCredentials = true;
    QByteArray lastBody;
private:
    QTcpServer m_server;
    QByteArray m_payload;
};

class Sweep final {
public:
    Sweep(QQmlApplicationEngine &engine, AppController &app, VoiceController &voice,
          QString directory)
        : m_engine(engine), m_app(app), m_voice(voice), m_directory(std::move(directory))
    {
        if (!engine.rootObjects().isEmpty()) m_window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (m_window) m_root = engine.newQObject(m_window);
    }

    bool run()
    {
        const QString selectedCase=qEnvironmentVariable("BETTERFLASH_GUI_CASE");
        if (!check(selectedCase.isEmpty()||selectedCase==QStringLiteral("due-search"),"gui_case_supported",
                   QStringLiteral("BETTERFLASH_GUI_CASE=%1; use an empty value for the complete sweep or due-search for the focused case.").arg(selectedCase))) return finish();
        if (!check(m_window != nullptr, "window_created")) return finish();
        assert(m_window);
        if (!check(waitUntil([this] { return !m_app.busy(); }), "collection_ready")) return finish();
        if (!check(m_app.decks().isEmpty() && m_app.cards().isEmpty() && m_app.history().isEmpty(),
                   "isolated_empty_collection", "Use a fresh --data-dir for this sweep.")) return finish();
        if (!check(!m_voice.enabled(), "microphone_disabled")) return finish();
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        resize(1320, 860);
        m_window->requestActivate();
        QTest::qWait(100);
        screenshot("first-run");
        const int themeType = qmlTypeId("BetterFlash",1,0,"Theme");
        QObject *const theme = m_engine.singletonInstance<QObject *>(themeType);
        if (!theme) throw std::runtime_error("The theme singleton was not found.");
        check(!theme->property("hasChoice").toBool(), "theme_has_no_initial_user_override");
        check(theme->property("dark").toBool() == (QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light),
              "theme_follows_system_until_choice");
        check(theme->property("monoFont").toString().contains(QStringLiteral("IBM Plex Mono")), "theme_uses_IBM_Plex_Mono");
        if (!theme->property("dark").toBool()) click("themeToggle");
        seedDecks();
        const QString image = importImage();
        createCardThroughEditor(image);
        seedCards(image);
        check(m_app.cards().size() >= 22, "fixture_cards_created");
        if (visible("cardDetailDialog")) close("cardDetailDialog");
        reviewIdleBrowser();
        dueSearchPreservesContext();
        if (selectedCase==QStringLiteral("due-search")) return finish();
        const QColor firstColor = m_window->color();
        desktop("dark");
        compactDesktop("dark");
        phone("dark");
        click("themeToggle");
        check(m_window->color() != firstColor, "theme_changes_palette");
        desktop("light");
        compactDesktop("light");
        phone("light");
        click("themeToggle");
        check(m_window->color() == firstColor, "theme_round_trip");
        QObject *const themePreferences = theme->property("preferences").value<QObject *>();
        if (!themePreferences) throw std::runtime_error("The theme preferences were not found.");
        check(QMetaObject::invokeMethod(themePreferences,"sync"), "theme_preferences_sync_available");
        QTest::qWait(100);
        QSettings persisted;
        persisted.sync();
        persisted.beginGroup(QStringLiteral("appearance"));
        const bool persistedChoice = persisted.value(QStringLiteral("hasThemeChoice")).toBool();
        const bool persistedDark = persisted.value(QStringLiteral("savedDark")).toBool();
        persisted.endGroup();
        check(theme->property("hasChoice").toBool() && persistedChoice
              && themePreferences->property("savedDark").toBool() == theme->property("dark").toBool(), "theme_choice_is_persisted",
              QStringLiteral("hasChoice=%1 persistedChoice=%2 persistedDark=%3 dark=%4")
                  .arg(theme->property("hasChoice").toBool()).arg(persistedChoice)
                  .arg(persistedDark).arg(theme->property("dark").toBool()));
        resize(1320, 860);
        m_app.stopReview();
        atomicize();
        reviewLifecycle();
        check(!m_voice.enabled(), "microphone_remains_disabled");
        return finish();
    }

    void failure(const QString &message) { check(false, "sweep_exception", message); }

    bool finish()
    {
        for (const QString &warning : std::as_const(m_warnings)) check(false, "qml_warning", warning);
        const QJsonObject report{{"success",m_success},{"checks",m_checks},{"screenshots",m_screenshots},
                                 {"qmlWarnings",QJsonArray::fromStringList(m_warnings)}};
        QSaveFile file(QDir(m_directory).filePath(QStringLiteral("report.json")));
        const QByteArray bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            m_success = false;
            qCritical().noquote() << "GUI_SWEEP_WRITE: Cannot save report to" << m_directory << file.errorString();
        }
        return m_success;
    }

    QStringList &warnings() { return m_warnings; }

private:
    bool check(bool condition, const QString &name, const QString &detail = {})
    {
        m_checks.append(QJsonObject{{"name",name},{"passed",condition},{"detail",detail}});
        if (!condition) { m_success = false; qCritical().noquote() << "GUI_SWEEP:" << name << detail; }
        return condition;
    }

    QObject *object(const QString &name) const
    {
        if (!m_window) return nullptr;
        if (QObject *const owned = m_window->findChild<QObject *>(name)) return owned;
        return visualObject(m_window->contentItem(),name);
    }
    QObject *require(const QString &name) const
    {
        QObject *const value = object(name);
        if (!value) throw std::runtime_error(("Missing QML hook: " + name).toStdString());
        return value;
    }
    QQuickItem *item(const QString &name) const
    {
        QQuickItem *const value = qobject_cast<QQuickItem *>(require(name));
        if (!value) throw std::runtime_error(("QML hook is not an item: " + name).toStdString());
        return value;
    }
    bool visible(const QString &name) const { return require(name)->property("visible").toBool(); }
    QRectF rect(const QString &name) const
    {
        QQuickItem *const value = item(name);
        return value->mapRectToScene(QRectF(QPointF(), QSizeF(value->width(), value->height())));
    }
    void focus(const QString &name) { item(name)->forceActiveFocus(Qt::OtherFocusReason); QTest::qWait(30); }
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        m_window->requestActivate();
        QTest::qWait(20);
        QTest::keyClick(m_window, key, modifiers);
        QTest::qWait(65);
    }
    void click(const QString &name)
    {
        QQuickItem *const value = item(name);
        check(value->isVisible() && value->isEnabled(), "clickable_" + name);
        const QPoint position = rect(name).center().toPoint();
        if (!check(QRect(QPoint(),m_window->size()).contains(position),"click_target_inside_window_"+name,
                   QStringLiteral("point=%1,%2 window=%3x%4").arg(position.x()).arg(position.y()).arg(m_window->width()).arg(m_window->height()))) return;
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, position);
        QTest::qWait(85);
    }
    QJSValue call(const QString &name, const QVariantList &arguments = {})
    {
        QJSValueList values;
        for (const QVariant &value : arguments) values.append(m_engine.toScriptValue(value));
        QJSValue function = m_root.property(name);
        const QJSValue result = function.callWithInstance(m_root, values);
        if (result.isError()) throw std::runtime_error((name + ": " + result.toString()).toStdString());
        QTest::qWait(80);
        m_window->grabWindow();
        return result;
    }
    void close(const QString &name)
    {
        QObject *const popup = require(name);
        if (!QMetaObject::invokeMethod(popup, "close")) popup->setProperty("visible", false);
        QTest::qWait(60);
    }
    void resize(int width, int height)
    {
        m_window->resize(width,height);
        QTest::qWait(150);
    }
    void screenshot(const QString &name)
    {
        QTest::qWait(120);
        const QString filename = name + QStringLiteral(".png");
        const QString path = QDir(m_directory).filePath(filename);
        const bool saved = m_window->grabWindow().save(path);
        check(saved, "screenshot_" + name, saved ? filename : path);
        if (saved) m_screenshots.append(filename);
    }
    void layouts(const QString &suffix)
    {
        const QRectF main = rect("mainContent");
        const QRectF nav = rect("sideNavigation");
        const QRectF header = rect("globalHeader");
        check(main.width() >= m_window->width() - nav.width() - 1.1, "main_full_width_" + suffix,
              QStringLiteral("main=%1 window=%2 nav=%3").arg(main.width()).arg(m_window->width()).arg(nav.width()));
        check(main.top() >= header.bottom() - 1.1 && main.bottom() <= m_window->height() + 1.1,
              "pinned_bars_reserve_space_" + suffix);
        const auto names = {QStringLiteral("reviewHeader"),QStringLiteral("libraryDeckPane"),QStringLiteral("libraryCardPane"),QStringLiteral("settingsTopBar")};
        for (const QString &name : names) {
            if (!item(name)->isVisible()) continue;
            const QRectF bounds = rect(name);
            check(bounds.left() >= -1.1 && bounds.right() <= m_window->width() + 1.1,
                  "toolbar_inside_window_" + suffix + "_" + name);
        }
    }
    void wheel(const QString &name, const QString &suffix)
    {
        QObject *const flick = flickableFor(require(name));
        if (!check(flick != nullptr, "scroll_available_" + suffix + "_" + name)) return;
        flick->setProperty("contentY", 0.0);
        QTest::qWait(50);
        const qreal width = flick->property("width").toDouble();
        const qreal contentWidth = flick->property("contentWidth").toDouble();
        check(contentWidth <= width + 1.5, "no_horizontal_scroll_" + suffix + "_" + name,
              QStringLiteral("content=%1 viewport=%2").arg(contentWidth).arg(width));
        QQuickItem *const viewport=qobject_cast<QQuickItem *>(flick);
        if (!viewport) throw std::runtime_error("A scroll viewport is not a visual item.");
        const QRectF visibleViewport=viewport->mapRectToScene(QRectF(0,0,viewport->width(),viewport->height()))
            .intersected(QRectF(QPointF(),m_window->size()));
        if (!check(!visibleViewport.isEmpty(),"scroll_target_visible_"+suffix+"_"+name)) return;
        const QPointF position=visibleViewport.center();
        QTest::mouseMove(m_window,position.toPoint());
        qreal last = flick->property("contentY").toDouble();
        const qreal start = last;
        bool monotonic = true;
        for (int iteration = 0; iteration < 40; ++iteration) {
            QWheelEvent event(position,m_window->mapToGlobal(position.toPoint()),QPoint(),QPoint(0,-120),
                              Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QTest::lastMouseTimestamp += 35;
            event.setTimestamp(static_cast<quint64>(QTest::lastMouseTimestamp));
            QCoreApplication::sendEvent(m_window,&event);
            QTest::qWait(30);
            const qreal current = flick->property("contentY").toDouble();
            if (current + 0.5 < last) monotonic = false;
            last = current;
            const qreal maximum = std::max(0.0,flick->property("contentHeight").toDouble()-flick->property("height").toDouble());
            if (last >= maximum - 1.0 && iteration > 2) break;
        }
        QTest::qWait(120);
        if (flick->property("contentY").toDouble() + 0.5 < last) monotonic = false;
        const bool overflows = flick->property("contentHeight").toDouble() > flick->property("height").toDouble() + 5;
        check(monotonic, "wheel_never_backwards_" + suffix + "_" + name);
        check(!overflows || last > start + 1, "wheel_advances_" + suffix + "_" + name,
              QStringLiteral("start=%1 final=%2 contentHeight=%3 viewport=%4").arg(start).arg(last)
              .arg(flick->property("contentHeight").toDouble()).arg(flick->property("height").toDouble()));
    }
    QString currentVariant() const
    {
        const QVariantMap card = m_app.currentCard();
        return card.value("variantId",card.value("id")).toString();
    }
    QString cardId(const QVariantMap &card) const { return card.value("cardId",card.value("id")).toString(); }
    QString deckNamed(const QString &name) const
    {
        for (const QVariant &value : m_app.decks()) {
            const QVariantMap record = value.toMap();
            if (record.value("name").toString() == name) return record.value("id").toString();
        }
        return {};
    }
    void seedDecks()
    {
        m_app.createDeck("Fixture A", "Native interaction verification.");
        m_app.createDeck("Fixture B", "Second deck for keyboard navigation.");
        if (!check(waitUntil([this] { return !m_app.busy() && m_app.decks().size() == 2; }), "fixture_decks_created"))
            throw std::runtime_error("Could not create fixture decks.");
        m_deckA = deckNamed("Fixture A");
        m_deckB = deckNamed("Fixture B");
        m_app.createDeck("Fixture A child", "Nested fixture deck.", m_deckA);
        m_app.createDeck("Fixture B child", "Second nested fixture deck.", m_deckB);
        if (!check(waitUntil([this] { return !m_app.busy() && m_app.decks().size() == 4; }), "fixture_hierarchy_created"))
            throw std::runtime_error("Could not create fixture hierarchy.");
        m_deckChild = deckNamed("Fixture A child");
        call("setDeck",{m_deckA});
    }
    QString importImage()
    {
        const QString path = QDir(m_directory).filePath("fixture-image.png");
        QImage image(160,110,QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setPen(QPen(Qt::black,2));
        painter.drawRect(12,12,136,86);
        painter.drawLine(12,98,148,12);
        painter.end();
        if (!check(image.save(path), "fixture_image_written")) throw std::runtime_error("Could not save fixture image.");
        QObject *const media = m_engine.rootContext()->contextProperty("media").value<QObject *>();
        if (!media) throw std::runtime_error("MediaStore context was not found.");
        QSignalSpy imported(media,SIGNAL(imageImported(QString)));
        check(imported.isValid(), "image_import_signal_available");
        const bool invoked = QMetaObject::invokeMethod(media,"importImage",Q_ARG(QUrl,QUrl::fromLocalFile(path)),Q_ARG(QString,QStringLiteral("verification image")));
        if (!check(invoked && waitUntil([&imported] { return !imported.isEmpty(); }), "image_imported"))
            throw std::runtime_error("Could not import fixture image: " + media->property("error").toString().toStdString());
        return imported.first().first().toString();
    }
    void createCardThroughEditor(const QString &image)
    {
        key(Qt::Key_N,Qt::ControlModifier);
        if (!check(visible("cardDialog"), "new_card_keyboard_opens_modal")) throw std::runtime_error("Ctrl+N did not open the card editor.");
        QObject *const editor = require("editorSource");
        editor->setProperty("text",QStringLiteral("A remembered phrase"));
        editor->setProperty("cursorPosition",19);
        focus("editorSource");
        key(Qt::Key_Return,Qt::ControlModifier);
        const QString split = editor->property("text").toString();
        static const QRegularExpression separator(QStringLiteral("(?m)^---[ \\t]*$"));
        check(split.count(separator) == 1, "editor_split_creates_one_separator");
        key(Qt::Key_Return,Qt::ControlModifier);
        check(editor->property("text").toString() == split, "editor_split_moves_without_duplicate");
        check(QMetaObject::invokeMethod(editor,"select",Q_ARG(int,2),Q_ARG(int,12)), "editor_selection_available");
        key(Qt::Key_C,Qt::ControlModifier | Qt::ShiftModifier);
        check(editor->property("text").toString().contains(QStringLiteral("{{c1::remembered}}")), "editor_cloze_wraps_selection");
        QObject *const picker = require("imageDialog");
        const QUrl imageUrl = QUrl::fromLocalFile(QDir(m_directory).filePath(QStringLiteral("fixture-image.png")));
        picker->setProperty("currentFolder",QUrl::fromLocalFile(m_directory));
        const QString beforeImage = editor->property("text").toString();
        const int imageCursor = editor->property("cursorPosition").toInt();
        key(Qt::Key_I,Qt::ControlModifier | Qt::ShiftModifier);
        check(visible("imageDialog"), "editor_image_keyboard_opens_file_dialog");
        QTest::qWait(150);
        close("imageDialog");
        const bool selected = picker->setProperty("selectedFile",imageUrl);
        check(selected && picker->property("selectedFile").toUrl() == imageUrl, "image_file_dialog_selects_fixture");
        editor->setProperty("cursorPosition",0);
        check(QMetaObject::invokeMethod(picker,"accepted"), "image_file_dialog_dispatches_valid_selection");
        const QString insertedImage = QString(image).replace(QStringLiteral("verification image"),QStringLiteral("image"));
        const QString expectedImageSource = beforeImage.left(imageCursor) + QLatin1Char('\n') + insertedImage
            + QLatin1Char('\n') + beforeImage.mid(imageCursor);
        QObject *const media = m_engine.rootContext()->contextProperty("media").value<QObject *>();
        check(waitUntil([editor,&expectedImageSource] { return editor->property("text").toString() == expectedImageSource; }), "editor_image_inserts_markdown_at_saved_cursor",
              QStringLiteral("actual=%1 expected=%2 mediaError=%3 imageSession=%4 editorSession=%5")
              .arg(editor->property("text").toString(),expectedImageSource,media ? media->property("error").toString() : QStringLiteral("missing"))
              .arg(m_window->property("imageSession").toInt()).arg(m_window->property("editorSession").toInt()));
        if (visible("imageDialog")) close("imageDialog");
        m_window->requestActivate();
        check(QTest::qWaitForWindowActive(m_window,1000), "window_active_after_image_picker");
        focus("editorSource");
        const QString code = QStringLiteral("std::expected<int, Error>\nsecond line with \\::literal");
        editor->setProperty("text",code + QStringLiteral("\n\n---\n\nBack"));
        QMetaObject::invokeMethod(editor,"select",Q_ARG(int,0),Q_ARG(int,static_cast<int>(code.size())));
        key(Qt::Key_C,Qt::ControlModifier | Qt::ShiftModifier);
        const QString encoded = editor->property("text").toString();
        check(encoded.contains(QStringLiteral("std\\::expected")) && encoded.contains(QString(3,QLatin1Char('\\'))+QStringLiteral("::literal"))
              && encoded.contains(QLatin1Char('\n')), "editor_cloze_escapes_cpp_and_preserves_multiline");
        const qsizetype delimiter = encoded.indexOf(QStringLiteral("\n\n---\n\n"));
        if (delimiter < 0) throw std::runtime_error("Editor lost the side separator.");
        editor->setProperty("text",encoded.left(delimiter) + QStringLiteral("\n\n") + image + QStringLiteral("\n\n---\n\nExtra explanation.\n\n") + image);
        const int before = m_app.cards().size();
        key(Qt::Key_S,Qt::ControlModifier);
        check(!visible("cardDialog"), "save_card_keyboard_closes_modal");
        check(waitUntil([this,before] { return !m_app.busy() && m_app.cards().size() == before + 1; }), "editor_card_persisted");
        if (m_app.cards().isEmpty()) throw std::runtime_error("Editor card was not persisted.");
        const QVariantMap card = m_app.cards().first().toMap();
        check(card.value("kind").toString() == QStringLiteral("cloze") && card.value("front").toString().contains(image)
              && card.value("back").toString().contains(image), "cloze_inferred_and_images_on_both_sides");
        const QString editableId = card.value("id").toString();
        call("openCardEditor",{editableId});
        check(visible("cardDialog"), "inversion_editor_opens_existing_card");
        require("cardKind")->setProperty("currentIndex",0);
        QTest::qWait(100);
        require("editorSource")->setProperty("text",QStringLiteral("Inversion source question\n\n---\n\nInversion source answer"));
        click("createInverted");
        require("editorTabs")->setProperty("currentIndex",1);
        QTest::qWait(100);
        const QString invertedPrefix = QStringLiteral("Ask the question that this answers based on deck context: ");
        const QString invertedPreview = require("invertedFrontPreview")->property("markdown").toString();
        check(visible("invertedFrontPreview") && invertedPreview.startsWith(invertedPrefix + QStringLiteral("\n\n"))
              && invertedPreview.endsWith(QStringLiteral("Inversion source answer")), "inversion_preview_uses_exact_prefix",
              QStringLiteral("kind=%1 inverted=%2 combo=%3 preview=%4 source=%5")
                  .arg(require("cardKind")->property("currentValue").toString())
                  .arg(require("createInverted")->property("checked").toBool())
                  .arg(require("cardKind")->property("currentIndex").toInt())
                  .arg(invertedPreview,require("editorSource")->property("text").toString()));
        key(Qt::Key_S,Qt::ControlModifier);
        check(waitUntil([this,editableId] {
            if (m_app.busy()) return false;
            for (const QVariant &value : m_app.cards()) {
                const QVariantMap candidate = value.toMap();
                if (candidate.value("id").toString() == editableId)
                    return candidate.value("kind").toString() == QStringLiteral("reverse");
            }
            return false;
        }), "inversion_editor_saves_reverse_card",
        QStringLiteral("id=%1 kind=%2 dialog=%3").arg(editableId).arg(card.value("kind").toString()).arg(visible("cardDialog")));
        call("openCardEditor",{editableId});
        check(require("createInverted")->property("checked").toBool(), "inversion_editor_restores_checked_state");
        click("createInverted");
        key(Qt::Key_S,Qt::ControlModifier);
        check(waitUntil([this,editableId] {
            if (m_app.busy()) return false;
            for (const QVariant &value : m_app.cards())
                if (value.toMap().value("id").toString() == editableId)
                    return value.toMap().value("kind").toString() == QStringLiteral("basic");
            return false;
        }), "inversion_editor_uncheck_restores_basic_schedule");
        call("openCardEditor",{editableId});
        click("createInverted");
        key(Qt::Key_S,Qt::ControlModifier);
        check(waitUntil([this,editableId] {
            if (m_app.busy()) return false;
            for (const QVariant &value : m_app.cards())
                if (value.toMap().value("id").toString() == editableId)
                    return value.toMap().value("kind").toString() == QStringLiteral("reverse");
            return false;
        }), "inversion_editor_recheck_preserves_reverse_schedule");
    }
    void seedCards(const QString &image)
    {
        QString question;
        QString answer;
        for (int paragraph = 0; paragraph < 60; ++paragraph) {
            question += QStringLiteral("Recall point %1, then explain its relationship to the next point.\n\n").arg(paragraph + 1);
            answer += QStringLiteral("Explanation %1 gives a concrete example with enough detail to require scrolling.\n\n").arg(paragraph + 1);
        }
        question += QStringLiteral("```cpp\nconst auto value = std::expected<int, int>{42};\n```\n\n$e^{i\\pi}+1=0$\n\n") + image;
        answer += QStringLiteral("```python\nvalues = tuple(range(4))\n```\n\n$\\frac{1}{2}$\n\n") + image;
        for (int index = 0; index < 20; ++index) m_app.saveCard({},m_deckA,"basic",QStringLiteral("Fixture %1\n\n").arg(index)+question,answer,"verification",3);
        m_app.saveCard({},m_deckB,"basic","Second-deck question","Second-deck answer","verification",1);
        m_app.saveCard({},m_deckChild,"basic","Nested question","Nested answer","verification",1);
        if (!check(waitUntil([this] { return !m_app.busy() && m_app.cards().size() == 23; },10000), "fixture_notes_persisted"))
            throw std::runtime_error("Could not persist fixture notes.");
        for (const QVariant &value : m_app.cards()) {
            const QVariantMap card = value.toMap();
            if (card.value("deckId").toString() == m_deckA && card.value("kind").toString() == "basic") { m_longCard = card.value("id").toString();break; }
        }
        assert(!m_longCard.isEmpty());
    }
    void dueSearchPreservesContext()
    {
        resize(860,600);
        key(Qt::Key_2,Qt::AltModifier);
        call("setDeck",{m_deckA,true});
        QObject *const library=require("libraryPage");
        QObject *const search=m_engine.rootContext()->contextProperty("librarySearch").value<QObject *>();
        if (!search) throw std::runtime_error("The library search context is unavailable.");
        const auto list=[](QObject *object,const char *name) {
            const QVariant raw=object->property(name);
            return raw.metaType()==QMetaType::fromType<QJSValue>() ? raw.value<QJSValue>().toVariant().toList() : raw.toList();
        };
        QString target,originalDue;
        for (const QVariant &value:m_app.cards()) {
            const QVariantMap card=value.toMap();
            if (card.value("deckId").toString()!=m_deckA||card.value("id").toString()==m_longCard
                ||!card.value("front").toString().startsWith(QStringLiteral("Fixture "))) continue;
            target=card.value("id").toString();
            originalDue=card.value("variants").toList().first().toMap().value("due").toString();
            break;
        }
        if (!check(!target.isEmpty(),"due_search_fixture_available")) return;
        const bool semantic=search->property("semanticEnabled").toBool();
        const QString originalFilter=library->property("filterState").toString();
        const QString originalQuery=m_window->property("cardSearchText").toString();
        search->setProperty("semanticEnabled",false);
        library->setProperty("filterState",QStringLiteral("due"));
        m_window->setProperty("cardSearchText",QStringLiteral("Fixture"));
        check(waitUntil([search] {return !search->property("busy").toBool();}),"due_search_initial_query_settled");

        const QString connection=QStringLiteral("gui-due-search-")+betterflash::model::uuid();
        {
            QSqlDatabase database=QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),connection);
            database.setDatabaseName(m_app.storagePath());
            if (!database.open()) throw std::runtime_error(("Cannot open due-search fixture database: "+database.lastError().text()).toStdString());
            const auto setDue=[&](const QString &due) {
                QSqlQuery query(database);
                if (!query.prepare(QStringLiteral("UPDATE review_variants SET due=? WHERE card_id=?"))) return false;
                query.addBindValue(due);query.addBindValue(target);
                return query.exec()&&query.numRowsAffected()>0;
            };
            const QString future=QDateTime::currentDateTimeUtc().addSecs(8).toString(Qt::ISODateWithMs);
            if (!check(setDue(future),"due_search_future_fixture_saved",m_app.storagePath())) return;
            m_app.refresh();
            check(waitUntil([this,search] {return !m_app.busy()&&!search->property("busy").toBool();}),"due_search_filtered_query_settled");
            const auto containsTarget=[&] {
                for (const QVariant &value:list(library,"visibleCards"))
                    if (value.toMap().value("id").toString()==target) return true;
                return false;
            };
            check(!containsTarget(),"due_search_excludes_future_card");
            const QVariantList visible=list(library,"visibleCards");
            const int lastPage=list(library,"cardPages").size()-1;
            check(lastPage>0&&!visible.isEmpty(),"due_search_has_navigation_context");
            if (!visible.isEmpty()) library->setProperty("searchPageAnchorId",visible.first().toMap().value("id"));
            check(QMetaObject::invokeMethod(library,"changePage",Q_ARG(QVariant,QVariant(lastPage))),"due_search_navigates_result_page");
            QTest::qWait(80);
            const int page=library->property("pageIndex").toInt();
            const QString anchor=library->property("pageAnchorId").toString();
            const QString selected=m_window->property("selectedCardId").toString();
            QSignalSpy resets(search,SIGNAL(resultsAboutToChange()));
            check(resets.isValid(),"due_search_context_signal_available");
            check(waitUntil([&] {return !search->property("busy").toBool()&&containsTarget();},12000),"due_search_discovers_newly_due_card");
            bool anchorStillVisible=false;
            for (const QVariant &entry:list(library,"pageCards"))
                if (entry.toMap().value("id").toString()==anchor) anchorStillVisible=true;
            check(resets.isEmpty()&&library->property("pageIndex").toInt()==page
                  &&anchorStillVisible
                  &&m_window->property("selectedCardId").toString()==selected,"due_search_retains_result_context",
                  QStringLiteral("searchResetSignals=%1 page=%2->%3 anchor=%4->%5 selected=%6->%7")
                      .arg(resets.size()).arg(page).arg(library->property("pageIndex").toInt())
                      .arg(anchor,library->property("pageAnchorId").toString(),selected,m_window->property("selectedCardId").toString()));
            library->setProperty("filterState",QString());
            check(setDue(QDateTime::currentDateTimeUtc().addSecs(5).toString(Qt::ISODateWithMs)),"due_search_metadata_fixture_saved");
            m_app.refresh();
            check(waitUntil([this,search] {return !m_app.busy()&&!search->property("busy").toBool();}),"due_search_unfiltered_query_settled");
            const QVariantList allVisible=list(library,"visibleCards");
            if (!allVisible.isEmpty()) library->setProperty("searchPageAnchorId",allVisible.first().toMap().value("id"));
            check(QMetaObject::invokeMethod(library,"changePage",Q_ARG(QVariant,QVariant(list(library,"cardPages").size()-1))),"due_search_navigates_metadata_page");
            QTest::qWait(80);
            const int metadataPage=library->property("pageIndex").toInt();
            const QString metadataAnchor=library->property("pageAnchorId").toString();
            QSignalSpy metadataResets(search,SIGNAL(resultsAboutToChange()));
            check(waitUntil([&] {
                for (const QVariant &entry:search->property("results").toList()) {
                    const QVariantMap result=entry.toMap();
                    if (result.value("id").toString()==target) return result.value("dueCount").toInt()>0;
                }
                return false;
            },10000),"due_search_updates_cached_result_metadata");
            QTest::qWait(80);
            check(metadataResets.isEmpty()&&library->property("pageIndex").toInt()==metadataPage
                  &&library->property("pageAnchorId").toString()==metadataAnchor,"due_search_metadata_preserves_navigation",
                  QStringLiteral("searchResetSignals=%1 page=%2->%3 anchor=%4->%5").arg(metadataResets.size())
                      .arg(metadataPage).arg(library->property("pageIndex").toInt()).arg(metadataAnchor,library->property("pageAnchorId").toString()));
            check(setDue(originalDue),"due_search_fixture_restored",m_app.storagePath());
            database.close();
        }
        QSqlDatabase::removeDatabase(connection);
        m_window->setProperty("cardSearchText",originalQuery);
        library->setProperty("filterState",originalFilter);
        search->setProperty("semanticEnabled",semantic);
        m_app.refresh();
        check(waitUntil([this] {return !m_app.busy();}),"due_search_cleanup_finished");
    }
    void reviewIdleBrowser()
    {
        m_app.stopReview();
        check(waitUntil([this] {return !m_app.busy()&&!m_app.reviewing();}),"review_idle_ready");
        call("runAction",{QStringLiteral("reviewPage")});
        check(visible("reviewIdle")&&!visible("reviewWorkspace"),"idle_review_overview_visible");
        const QVariantList cards=m_app.cards(),history=m_app.history();
        call("setDeck",{m_deckChild,true});
        check(m_app.selectedDeckId()==m_deckChild,"overview_selects_child_deck");
        call("setDeck",{m_deckA,true});
        check(m_app.selectedDeckId()==m_deckA&&m_app.cards()==cards&&m_app.history()==history,
              "overview_navigation_preserves_collection");
        QObject *const ai=m_engine.rootContext()->contextProperty("ai").value<QObject *>();
        check(ai&&!ai->property("busy").toBool(),"overview_selection_does_not_start_inference");
        key(Qt::Key_2,Qt::AltModifier);
    }
    QQuickItem *paneToggle(bool rightSide,bool collapsed) const
    {
        std::function<QQuickItem *(QQuickItem *)> find=[&](QQuickItem *parent)->QQuickItem * {
            if (parent->isVisible()&&parent->property("rightSide").isValid()&&parent->property("collapsed").isValid()
                &&parent->property("rightSide").toBool()==rightSide&&parent->property("collapsed").toBool()==collapsed) return parent;
            for (QQuickItem *const child:parent->childItems()) if (QQuickItem *const found=find(child)) return found;
            return nullptr;
        };
        QQuickItem *const result=find(item("libraryPage"));
        if (!result) throw std::runtime_error("The library pane toggle is unavailable.");
        return result;
    }
    void togglePane(bool rightSide,bool collapsed)
    {
        QQuickItem *const control=paneToggle(rightSide,collapsed);
        const QPoint position=control->mapRectToScene(QRectF(0,0,control->width(),control->height())).center().toPoint();
        QTest::mouseClick(m_window,Qt::LeftButton,Qt::NoModifier,position);
        QTest::qWait(100);
    }
    void desktop(const QString &theme)
    {
        resize(1320,860);
        key(Qt::Key_2,Qt::AltModifier);
        call("setDeck",{m_deckA}); call("pickCard",{m_longCard});
        check(m_window->property("page").toInt()==1&&visible("libraryCardList"),"library_navigation_"+theme);
        wheel("libraryCardList",theme+"_desktop");
        const QVariantList cards=m_app.cards(),history=m_app.history();
        const qreal scrollBefore=require("libraryCardList")->property("contentY").toDouble();
        call("openSelectedDetail");
        check(visible("cardDetailDialog")&&require("cardDetailDialog")->property("cardId").toString()==m_longCard,
              "selected_card_opens_detail_"+theme);
        wheel("cardDetailDialog",theme+"_detail");
        close("cardDetailDialog");
        check(m_window->property("selectedCardId").toString()==m_longCard&&m_app.cards()==cards&&m_app.history()==history,
              "detail_close_preserves_selection_and_records_"+theme);
        check(std::abs(require("libraryCardList")->property("contentY").toDouble()-scrollBefore)<1.1,
              "detail_close_preserves_library_scroll_"+theme);
        togglePane(true,false);
        check(!visible("libraryCardPane"),"card_pane_collapses_"+theme);
        togglePane(true,true);
        check(visible("libraryCardPane")&&m_window->property("selectedCardId").toString()==m_longCard,
              "card_pane_reopens_with_selection_"+theme);
        togglePane(false,false);
        check(!require("libraryPage")->property("effectiveLeftOpen").toBool()&&visible("libraryCardPane"),
              "deck_pane_collapses_without_hiding_cards_"+theme);
        togglePane(false,true);
        key(Qt::Key_3,Qt::AltModifier); key(Qt::Key_2,Qt::AltModifier);
        check(m_window->property("selectedCardId").toString()==m_longCard,"navigation_preserves_selected_card_"+theme);
        click("navToggle");
        check(m_window->property("navCollapsed").toBool(),"navigation_collapses_"+theme);
        click("navReview"); click("navLibrary");
        check(m_window->property("page").toInt()==1&&m_window->property("selectedCardId").toString()==m_longCard,
              "collapsed_navigation_preserves_context_"+theme);
        click("navToggle");
        check(!m_window->property("navCollapsed").toBool(),"navigation_reopens_"+theme);
        layouts(theme+"_desktop"); screenshot(theme+"-desktop-library");
        keyboardNavigation(theme); review(theme);
        key(Qt::Key_4,Qt::AltModifier);
        require("settingsPage")->setProperty("section",1);
        QTest::qWait(100); wheel("settingsScroll",theme+"_desktop_keyboard");
        screenshot(theme+"-desktop-keyboard"); rebind(theme);
    }
    void rebind(const QString &theme)
    {
        call("openRebind",{QStringLiteral("commandPalette")});
        check(visible("rebindDialog"), "shortcut_rebinding_modal_" + theme);
        key(Qt::Key_K,Qt::ControlModifier | Qt::ShiftModifier);
        check(!m_window->property("rebindSequence").toString().isEmpty(), "shortcut_capture_uses_key_combination_" + theme);
        click("saveBinding");
        check(!visible("rebindDialog"), "shortcut_binding_saved_" + theme);
        key(Qt::Key_K,Qt::ControlModifier | Qt::ShiftModifier);
        check(visible("commandPalette"), "custom_shortcut_activates_immediately_" + theme);
        close("commandPalette");
        call("openRebind",{QStringLiteral("commandPalette")});
        key(Qt::Key_N,Qt::ControlModifier);
        click("saveBinding");
        check(visible("rebindDialog") && m_window->property("rebindError").toString().contains(QStringLiteral("SHORTCUT_CONFLICT")),
              "shortcut_conflict_keeps_modal_open_" + theme);
        close("rebindDialog");
        QObject *const shortcuts = m_engine.rootContext()->contextProperty("shortcuts").value<QObject *>();
        if (!shortcuts) throw std::runtime_error("Shortcut manager was not found.");
        check(QMetaObject::invokeMethod(shortcuts,"reset"), "shortcut_reset_available_" + theme);
        key(Qt::Key_K,Qt::ControlModifier);
        check(visible("commandPalette"), "shortcut_reset_restores_defaults_" + theme);
        close("commandPalette");
    }
    void keyboardNavigation(const QString &theme)
    {
        key(Qt::Key_K,Qt::ControlModifier);
        check(visible("commandPalette"), "command_palette_keyboard_" + theme);
        require("commandSearch")->setProperty("text",QStringLiteral("Fixture B"));
        key(Qt::Key_Down);
        key(Qt::Key_Up);
        key(Qt::Key_Return);
        check(waitUntil([this] { return m_app.selectedDeckId() == m_deckB && !visible("commandPalette"); }),
              "command_palette_switches_deck_" + theme);
        key(Qt::Key_L,Qt::ControlModifier);
        check(visible("deckSearchField")&&require("deckSearchField")->property("activeFocus").toBool(),
              "deck_search_keyboard_focuses_field_"+theme);
        QObject *const library=require("libraryPage");
        library->setProperty("deckSearchText",QStringLiteral("Fixture A"));
        QTest::qWait(80); key(Qt::Key_Return);
        const QVariant raw=library->property("treeRows");
        const QVariantList rows=raw.metaType()==QMetaType::fromType<QJSValue>()?raw.value<QJSValue>().toVariant().toList():raw.toList();
        int target=-1;
        for (qsizetype index=0;index<rows.size();++index)
            if (rows[index].toMap().value("id").toString()==m_deckA) target=static_cast<int>(index);
        check(target>=0,"deck_search_finds_matching_record_"+theme);
        require("libraryDeckTree")->setProperty("currentIndex",target);
        focus("libraryDeckTree"); key(Qt::Key_Return);
        check(waitUntil([this] {return m_app.selectedDeckId()==m_deckA;}),"deck_search_selects_deck_"+theme);
        library->setProperty("deckSearchText",QString());
        call("pickCard",{m_longCard});
        key(Qt::Key_E,Qt::ControlModifier);
        check(visible("cardDialog") && m_window->property("editingCardId").toString() == m_longCard,
              "edit_selected_card_keyboard_" + theme);
        require("editorTabs")->setProperty("currentIndex",1);
        QTest::qWait(120);
        wheel("editorPreviewScroll",theme + "_desktop_preview");
        screenshot(theme + "-desktop-editor");
        close("cardDialog");
    }
    void ensureLongCurrent()
    {
        for (int index = 0; index < 8 && (m_app.currentCard().value("kind").toString() != "basic"
                                           || m_app.currentCard().value("pointCount").toInt() < 3); ++index) {
            const QString before = currentVariant();
            m_app.deferCard();
            waitUntil([this,before] { return !m_app.busy() && currentVariant() != before; });
        }
    }
    void checkReviewQueuePreview(const QString &suffix)
    {
        QObject *const preview=require("reviewQueuePreview");
        const QVariantList pending=m_app.pendingCards(),history=m_app.history(),cards=m_app.cards();
        const QString selected=currentVariant();
        check(preview->property("count").toInt()==m_app.reviewCards().size()
              &&preview->property("activeVariantId").toString()==selected,"timeline_matches_review_session_"+suffix);
        const qreal minimum=preview->property("minimumScroll").toDouble(),maximum=preview->property("maximumScroll").toDouble();
        focus("reviewQueueScrollBar"); key(Qt::Key_End);
        check(std::abs(preview->property("contentX").toDouble()-maximum)<2.0,"timeline_keyboard_reaches_end_"+suffix);
        key(Qt::Key_Home);
        check(std::abs(preview->property("contentX").toDouble()-minimum)<2.0,"timeline_keyboard_reaches_start_"+suffix);
        check(currentVariant()==selected&&m_app.pendingCards()==pending&&m_app.history()==history&&m_app.cards()==cards,
              "timeline_scroll_preserves_review_records_"+suffix);
        click("reviewQueueCollapse");
        check(!visible("reviewQueueBar")&&visible("reviewQueueReopen"),"timeline_collapses_"+suffix);
        click("reviewQueueReopen");
        check(visible("reviewQueueBar")&&currentVariant()==selected,"timeline_reopens_without_changing_card_"+suffix);
        focus("reviewQuestion");
    }
    void reviewLifecycle()
    {
        m_app.stopReview(); check(waitUntil([this] {return !m_app.busy()&&!m_app.reviewing();}),"short_review_setup");
        call("runAction",{QStringLiteral("reviewPage")}); m_app.startReview(m_deckB);
        if (!check(waitUntil([this] {return !m_app.busy()&&m_app.reviewing();}),"short_review_starts")) return;
        const int count=m_app.history().size();
        focus("reviewQuestion"); click("revealAnswer"); click("grade3");
        check(waitUntil([this,count] {return !m_app.busy()&&m_app.queueCount()==0&&m_app.history().size()==count+1;}),
              "short_review_commits_last_grade");
        m_app.stopReview();
        check(waitUntil([this] {return !m_app.busy()&&!m_app.reviewing()&&visible("reviewIdle");}),"review_end_restores_overview");
    }
    void review(const QString &theme)
    {
        m_app.stopReview(); check(waitUntil([this] {return !m_app.busy()&&!m_app.reviewing();}),"review_setup_"+theme);
        key(Qt::Key_1,Qt::AltModifier); m_app.startReview(m_deckA);
        if (!check(waitUntil([this] {return !m_app.busy()&&m_app.reviewing()&&!m_app.currentCard().isEmpty();}),"review_started_"+theme))
            throw std::runtime_error("Review did not start.");
        check(visible("reviewWorkspace")&&visible("reviewPause")&&!visible("reviewIdle"),"active_review_workspace_visible_"+theme);
        ensureLongCurrent(); checkReviewQueuePreview(theme+"_initial");
        const QString selected=currentVariant(); const QVariantList pending=m_app.pendingCards();
        click("reviewPause"); check(waitUntil([this] {return m_app.paused();}),"review_pause_control_"+theme);
        const double elapsed=m_app.responseSeconds(); QTest::qWait(100);
        check(std::abs(m_app.responseSeconds()-elapsed)<0.03&&currentVariant()==selected&&m_app.pendingCards()==pending,
              "pause_preserves_timing_and_queue_"+theme);
        click("reviewPause"); check(waitUntil([this] {return !m_app.paused();}),"review_resume_control_"+theme);
        focus("reviewQuestion");
        check(!m_app.answerRevealed(),"review_answer_starts_hidden_"+theme);
        key(Qt::Key_Space);
        check(m_app.answerRevealed()&&visible("reviewAnswer"),"review_keyboard_reveals_answer_"+theme);
        wheel("reviewScroll",theme+"_desktop_review");
        const int history=m_app.history().size(),queue=m_app.queueCount();
        check(m_app.currentCard().value("pointCount").toInt()==3,"partial_grade_three_point_fixture_"+theme);
        focus("reviewQuestion"); key(Qt::Key_2);
        check(visible("partialDialog"),"partial_grade_keyboard_opens_dialog_"+theme);
        require("recalledPoints")->setProperty("value",1);
        click("gradePartialConfirm");
        check(waitUntil([this,history,queue] {return !m_app.busy()&&m_app.history().size()==history+1&&m_app.queueCount()==queue-1&&!visible("partialDialog");}),
              "partial_grade_commits_and_advances_"+theme);
        bool partialRecorded=false;
        for (const QVariant &value:m_app.history()) {
            const QVariantMap review=value.toMap();
            if (review.value("variantId").toString()==selected)
                partialRecorded=review.value("grade").toInt()==1&&std::abs(review.value("recallFraction").toDouble()-1.0/3.0)<0.001;
        }
        check(partialRecorded,"partial_grade_records_one_third_recall_"+theme);
        const QString cursor=currentVariant();
        const QVariantList pendingAfterGrade=m_app.pendingCards(),historyAfterGrade=m_app.history(),cardsAfterGrade=m_app.cards();
        const auto selectTimeline=[this](const QString &variant) {
            const QVariantList timeline=m_app.reviewCards();
            int index=-1;
            for (qsizetype position=0;position<timeline.size();++position)
                if (timeline[position].toMap().value("variantId").toString()==variant) index=static_cast<int>(position);
            if (index<0) return false;
            require("reviewQueuePreview")->setProperty("currentIndex",index);
            focus("reviewQueuePreview"); key(Qt::Key_Return);
            return waitUntil([this,variant] {return !m_app.busy()&&currentVariant()==variant;});
        };
        check(selectTimeline(selected)&&m_app.reviewingCompletedCard()&&m_app.answerRevealed()
              &&m_app.pendingCards()==pendingAfterGrade&&m_app.history()==historyAfterGrade&&m_app.cards()==cardsAfterGrade,
              "timeline_inspects_completed_card_without_mutation_"+theme);
        check(selectTimeline(cursor)&&!m_app.reviewingCompletedCard()&&!m_app.answerRevealed()
              &&m_app.pendingCards()==pendingAfterGrade&&m_app.history()==historyAfterGrade&&m_app.cards()==cardsAfterGrade,
              "timeline_returns_to_pending_card_without_mutation_"+theme);
        const QString next=currentVariant(); const QVariantList recorded=m_app.history(),schedules=m_app.cards();
        focus("deferCard"); key(Qt::Key_Space);
        check(waitUntil([this,next] {return !m_app.busy()&&currentVariant()!=next;})
              &&m_app.history()==recorded&&m_app.cards()==schedules,"defer_keyboard_does_not_grade_"+theme);
        focus("postponeCard"); key(Qt::Key_Space);
        check(visible("postponeDialog"),"postpone_keyboard_opens_dialog_"+theme);
        const QString postponed=currentVariant(); const int postponedCount=m_app.queueCount();
        click("postponeConfirm");
        check(waitUntil([this,postponedCount] {return !m_app.busy()&&m_app.queueCount()==postponedCount-1&&!visible("postponeDialog");})
              &&currentVariant()!=postponed&&m_app.history()==recorded,"postpone_commits_without_grade_"+theme);
        click("reviewSummary");
        QObject *const ai=m_engine.rootContext()->contextProperty("ai").value<QObject *>();
        check(visible("aiDialog")&&ai&&!ai->property("busy").toBool(),"summary_requires_explicit_generation_"+theme);
        close("aiDialog");
        const QString beforeNavigation=currentVariant(); const QVariantList navigationQueue=m_app.pendingCards();
        key(Qt::Key_2,Qt::AltModifier);
        check(waitUntil([this] {return m_app.paused();})&&currentVariant()==beforeNavigation&&m_app.pendingCards()==navigationQueue,
              "review_navigation_pauses_and_retains_session_"+theme);
        key(Qt::Key_1,Qt::AltModifier); click("reviewPause");
        check(waitUntil([this] {return !m_app.paused()&&visible("reviewWorkspace");}),"review_navigation_resumes_workspace_"+theme);
        layouts(theme+"_desktop_review");
    }
    void phone(const QString &theme)
    {
        resize(390,780);
        key(Qt::Key_2,Qt::AltModifier);
        call("setDeck",{m_deckA});
        call("pickCard",{m_longCard});
        check(visible("libraryCardList"),"phone_library_cards_available_"+theme);
        wheel("libraryCardList",theme+"_phone_library"); screenshot(theme+"-phone-library");
        call("openSelectedDetail"); check(visible("cardDetailDialog"),"phone_card_detail_opens_"+theme);
        close("cardDetailDialog");
        key(Qt::Key_E,Qt::ControlModifier);
        check(visible("cardDialog"), "phone_editor_modal_" + theme);
        const qreal modalWidth = require("cardDialog")->property("width").toDouble();
        const qreal modalHeight = require("cardDialog")->property("height").toDouble();
        check(modalWidth <= m_window->width() && modalHeight <= m_window->height(), "phone_editor_fits_window_" + theme);
        require("editorTabs")->setProperty("currentIndex",1);
        QTest::qWait(100);
        wheel("editorPreviewScroll",theme + "_phone_preview");
        screenshot(theme + "-phone-editor");
        close("cardDialog");
        key(Qt::Key_1,Qt::AltModifier);
        if (m_app.paused()) click("reviewPause");
        check(waitUntil([this] {return m_app.reviewing()&&!m_app.paused()&&visible("reviewWorkspace");}),
              "phone_review_resumes_after_navigation_"+theme);
        ensureLongCurrent(); if (!m_app.answerRevealed()) {focus("reviewQuestion"); key(Qt::Key_Space);}
        wheel("reviewScroll",theme+"_phone_review"); checkReviewQueuePreview(theme+"_phone");
        layouts(theme+"_phone_review"); screenshot(theme+"-phone-review");
        key(Qt::Key_4,Qt::AltModifier);
        require("settingsPage")->setProperty("section",1);
        QTest::qWait(100);
        wheel("settingsScroll",theme + "_phone_keyboard");
        layouts(theme + "_phone_keyboard");
        screenshot(theme + "-phone-keyboard");
        dialogsFit(theme);
    }
    void compactDesktop(const QString &theme)
    {
        resize(860,600); key(Qt::Key_2,Qt::AltModifier); call("setDeck",{m_deckA}); call("pickCard",{m_longCard});
        check(visible("libraryCardPane"),"compact_library_cards_available_"+theme);
        togglePane(true,false); check(!visible("libraryCardPane"),"compact_card_pane_collapses_"+theme);
        togglePane(true,true);
        check(visible("libraryCardPane")&&m_window->property("selectedCardId").toString()==m_longCard,
              "compact_card_pane_restores_selection_"+theme);
        layouts(theme+"_compact_desktop"); screenshot(theme+"-compact-desktop-library");
    }
    void dialogsFit(const QString &theme)
    {
        const auto checkDialog = [this,&theme](const QString &name) {
            QObject *const dialog = require(name);
            const qreal width = dialog->property("width").toDouble();
            const qreal height = dialog->property("height").toDouble();
            check(visible(name) && width > 0 && width <= m_window->width() && height > 0 && height <= m_window->height(),
                  "phone_modal_fits_" + theme + "_" + name,
                  QStringLiteral("dialog=%1x%2 window=%3x%4").arg(width).arg(height).arg(m_window->width()).arg(m_window->height()));
            close(name);
        };
        call("openDeckEditor",{m_deckA});
        checkDialog("deckDialog");
        call("openRebind",{QStringLiteral("newCard")});
        checkDialog("rebindDialog");
        call("openCredentials",{QStringLiteral("voice")});
        require("credentialValue")->setProperty("text",QStringLiteral("isolated-fixture-secret"));
        checkDialog("credentialDialog");
        check(require("credentialValue")->property("text").toString().isEmpty(), "cancel_clears_credential_field_" + theme);
        for (const QString &action:{QStringLiteral("openVoiceConfig"),QStringLiteral("openAiConfig"),QStringLiteral("openSyncConfig")}) {
            call(action); check(m_window->property("page").toInt()==3&&visible("settingsPage"),"settings_action_opens_page_"+theme+"_"+action);
        }
        call("openPartial");
        checkDialog("partialDialog");
        call("openPostpone");
        checkDialog("postponeDialog");
        call("openSummary");
        checkDialog("aiDialog");
        call("openAllHistory");
        check(m_window->property("page").toInt()==2,"history_action_opens_history_page_"+theme);
        call("confirmDelete",{QStringLiteral("card"),m_longCard,QStringLiteral("Fixture")});
        checkDialog("deleteDialog");
        call("showError",{QStringLiteral("GUI_FIXTURE"),QStringLiteral("A verification error."),QStringLiteral("Isolated fixture details.")});
        checkDialog("errorDialog");
    }
    void atomicize()
    {
        QObject *const ai = m_engine.rootContext()->contextProperty("ai").value<QObject *>();
        QObject *const atomic = m_engine.rootContext()->contextProperty("atomic").value<QObject *>();
        if (!ai || !atomic) throw std::runtime_error("Atomicize contexts were not found.");
        LoopbackProvider provider;
        const QString originalEndpoint = ai->property("endpoint").toString();
        const QString originalProvider = ai->property("provider").toString();
        ai->setProperty("provider",QStringLiteral("remote"));
        ai->setProperty("endpoint",provider.endpoint());
        QObject *const binding=ai->property("apiKey").value<QObject *>();
        if (!binding||!waitUntil([binding] {return !binding->property("ownKeyId").toString().isEmpty();}))
            throw std::runtime_error("The isolated provider credential binding is unavailable.");
        check(QMetaObject::invokeMethod(ai,"setApiKey",Q_ARG(QString,QStringLiteral("gui-fixture-only")))
              &&ai->property("hasApiKey").toBool(),"atomicize_uses_isolated_mock_credential");
        const int before = m_app.cards().size();
        m_app.saveCard({},m_deckA,"basic","Atomicize fixture source","Independent concept A.\n\nIndependent concept B.","verification",2);
        if (!check(waitUntil([this,before] { return !m_app.busy() && m_app.cards().size() == before+1; }), "atomicize_fixture_saved"))
            throw std::runtime_error("Could not persist the Atomicize fixture.");
        QString sourceId;
        for (const QVariant &value : m_app.cards()) {
            const QVariantMap card = value.toMap();
            if (card.value("front").toString() == QStringLiteral("Atomicize fixture source")) sourceId = card.value("id").toString();
        }
        assert(!sourceId.isEmpty());
        const auto sourceExists = [this,&sourceId] {
            for (const QVariant &value : m_app.cards()) if (value.toMap().value("id").toString() == sourceId) return true;
            return false;
        };
        call("setDeck",{m_deckA});
        call("pickCard",{sourceId});
        provider.proposal(QJsonObject{{"decision","keep"},{"reason","The related steps need their shared context."},{"cards",QJsonArray{}}});
        call("openAtomicize",{sourceId});
        check(waitUntil([atomic] { return !atomic->property("busy").toBool() && atomic->property("decision").toString() == QStringLiteral("keep"); }),
              "atomicize_keep_proposal_received");
        check(visible("atomicDialog") && !item("atomicApply")->isVisible() && sourceExists(), "atomicize_keep_never_replaces_source");
        screenshot("atomicize-keep");
        close("atomicDialog");
        check(sourceExists() && m_app.cards().size() == before+1, "atomicize_keep_close_preserves_source");
        const QJsonArray splitCards{
            QJsonObject{{"front","What is concept A?"},{"back","Concept A."},{"pointCount",1}},
            QJsonObject{{"front","What is concept B?"},{"back","Concept B."},{"pointCount",1}}
        };
        provider.proposal(QJsonObject{{"decision","split"},{"reason","Each concept has its own recall target."},{"cards",splitCards}});
        call("openAtomicize",{sourceId});
        check(waitUntil([this,atomic] { return !atomic->property("busy").toBool() && atomic->property("decision").toString() == QStringLiteral("split")
                     && require("atomicTabs")->property("count").toInt() == 3; }), "atomicize_split_proposal_received");
        const QString edited = QStringLiteral("An edited focused question?\n\n---\n\nAn edited focused answer.");
        require("atomicDraftSource")->setProperty("text",edited);
        require("atomicPoints")->setProperty("value",2);
        require("atomicTabs")->setProperty("currentIndex",2);
        require("atomicTabs")->setProperty("currentIndex",1);
        check(require("atomicDraftSource")->property("text").toString() == edited && require("atomicPoints")->property("value").toInt() == 2,
              "atomicize_edits_survive_tab_switching");
        resize(390,780);
        check(require("atomicDialog")->property("width").toDouble() <= 390 && require("atomicDialog")->property("height").toDouble() <= 780,
              "atomicize_phone_modal_fits");
        click("atomicPreviewToggle");
        wheel("atomicPreviewScroll","atomicize_phone");
        screenshot("atomicize-phone-preview");
        close("atomicDialog");
        check(sourceExists() && m_app.cards().size() == before+1, "atomicize_cancel_discards_proposal_preserves_source");
        resize(1320,860);
        call("openAtomicize",{sourceId});
        check(waitUntil([atomic] { return !atomic->property("busy").toBool() && atomic->property("decision").toString() == QStringLiteral("split"); }),
              "atomicize_second_split_received");
        require("atomicDraftSource")->setProperty("text",QStringLiteral("\n\n---\n\nAn answer"));
        click("atomicApply");
        check(visible("atomicDialog") && !atomic->property("applying").toBool() && sourceExists()
              && !require("atomicDialog")->property("localError").toString().isEmpty(), "atomicize_invalid_draft_stays_open");
        require("atomicDraftSource")->setProperty("text",edited);
        require("atomicPoints")->setProperty("value",2);
        const int history = m_app.history().size();
        QSignalSpy applied(atomic,SIGNAL(applied()));
        bool protectedDuringApply = false;
        bool observedApply = false;
        Atomicizer *const native = qobject_cast<Atomicizer *>(atomic);
        if (!native) throw std::runtime_error("The Atomicize native context has the wrong type.");
        const QMetaObject::Connection watch = QObject::connect(native,&Atomicizer::changed,m_window,[this,native,&protectedDuringApply,&observedApply] {
            if (!native->applying()) return;
            observedApply = true;
            protectedDuringApply = visible("atomicDialog") && !item("atomicCancel")->isEnabled()
                && require("atomicDialog")->property("closePolicy").toInt() == 0;
        });
        click("atomicApply");
        check(waitUntil([this,&applied,&sourceExists,before] { return !m_app.busy() && !applied.isEmpty() && !sourceExists()
            && m_app.cards().size() == before+2 && !visible("atomicDialog"); }), "atomicize_apply_waits_for_durable_replacement");
        QObject::disconnect(watch);
        check(observedApply && protectedDuringApply, "atomicize_commit_controls_protected");
        check(m_app.history().size() == history && m_app.selectedDeckId() == m_deckA && m_window->property("page").toInt() == 1,
              "atomicize_commit_preserves_history_and_deck_context");
        bool editedSaved = false;
        for (const QVariant &value : m_app.cards()) {
            const QVariantMap card = value.toMap();
            if (card.value("front").toString() == QStringLiteral("An edited focused question?"))
                editedSaved = card.value("back").toString() == QStringLiteral("An edited focused answer.") && card.value("pointCount").toInt() == 2
                    && card.value("kind").toString() == QStringLiteral("basic");
        }
        check(editedSaved, "atomicize_saves_edited_markdown_and_points");
        check(provider.requests == 3 && provider.onlyFixtureCredentials, "atomicize_only_loopback_mock_requests");
        ai->setProperty("endpoint",originalEndpoint);
        ai->setProperty("provider",originalProvider);
    }

    QQmlApplicationEngine &m_engine;
    AppController &m_app;
    VoiceController &m_voice;
    const QString m_directory;
    QQuickWindow *m_window = nullptr;
    QJSValue m_root;
    QString m_deckA;
    QString m_deckB;
    QString m_deckChild;
    QString m_longCard;
    QStringList m_warnings;
    QJsonArray m_checks;
    QJsonArray m_screenshots;
    bool m_success = true;
};
}

void runGuiSweep(QQmlApplicationEngine &engine, AppController &app, VoiceController &voice,
                 const QString &artifactDirectory)
{
    if (!QDir().mkpath(artifactDirectory)) {
        qCritical().noquote() << "GUI_SWEEP_DIRECTORY: Cannot create artifact directory" << artifactDirectory;
        QCoreApplication::exit(1);
        return;
    }
    Sweep sweep(engine,app,voice,artifactDirectory);
    qmlWarnings = &sweep.warnings();
    previousHandler = qInstallMessageHandler(captureMessage);
    bool success = false;
    try { success = sweep.run(); }
    catch (const std::exception &error) { sweep.failure(QString::fromUtf8(error.what()));success = sweep.finish(); }
    qInstallMessageHandler(previousHandler);
    previousHandler = nullptr;
    qmlWarnings = nullptr;
    QCoreApplication::exit(success ? 0 : 1);
}
