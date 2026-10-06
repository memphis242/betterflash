#include "GuiSweep.h"
#include "core/appcontroller.h"
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

struct EnvironmentOverride final {
    explicit EnvironmentOverride(QByteArray variable, const QByteArray &value)
        : name(std::move(variable)), original(qgetenv(name.constData())), present(qEnvironmentVariableIsSet(name.constData()))
    { qputenv(name.constData(),value); }
    ~EnvironmentOverride()
    {
        if (present) qputenv(name.constData(),original);
        else qunsetenv(name.constData());
    }
    const QByteArray name;
    const QByteArray original;
    const bool present;
};

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
        reviewIdleBrowser();
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
        const auto names = {QStringLiteral("reviewTopBar"),QStringLiteral("libraryTopBar"),QStringLiteral("settingsTopBar")};
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
        const QPointF position = rect(name).center();
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
    void reviewIdleBrowser()
    {
        m_app.stopReview();
        for (int index = 0; index < 8; ++index)
            m_app.createDeck(QStringLiteral("Browser fixture %1").arg(index), QStringLiteral("Horizontal browser fixture."));
        if (!check(waitUntil([this] { return !m_app.busy() && m_app.decks().size() == 12; }), "review_browser_overflow_fixtures")) return;
        call("setDeck",{m_deckA,true});
        key(Qt::Key_1,Qt::AltModifier);
        if (!check(waitUntil([this] { return !m_app.busy() && visible("reviewIdle"); }), "review_idle_visible")) return;
        check(visible("reviewDeckBrowser") && visible("reviewDeckList"), "review_deck_browser_visible");
        QObject *const deckList = require("reviewDeckList");
        int topLevelCount = 0;
        for (const QVariant &value : m_app.decks())
            topLevelCount += value.toMap().value("parentId").toString().isEmpty() ? 1 : 0;
        check(deckList->property("count").toInt() == topLevelCount, "review_root_lists_only_top_level_decks",
              QStringLiteral("count=%1").arg(deckList->property("count").toInt()));
        check(deckList->property("contentWidth").toDouble() > deckList->property("width").toDouble(),
              "review_deck_browser_has_bounded_horizontal_overflow");
        deckList->setProperty("contentX",0.0);
        const QPointF deckPosition = rect("reviewDeckList").center();
        qreal previousX = 0.0;
        bool wheelMonotonic = true;
        for (int iteration = 0; iteration < 8; ++iteration) {
            QWheelEvent event(deckPosition,m_window->mapToGlobal(deckPosition.toPoint()),QPoint(),QPoint(0,-120),
                              Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QTest::lastMouseTimestamp += 35;
            event.setTimestamp(static_cast<quint64>(QTest::lastMouseTimestamp));
            QCoreApplication::sendEvent(m_window,&event);
            QTest::qWait(30);
            const qreal currentX = deckList->property("contentX").toDouble();
            if (currentX + 0.5 < previousX) wheelMonotonic = false;
            previousX = currentX;
        }
        check(wheelMonotonic && previousX > 0 && m_window->property("page").toInt() == 0,
              "review_deck_browser_horizontal_wheel_is_bounded");
        QObject *const stats = require("reviewStats");
        check(stats->property("visible").toBool(), "review_queue_stats_visible");
        QVariantMap selected;
        for (const QVariant &value : m_app.decks())
            if (value.toMap().value("id").toString() == m_deckA) selected = value.toMap();
        const QStringList statKeys = {QStringLiteral("dueCount"),QStringLiteral("newDueCount"),QStringLiteral("reviewDueCount"),QStringLiteral("laterCount")};
        const QStringList objectKeys = {QStringLiteral("due"),QStringLiteral("new"),QStringLiteral("review"),QStringLiteral("later")};
        for (int index = 0; index < statKeys.size(); ++index) {
            QObject *const stat = require("queueStat" + objectKeys[index]);
            const QVariant expected = m_app.decks().isEmpty() ? QVariant(0) : QVariant(selected.value(statKeys[index]).toInt());
            check(stat->property("count").toInt() == expected.toInt(), "review_queue_stat_matches_model_" + objectKeys[index],
                  QStringLiteral("actual=%1 expected=%2").arg(stat->property("count").toInt()).arg(expected.toInt()));
        }
        const QRectF idle = rect("reviewIdle");
        const QRectF action = rect("reviewStartArea");
        const QRectF primary = rect("reviewPrimary");
        check(primary.width() >= 180 && primary.height() >= 48
              && std::abs(primary.center().x() - action.center().x()) <= 2.0
              && primary.center().y() >= idle.top() + idle.height() * 0.5,
              "review_idle_primary_is_large_and_centered");
        check(action.top() >= idle.top() && action.bottom() <= idle.bottom() + 1.1, "review_idle_action_zone_bounded");
        call("setDeck",{m_deckA,true});
        QObject *const subdecks = require("reviewBrowseSubdecks");
        QStringList childNames;
        for (const QVariant &value : m_app.decks()) {
            const QVariantMap deck = value.toMap();
            if (deck.value("parentId").toString() == m_deckA) childNames.append(deck.value("id").toString());
        }
        check(subdecks->property("enabled").toBool(), "review_subdeck_action_enabled",
              QStringLiteral("appSelected=%1 windowSelected=%2 uiSelected=%3 parent=%4 children=%5")
                  .arg(m_app.selectedDeckId()).arg(m_window->property("selectedDeckId").toString())
                  .arg(require("reviewDeckBreadcrumb")->property("text").toString()).arg(m_deckA).arg(childNames.join(',')));
        click("reviewBrowseSubdecks");
        check(m_window->property("deckBrowserParentId").toString() == m_deckA, "review_browse_enters_selected_subdecks");
        check(require("reviewDeckList")->property("count").toInt() == 1, "review_child_level_lists_children");
        click("reviewDeckBrowserBack");
        check(m_window->property("deckBrowserParentId").toString().isEmpty(), "review_browse_back_returns_to_root");
        click("reviewDeckTreeToggle");
        check(m_window->property("deckTreeExpanded").toBool() && visible("reviewDeckTree"), "review_tree_expands_from_root");
        const QRectF tree = rect("reviewDeckTree");
        check(tree.width() <= rect("reviewDeckBrowser").width() + 1.1 && tree.height() > 0, "review_tree_is_bounded");
        QObject *const treeList = require("reviewDeckTree");
        click("reviewDeckBranch0");
        check(treeList->property("count").toInt() == 1 && visible("reviewDeckTree"), "review_tree_branch_collapses_to_root");
        click("reviewDeckBranch0");
        check(treeList->property("count").toInt() > 1, "review_tree_branch_reopens_children");
        click("reviewDeckTreeSelect1");
        check(m_app.selectedDeckId() == m_deckChild && m_window->property("page").toInt() == 0,
              "review_tree_child_selection_preserves_review_context");
        focus("reviewDeckList");
        key(Qt::Key_Right);
        key(Qt::Key_Return);
        check(m_app.selectedDeckId() != m_deckA && !m_app.selectedDeckId().isEmpty(), "review_deck_tiles_keyboard_select");
        call("setDeck",{m_deckA,true});
        check(m_window->property("deckTreeExpanded").toBool(), "review_tree_state_persists_across_selection");
        key(Qt::Key_2,Qt::AltModifier);
    }
    void desktop(const QString &theme)
    {
        resize(1320,860);
        key(Qt::Key_2,Qt::AltModifier);
        call("setDeck",{m_deckA});
        check(m_window->property("page").toInt() == 1, "library_navigation_" + theme);
        wheel("libraryScroll",theme + "_desktop");
        call("pickCard",{m_longCard});
        check(item("cardDetails")->isVisible(), "detail_opens_after_selection_" + theme);
        const qreal detailWidth = item("cardDetails")->width();
        const qreal listWidth = item("libraryList")->width();
        check(detailWidth >= 260 && listWidth >= 300, "detail_has_sensible_bounds_" + theme,
              QStringLiteral("detail=%1 list=%2").arg(detailWidth).arg(listWidth));
        wheel("detailsScroll",theme + "_desktop");
        click("detailsToggle");
        check(!item("cardDetails")->isVisible() && item("libraryList")->width() >= listWidth + detailWidth - 2,
              "detail_collapse_restores_full_list_" + theme);
        click("detailsToggle");
        check(std::abs(item("cardDetails")->width()-detailWidth) < 1.1, "detail_reopen_preserves_width_" + theme,
              QStringLiteral("before=%1 after=%2").arg(detailWidth).arg(item("cardDetails")->width()));
        focus("detailsHandle");
        const int initial = call("detailWidth").toInt();
        key(Qt::Key_Left);
        const int resized = call("detailWidth").toInt();
        check(resized == std::min(initial+24,650), "detail_keyboard_resize_" + theme);
        const qreal scrollBeforeNavigation = require("libraryScroll")->property("contentY").toDouble();
        key(Qt::Key_3,Qt::AltModifier);
        key(Qt::Key_2,Qt::AltModifier);
        check(call("detailWidth").toInt() == resized && m_window->property("selectedCardId").toString() == m_longCard,
              "navigation_preserves_card_and_pane_width_" + theme);
        check(std::abs(require("libraryScroll")->property("contentY").toDouble() - scrollBeforeNavigation) < 1.1,
              "navigation_preserves_library_scroll_" + theme);
        click("navToggle");
        check(m_window->property("navCollapsed").toBool() && rect("sideNavigation").width() <= 60
              && rect("mainContent").width() >= m_window->width() - 1.1,
              "side_navigation_collapse_preserves_content_" + theme);
        for (const QString &navName : {QStringLiteral("navReview"),QStringLiteral("navLibrary"),QStringLiteral("navHistory"),QStringLiteral("navSettings")})
            check(visible(navName) && require(navName)->property("enabled").toBool(), "collapsed_navigation_control_" + navName + "_" + theme);
        click("navReview");
        click("navLibrary");
        check(m_window->property("page").toInt() == 1 && m_window->property("selectedCardId").toString() == m_longCard,
              "collapsed_navigation_preserves_library_context_" + theme);
        layouts(theme + "_desktop_collapsed");
        click("navToggle");
        check(!m_window->property("navCollapsed").toBool() && rect("sideNavigation").width() > 60,
              "side_navigation_reopens_" + theme);
        layouts(theme + "_desktop_expanded");
        screenshot(theme + "-desktop-library");
        keyboardNavigation(theme);
        review(theme);
        key(Qt::Key_4,Qt::AltModifier);
        call("setSettingsSection",{1});
        require("settingsSection")->setProperty("currentIndex",1);
        require("settingsPage")->setProperty("section",1);
        QTest::qWait(100);
        wheel("settingsScroll",theme + "_desktop_keyboard");
        screenshot(theme + "-desktop-keyboard");
        rebind(theme);
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
        check(visible("deckPicker"), "deck_search_keyboard_" + theme);
        require("deckSearch")->setProperty("text",QStringLiteral("Native interaction"));
        key(Qt::Key_Down);
        key(Qt::Key_Up);
        key(Qt::Key_Return);
        check(waitUntil([this] { return m_app.selectedDeckId() == m_deckA && !visible("deckPicker"); }),
              "deck_search_selects_deck_" + theme);
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
        QObject *const preview = require("reviewQueuePreview");
        check(preview->property("timelineMode").toBool(), "review_preview_is_timeline_" + suffix);
        check(preview->property("horizontalScrollable").toBool() && preview->property("interactive").toBool(),
              "review_preview_is_horizontally_scrollable_" + suffix);
        check(!preview->property("autoCentersCurrent").toBool(),
              "review_preview_does_not_auto_recenter_" + suffix);
        check(!preview->property("railVisible").toBool(), "review_preview_has_no_rail_" + suffix);
        check(preview->property("count").toInt() >= 1, "review_timeline_has_current_card_" + suffix,
              QStringLiteral("count=%1 pending=%2").arg(preview->property("count").toInt()).arg(m_app.pendingCards().size()));

        const QRectF viewport = rect("reviewQueuePreview");
        const QRectF surface = rect("reviewDialogSurface");
        check(viewport.left() >= surface.left() - 1.1 && viewport.right() <= surface.right() + 1.1
              && viewport.top() >= surface.top() - 1.1 && viewport.bottom() <= surface.bottom() + 1.1,
              "review_timeline_inside_modal_" + suffix);
        check(viewport.width() >= surface.width() - 48.0,
              "review_timeline_spans_modal_section_" + suffix,
              QStringLiteral("timeline=%1 modal=%2").arg(viewport.width()).arg(surface.width()));

        const int currentIndex = preview->property("currentIndex").toInt();
        check(preview->property("currentIndex").isValid() && currentIndex >= 0,
              "review_timeline_current_card_is_addressable_" + suffix,
              QStringLiteral("index=%1").arg(currentIndex));
        check(require("reviewQueueCurrent")->property("timelineRole").toString() == QStringLiteral("current"),
              "review_timeline_marks_current_role_" + suffix);
        check(!require("reviewQueueCurrent")->property("currentLabelVisible").toBool(),
              "review_timeline_expresses_current_without_label_" + suffix);

        const int previousCount = preview->property("previousCount").toInt();
        const int upcomingCount = preview->property("upcomingCount").toInt();
        const int expectedUpcoming = std::min(5, std::max(0, static_cast<int>(m_app.pendingCards().size()) - 1));
        check(previousCount >= 0 && previousCount <= 2 && upcomingCount == expectedUpcoming
              && preview->property("count").toInt() == upcomingCount + 3,
              "review_timeline_role_counts_match_" + suffix,
              QStringLiteral("previous=%1 upcoming=%2 expectedUpcoming=%3 count=%4").arg(previousCount).arg(upcomingCount)
                  .arg(expectedUpcoming).arg(preview->property("count").toInt()));
        check(preview->property("previousSpaceReserved").toBool(),
              "review_timeline_reserves_leading_space_" + suffix);
        if (previousCount > 0) {
            QObject *const previous = require("reviewTimelineItem1");
            check(previous->property("timelineRole").toString() == QStringLiteral("previous")
                      && !previous->property("variantId").toString().isEmpty()
                      && previous->property("reviewOutline").toBool()
                      && previous->property("reviewedGrade").toInt() >= 0
                      && previous->property("reviewedGrade").toInt() <= 4
                      && previous->property("opacity").toDouble() < 1.0,
                  "review_timeline_previous_card_carries_a_faded_grade_" + suffix);
        }
        check(preview->property("fadeEnds").toBool(), "review_timeline_has_faded_ends_" + suffix);
        check(preview->property("previousCardsFaded").toBool() && preview->property("upcomingCardsHazy").toBool(),
              "review_timeline_applies_directional_fade_" + suffix);
        check(preview->property("previousOutlines").toBool(), "review_timeline_previous_cards_have_review_outlines_" + suffix);

        const qreal contentWidth = preview->property("contentWidth").toDouble();
        const qreal viewportWidth = preview->property("width").toDouble();
        if (contentWidth > viewportWidth + 5.0) {
            const QString beforeVariant = currentVariant();
            const int beforeIndex = currentIndex;
            const qreal maximum = std::max(0.0, contentWidth - viewportWidth);
            preview->setProperty("contentX", maximum);
            QTest::qWait(100);
            const qreal scrolled = preview->property("contentX").toDouble();
            check(scrolled >= maximum - 2.0 && currentVariant() == beforeVariant
                      && preview->property("currentIndex").toInt() == beforeIndex,
                  "review_preview_scrolls_without_changing_card_" + suffix,
                  QStringLiteral("contentX=%1 maximum=%2 index=%3->%4 variantUnchanged=%5")
                      .arg(scrolled).arg(maximum).arg(beforeIndex).arg(preview->property("currentIndex").toInt())
                      .arg(currentVariant() == beforeVariant));
            const bool currentOutOfView = !rect("reviewQueueCurrent").intersects(viewport);
            if (currentOutOfView)
                check(true, "review_preview_allows_current_card_out_of_view_" + suffix);
        }
    }
    void reviewLifecycle()
    {
        m_app.stopReview();
        QTest::qWait(60);
        m_app.startReview(m_deckB);
        if (!check(waitUntil([this] { return !m_app.busy() && m_app.reviewing() && !m_app.currentCard().isEmpty(); }),
                   "short_review_starts")) return;
        check(visible("reviewDialog"), "short_review_modal_visible");
        QObject *const shortPreview = require("reviewQueuePreview");
        check(shortPreview->property("count").toInt() >= 1 && shortPreview->property("currentIndex").toInt() >= 0,
              "short_review_timeline_has_current_card");
        m_app.revealAnswer();
        m_app.grade(3);
        check(waitUntil([this] { return !m_app.busy() && !m_app.reviewing(); })
              && !visible("reviewDialog"), "short_review_exhaustion_closes_modal");
        m_app.startReview(m_deckA);
        if (!check(waitUntil([this] { return !m_app.busy() && m_app.reviewing() && visible("reviewDialog"); }),
                   "review_lifecycle_restart")) return;
        m_app.stopReview();
        check(waitUntil([this] { return !m_app.busy() && !m_app.reviewing() && !visible("reviewDialog"); }),
              "review_stop_closes_modal");
    }
    void review(const QString &theme)
    {
        m_app.stopReview();
        QTest::qWait(60);
        key(Qt::Key_1,Qt::AltModifier);
        m_app.startReview(m_deckA);
        if (!check(waitUntil([this] { return !m_app.busy() && m_app.reviewing() && !m_app.currentCard().isEmpty(); }), "review_started_" + theme))
            throw std::runtime_error("Review did not start.");
        check(visible("reviewIdle") && visible("reviewDialog") && visible("reviewModalHeader")
              && visible("reviewPause") && visible("reviewPrimary"),
              "active_review_uses_pinned_pause_control_" + theme);
        const QRectF surface = rect("reviewDialogSurface");
        check(surface.left() >= -1.1 && surface.top() >= -1.1 && surface.right() <= m_window->width() + 1.1
              && surface.bottom() <= m_window->height() + 1.1
              && require("reviewDialogSurface")->property("radius").toDouble() > 0,
              "review_dialog_is_bounded_and_rounded_" + theme);
        const QRectF modalHeader = rect("reviewModalHeader");
        const QRectF deckTitle = rect("reviewDeckTitle");
        const QRectF pause = rect("reviewPause");
        const QRectF closeButton = rect("reviewClose");
        check(pause.top() >= modalHeader.top() - 1.1 && pause.bottom() <= modalHeader.bottom() + 1.1
              && pause.right() <= modalHeader.right() + 1.1 && pause.left() >= modalHeader.left() - 1.1,
              "review_pause_sits_in_title_bar_" + theme);
        check(pause.left() >= deckTitle.right() - 1.1 && pause.right() < closeButton.left() - 4.0,
              "review_pause_is_after_title_and_before_close_" + theme,
              QStringLiteral("titleRight=%1 pause=%2..%3 closeLeft=%4")
                  .arg(deckTitle.right()).arg(pause.left()).arg(pause.right()).arg(closeButton.left()));
        const QRectF timer = rect("reviewTimer");
        check(require("reviewTimer")->property("running").toBool()
                  && require("reviewTimer")->property("elapsedSeconds").toDouble() >= 0.0,
              "review_timer_runs_during_active_card_" + theme);
        check(std::abs(timer.center().x() - surface.center().x()) <= 3.0,
              "review_timer_is_centered_" + theme,
              QStringLiteral("timerCenter=%1 modalCenter=%2")
                  .arg(timer.center().x()).arg(surface.center().x()));
        checkReviewQueuePreview(theme + "_initial");
        const QString modalVariant = currentVariant();
        const int modalQueue = m_app.queueCount();
        const bool modalAnswer = m_app.answerRevealed();
        QTest::mouseClick(m_window,Qt::LeftButton,Qt::NoModifier,QPoint(5,5));
        QTest::qWait(80);
        check(m_app.reviewing() && !m_app.paused() && currentVariant() == modalVariant && m_app.queueCount() == modalQueue,
              "review_modal_blocks_outside_click_" + theme);
        click("reviewClose");
        check(waitUntil([this] { return m_app.paused() && !visible("reviewDialog"); })
              && currentVariant() == modalVariant && m_app.queueCount() == modalQueue && m_app.answerRevealed() == modalAnswer,
              "review_close_pauses_and_preserves_session_" + theme);
        const double pausedTime = m_app.responseSeconds();
        QTest::qWait(100);
        check(std::abs(m_app.responseSeconds() - pausedTime) < 0.03, "review_close_stops_response_timer_" + theme);
        click("reviewPrimary");
        check(waitUntil([this] { return m_app.reviewing() && !m_app.paused() && visible("reviewDialog"); }),
              "review_resume_reopens_same_session_" + theme);
        check(currentVariant() == modalVariant && m_app.queueCount() == modalQueue && m_app.answerRevealed() == modalAnswer,
              "review_resume_preserves_session_state_" + theme);
        key(Qt::Key_Escape);
        check(waitUntil([this] { return m_app.paused() && !visible("reviewDialog"); })
              && currentVariant() == modalVariant && m_app.queueCount() == modalQueue && m_app.answerRevealed() == modalAnswer,
              "review_escape_pauses_and_preserves_session_" + theme);
        click("reviewPrimary");
        check(waitUntil([this] { return m_app.reviewing() && !m_app.paused() && visible("reviewDialog"); }),
              "review_escape_resume_reopens_session_" + theme);
        ensureLongCurrent();
        check(item("reviewQuestion")->height() > 0 && require("reviewQuestion")->property("renderError").toString().isEmpty(),
              "question_and_image_render_" + theme);
        const QRectF question = rect("reviewQuestion");
        const QRectF reveal = rect("revealAnswer");
        check(!m_app.answerRevealed() && !visible("reviewAnswer"),
              "review_answer_is_concealed_before_reveal_" + theme);
        check(reveal.top() >= question.bottom() - 1.1
                  && reveal.width() >= surface.width() * 0.70
                  && reveal.height() >= 52.0
                  && require("revealAnswer")->property("radius").toDouble() > 0,
              "review_reveal_is_large_rounded_area_below_question_" + theme,
              QStringLiteral("questionBottom=%1 reveal=%2..%3 size=%4x%5 radius=%6")
                  .arg(question.bottom()).arg(reveal.top()).arg(reveal.bottom()).arg(reveal.width()).arg(reveal.height())
                  .arg(require("revealAnswer")->property("radius").toDouble()));
        wheel("reviewScroll",theme + "_desktop_question");
        const QRectF toolbarBefore = rect("reviewToolbar");
        wheel("reviewScroll",theme + "_desktop_question_again");
        check(rect("reviewToolbar") == toolbarBefore, "review_controls_stay_pinned_" + theme);
        check(rect("reviewModalHeader").top() <= rect("reviewQueuePreview").top()
              && rect("reviewToolbar").bottom() <= rect("reviewDialogSurface").bottom() + 1.1,
              "review_modal_header_and_grading_footer_stay_pinned_" + theme);
        key(Qt::Key_P);
        check(m_app.paused(), "review_pause_keyboard_" + theme);
        const double elapsed = m_app.responseSeconds();
        QTest::qWait(100);
        check(std::abs(m_app.responseSeconds()-elapsed) < 0.03, "response_timer_paused_" + theme);
        key(Qt::Key_P);
        check(!m_app.paused(), "review_resume_keyboard_" + theme);
        const QString current = currentVariant();
        const int queue = m_app.queueCount();
        key(Qt::Key_N,Qt::ControlModifier);
        check(visible("cardDialog"), "editor_during_review_" + theme);
        require("editorSource")->setProperty("text",QStringLiteral("Typing "));
        require("editorSource")->setProperty("cursorPosition",7);
        focus("editorSource");
        for (const Qt::Key letter : {Qt::Key_D,Qt::Key_S,Qt::Key_P,Qt::Key_1,Qt::Key_Space}) key(letter);
        check(m_app.queueCount() == queue && currentVariant() == current && !m_app.paused() && !m_app.answerRevealed(),
              "review_shortcuts_do_not_intercept_editor_" + theme);
        close("cardDialog");
        key(Qt::Key_Space);
        check(m_app.answerRevealed(), "reveal_keyboard_" + theme);
        check(item("reviewAnswer")->height() > 0 && require("reviewAnswer")->property("renderError").toString().isEmpty(),
              "answer_and_image_render_" + theme);
        const QRectF firstGrade = rect("grade0");
        const QRectF revealedTimer = rect("reviewTimer");
        check(revealedTimer.bottom() <= firstGrade.top() + 1.1,
              "review_timer_is_above_grade_controls_" + theme,
              QStringLiteral("timerBottom=%1 gradeTop=%2").arg(revealedTimer.bottom()).arg(firstGrade.top()));
        wheel("reviewScroll",theme + "_desktop_answer");
        screenshot(theme + "-desktop-review");
        const int historyCount = m_app.history().size();
        key(Qt::Key_2);
        check(visible("partialDialog"), "partial_grade_keyboard_" + theme);
        require("recalledPoints")->setProperty("value",1);
        click("gradePartialConfirm");
        check(waitUntil([this,historyCount,queue] { return !m_app.busy() && m_app.history().size() == historyCount+1 && m_app.queueCount() == queue-1; }),
              "partial_grade_records_and_advances_" + theme);
        checkReviewQueuePreview(theme + "_after_grade");
        if (!m_app.history().isEmpty()) {
            const QVariantMap grade = m_app.history().first().toMap();
            check(grade.value("grade").toInt() == 1 && std::abs(grade.value("recallFraction").toDouble()-1.0/3.0) < 0.001,
                  "partial_grade_uses_point_fraction_" + theme);
        }
        const QString deferred = currentVariant();
        const int deferredCount = m_app.queueCount();
        key(Qt::Key_D);
        check(waitUntil([this,deferred] { return !m_app.busy() && currentVariant() != deferred; }), "defer_keyboard_advances_" + theme);
        checkReviewQueuePreview(theme + "_after_defer");
        const QVariantList pending = m_app.pendingCards();
        const QString last = pending.isEmpty() ? QString() : pending.last().toMap().value("variantId",pending.last().toMap().value("id")).toString();
        check(m_app.queueCount() == deferredCount && m_app.history().size() == historyCount+1 && last == deferred,
              "defer_moves_to_end_without_grade_" + theme);
        const int postponedCount = m_app.queueCount();
        key(Qt::Key_S);
        check(visible("postponeDialog"), "postpone_keyboard_" + theme);
        click("postponeConfirm");
        check(waitUntil([this,postponedCount] { return !m_app.busy() && m_app.queueCount() == postponedCount-1; })
              && m_app.history().size() == historyCount+1 && !visible("postponeDialog"),
              "postpone_accepts_local_tomorrow_" + theme,
              QStringLiteral("field=%1 dialogVisible=%2 queue=%3 expected=%4")
                  .arg(require("postponeDate")->property("text").toString())
                  .arg(visible("postponeDialog"))
                  .arg(m_app.queueCount()).arg(postponedCount-1));
        checkReviewQueuePreview(theme + "_after_postpone");
        key(Qt::Key_K,Qt::ControlModifier);
        require("commandSearch")->setProperty("text",QStringLiteral("summ"));
        key(Qt::Key_Return);
        check(waitUntil([this] { return visible("aiDialog"); }), "summary_action_opens_disclosure_" + theme);
        QObject *const ai = m_engine.rootContext()->contextProperty("ai").value<QObject *>();
        check(ai && !ai->property("busy").toBool() && ai->property("summary").toString().isEmpty(),
              "summary_never_autosends_" + theme);
        close("aiDialog");
        const QString navigationVariant = currentVariant();
        const int navigationQueue = m_app.queueCount();
        key(Qt::Key_2,Qt::AltModifier);
        check(m_window->property("page").toInt() == 1 && m_app.paused() && !visible("reviewDialog")
              && currentVariant() == navigationVariant && m_app.queueCount() == navigationQueue,
              "review_navigation_pauses_and_preserves_session_" + theme);
        key(Qt::Key_1,Qt::AltModifier);
        check(visible("reviewPrimary"), "review_navigation_exposes_resume_action_" + theme);
        click("reviewPrimary");
        check(waitUntil([this] { return m_app.reviewing() && !m_app.paused() && visible("reviewDialog"); }),
              "review_navigation_resume_reopens_session_" + theme);
        layouts(theme + "_desktop_review");
    }
    void phone(const QString &theme)
    {
        resize(390,780);
        key(Qt::Key_2,Qt::AltModifier);
        call("setDeck",{m_deckA});
        check(!item("cardDetails")->isVisible() && item("libraryList")->width() >= item("mainContent")->width() - rect("sideNavigation").width() - 32,
              "phone_library_uses_full_width_" + theme);
        layouts(theme + "_phone_library");
        wheel("libraryScroll",theme + "_phone_library");
        screenshot(theme + "-phone-library");
        click("detailsToggle");
        check(visible("cardDetailDialog"), "phone_card_detail_modal_" + theme);
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
        if (m_app.paused() && visible("reviewPrimary")) click("reviewPrimary");
        check(waitUntil([this] { return m_app.reviewing() && !m_app.paused() && visible("reviewDialog"); }),
              "phone_review_modal_reopened_after_navigation_" + theme);
        ensureLongCurrent();
        if (!m_app.answerRevealed()) key(Qt::Key_Space);
        wheel("reviewScroll",theme + "_phone_review");
        layouts(theme + "_phone_review");
        const QRectF grades = rect("reviewToolbar");
        const QRectF modalSurface = rect("reviewDialogSurface");
        check(grades.left() >= modalSurface.left() - 1.1 && grades.right() <= modalSurface.right() + 1.1
              && grades.top() >= modalSurface.top() - 1.1 && grades.bottom() <= modalSurface.bottom() + 1.1,
              "phone_review_controls_do_not_overlap_nav_" + theme);
        QObject *const queuePreview = require("reviewQueuePreview");
        const QString previewVariant = currentVariant();
        const int previewQueue = m_app.queueCount();
        queuePreview->setProperty("currentIndex",0);
        queuePreview->setProperty("contentX",0.0);
        QTest::qWait(80);
        const int previewStart = queuePreview->property("currentIndex").toInt();
        focus("reviewQueuePreview");
        key(Qt::Key_Right);
        key(Qt::Key_Right);
        key(Qt::Key_Right);
        const int previewEnd = queuePreview->property("currentIndex").toInt();
        check(previewEnd > previewStart && previewEnd < queuePreview->property("count").toInt()
              && currentVariant() == previewVariant && m_app.queueCount() == previewQueue,
              "phone_review_preview_keyboard_navigation_is_bounded_" + theme,
              QStringLiteral("index=%1->%2 count=%3 focus=%4 sameVariant=%5 queue=%6->%7")
                  .arg(previewStart).arg(previewEnd).arg(queuePreview->property("count").toInt())
                  .arg(queuePreview->property("activeFocus").toBool()).arg(currentVariant() == previewVariant)
                  .arg(previewQueue).arg(m_app.queueCount()));
        screenshot(theme + "-phone-review");
        check(!visible("navToggle") && rect("sideNavigation").width() <= 60,
              "phone_navigation_uses_persistent_icon_rail_" + theme);
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
        resize(860,600);
        key(Qt::Key_2,Qt::AltModifier);
        call("setDeck",{m_deckA});
        call("pickCard",{m_longCard});
        layouts(theme + "_compact_desktop");
        const qreal pageWidth = item("mainContent")->width() - rect("sideNavigation").width();
        check(item("libraryList")->width() >= 260 && item("cardDetails")->width() >= 240
              && item("libraryList")->width() + item("cardDetails")->width() <= pageWidth + 2,
              "compact_desktop_panes_clamp_width_" + theme);
        click("detailsToggle");
        check(item("libraryList")->width() >= pageWidth - 2 * m_window->property("gutter").toInt() - 2, "compact_desktop_collapsed_list_full_width_" + theme);
        click("detailsToggle");
        screenshot(theme + "-compact-desktop-library");
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
        call("openVoiceConfig");
        checkDialog("voiceConfigDialog");
        call("openAiConfig");
        checkDialog("aiConfigDialog");
        call("openSyncConfig");
        checkDialog("syncConfigDialog");
        call("openPartial");
        checkDialog("partialDialog");
        call("openPostpone");
        checkDialog("postponeDialog");
        call("openSummary");
        checkDialog("aiDialog");
        call("openAllHistory");
        checkDialog("allHistoryDialog");
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
        const struct EnvironmentOverride credential(QByteArrayLiteral("BETTERFLASH_LLM_API_KEY"),QByteArrayLiteral("gui-fixture-only"));
        const QString originalEndpoint = ai->property("endpoint").toString();
        ai->setProperty("endpoint",provider.endpoint());
        check(ai->property("hasApiKey").toBool(), "atomicize_uses_isolated_mock_credential");
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
        click("atomicizeSelectedCard");
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
        click("atomicizeSelectedCard");
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
        click("atomicizeSelectedCard");
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
