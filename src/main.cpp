#include "core/appcontroller.h"
#include "integrations/MediaStore.h"
#include "integrations/ShortcutManager.h"
#include "integrations/Summarizer.h"
#include "integrations/SyncController.h"
#include "render/MarkdownView.h"
#include "voice/VoiceController.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#ifdef BETTERFLASH_GUI_SWEEP
#include "verification/GuiSweep.h"
#endif

int main(int argc, char **argv) {
    QGuiApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("BetterFlash"));
    QCoreApplication::setApplicationName(QStringLiteral("BetterFlash"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Native Markdown flashcards for focused review."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("data-dir"), QStringLiteral("Use an isolated collection and settings directory."), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("demo"), QStringLiteral("Create the optional example deck in an empty collection.")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("Save a window capture after startup."), QStringLiteral("file")});
#ifdef BETTERFLASH_GUI_SWEEP
    parser.addOption({QStringLiteral("gui-sweep"), QStringLiteral("Run the native scripted interface verification."), QStringLiteral("artifact-directory")});
#endif
    parser.process(application);
    QString dataDirectory = parser.isSet(QStringLiteral("data-dir")) ? QDir(parser.value(QStringLiteral("data-dir"))).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(dataDirectory)) {
        qCritical().noquote() << QStringLiteral("STORAGE_DIRECTORY: Cannot create %1. Choose a writable --data-dir.").arg(dataDirectory);
        return 1;
    }
    if (parser.isSet(QStringLiteral("data-dir")))
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dataDirectory + QStringLiteral("/settings"));

    AppController app(dataDirectory);
    MediaStore media(dataDirectory);
    ShortcutManager shortcuts;
    Summarizer ai(&app);
    SyncController sync(&app, media.rootPath());
    VoiceController voice;
    qmlRegisterType<MarkdownView>("BetterFlash.Native", 1, 0, "MarkdownView");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &app);
    engine.rootContext()->setContextProperty(QStringLiteral("voice"), &voice);
    engine.rootContext()->setContextProperty(QStringLiteral("ai"), &ai);
    engine.rootContext()->setContextProperty(QStringLiteral("sync"), &sync);
    engine.rootContext()->setContextProperty(QStringLiteral("media"), &media);
    engine.rootContext()->setContextProperty(QStringLiteral("shortcuts"), &shortcuts);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    const auto speakQuestion = [&] {
        if (voice.enabled() && app.reviewing() && !app.paused())
            voice.speak(MarkdownView::plainText(app.currentCard().value(QStringLiteral("question")).toString()));
    };
    QObject::connect(&voice, &VoiceController::readyChanged, &application, [&] {
        if (voice.ready()) {
            if (app.reviewing()) speakQuestion();
            else voice.startListening();
        }
    });
    QObject::connect(&app, &AppController::currentCardChanged, &application, [&] {
        QTimer::singleShot(0, &application, speakQuestion);
    });
    QObject::connect(&app, &AppController::answerRevealedChanged, &application, [&] {
        if (voice.enabled() && app.answerRevealed())
            voice.speak(MarkdownView::plainText(app.currentCard().value(QStringLiteral("answer")).toString())
                + QStringLiteral(". Choose missed, partial, hard, good, or easy."));
    });
    QObject::connect(&app, &AppController::lastErrorChanged, &application, [&] {
        if (voice.enabled() && !app.lastError().isEmpty())
            voice.speak(app.lastError().value(QStringLiteral("message")).toString());
    });
    QObject::connect(&voice, &VoiceController::answerRecognized, &application, [&](const QString &answer) {
        if (!app.reviewing() || app.paused()) return;
        app.setSpokenAnswer(answer);
        if (!app.answerRevealed()) app.revealAnswer();
    });
    QObject::connect(&voice, &VoiceController::commandRecognized, &application,
        [&](const QString &command, const QVariant &argument) {
            if (command == QStringLiteral("start_review")) {
                QString deckId = app.selectedDeckId();
                const QString name = argument.toString().trimmed();
                if (!name.isEmpty()) {
                    deckId.clear();
                    for (const QVariant &entry : app.decks()) {
                        const QVariantMap deck = entry.toMap();
                        if (deck.value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0)
                            deckId = deck.value(QStringLiteral("id")).toString();
                    }
                    if (deckId.isEmpty()) { voice.speak(QStringLiteral("That deck was not found. Say start review to review the selected deck.")); return; }
                }
                app.startReview(deckId);
            } else if (command == QStringLiteral("show_answer")) app.revealAnswer();
            else if (command == QStringLiteral("repeat")) {
                if (app.answerRevealed()) voice.speak(MarkdownView::plainText(app.currentCard().value(QStringLiteral("answer")).toString()));
                else speakQuestion();
            } else if (command == QStringLiteral("missed") || command == QStringLiteral("again")) app.grade(0);
            else if (command == QStringLiteral("partial")) app.grade(1, argument.isValid() ? argument.toDouble() : 0.5);
            else if (command == QStringLiteral("hard")) app.grade(2);
            else if (command == QStringLiteral("good")) app.grade(3);
            else if (command == QStringLiteral("easy")) app.grade(4);
            else if (command == QStringLiteral("defer")) app.deferCard();
            else if (command == QStringLiteral("postpone")) app.postponeDays(argument.toInt());
            else if (command == QStringLiteral("pause")) { app.pauseReview(); voice.speak(QStringLiteral("Review paused. Say resume to continue.")); }
            else if (command == QStringLiteral("resume")) { app.resumeReview(); speakQuestion(); }
            else if (command == QStringLiteral("stop")) { app.stopReview(); voice.speak(QStringLiteral("Review ended. Say start review to begin again.")); }
        });

    if (parser.isSet(QStringLiteral("demo"))) {
        auto *const initialization = new QTimer(&application);
        initialization->setInterval(50);
        QObject::connect(initialization, &QTimer::timeout, &application, [&app, initialization] {
            if (app.busy()) return;
            initialization->stop();
            if (app.decks().isEmpty()) app.loadExampleDeck();
        });
        initialization->start();
    }
    engine.loadFromModule(QStringLiteral("BetterFlash"), QStringLiteral("Main"));
    if (parser.isSet(QStringLiteral("screenshot"))) {
        const QString output = parser.value(QStringLiteral("screenshot"));
        QTimer::singleShot(2500, &application, [&engine, output] {
            if (engine.rootObjects().isEmpty()) return;
            if (auto *const window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
                if (!window->grabWindow().save(output))
                    qWarning().noquote() << QStringLiteral("CAPTURE_WRITE: Could not save %1.").arg(output);
            }
        });
    }
#ifdef BETTERFLASH_GUI_SWEEP
    if (parser.isSet(QStringLiteral("gui-sweep"))) {
        const QString directory = parser.value(QStringLiteral("gui-sweep"));
        QTimer::singleShot(1000, &application, [&engine, &app, &voice, directory] {
            runGuiSweep(engine, app, voice, directory);
        });
    }
#endif
    return application.exec();
}

