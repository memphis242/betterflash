#include "core/appcontroller.h"
#include "integrations/MediaStore.h"
#include "integrations/Atomicizer.h"
#include "integrations/ShortcutManager.h"
#include "integrations/Summarizer.h"
#include "integrations/SyncController.h"
#include "render/MarkdownView.h"
#include "voice/VoiceController.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QHash>
#include <QGuiApplication>
#include <QIcon>
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
    QCoreApplication::setOrganizationName(QStringLiteral("BetterFlash"));
    QCoreApplication::setApplicationName(QStringLiteral("BetterFlash"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QGuiApplication::setDesktopFileName(QStringLiteral("org.betterflash.BetterFlash"));
#ifdef Q_OS_ANDROID
    qputenv("ANDROID_OPENSSL_SUFFIX", "_3");
#endif
    QGuiApplication application(argc, argv);
    const QStringList fontResources{
        QStringLiteral(":/fonts/IBMPlexMono-Regular.ttf"),
        QStringLiteral(":/fonts/IBMPlexMono-Bold.ttf"),
        QStringLiteral(":/fonts/IBMPlexMono-Italic.ttf"),
        QStringLiteral(":/fonts/IBMPlexMono-BoldItalic.ttf")
    };
    for (const QString &resource : fontResources) {
        if (QFontDatabase::addApplicationFont(resource) < 0) {
            qCritical().noquote() << QStringLiteral("FONT_LOAD: Cannot load %1. Rebuild or reinstall BetterFlash.").arg(resource);
            return 1;
        }
    }
    application.setFont(QFont(QStringLiteral("IBM Plex Mono")));
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/betterflash.svg")));
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
    Atomicizer atomic(&app, &ai);
    SyncController sync(&app, media.rootPath());
    VoiceController voice;
#ifdef Q_OS_ANDROID
    QObject::connect(&application, &QGuiApplication::applicationStateChanged, &voice,
        [&voice](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) voice.setEnabled(false);
        });
#endif
    qmlRegisterType<MarkdownView>("BetterFlash.Native", 1, 0, "MarkdownView");
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &app);
    engine.rootContext()->setContextProperty(QStringLiteral("voice"), &voice);
    engine.rootContext()->setContextProperty(QStringLiteral("ai"), &ai);
    engine.rootContext()->setContextProperty(QStringLiteral("atomic"), &atomic);
    engine.rootContext()->setContextProperty(QStringLiteral("sync"), &sync);
    engine.rootContext()->setContextProperty(QStringLiteral("media"), &media);
    engine.rootContext()->setContextProperty(QStringLiteral("shortcuts"), &shortcuts);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &application,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);

    const auto speakQuestion = [&] {
        if (voice.enabled() && app.reviewing() && !app.paused()) {
            QString narration = MarkdownView::plainText(app.currentCard().value(QStringLiteral("question")).toString());
            if (app.reviewingCompletedCard())
                narration += QStringLiteral(". ") + MarkdownView::plainText(app.currentCard().value(QStringLiteral("answer")).toString());
            voice.speak(narration);
        }
    };
    bool voiceSummaryPending = false;
    QObject::connect(&ai, &Summarizer::changed, &application, [&] {
        if (!voiceSummaryPending || ai.busy()) return;
        voiceSummaryPending = false;
        if (voice.enabled()) voice.speak(ai.summary().isEmpty() ? ai.error() : MarkdownView::plainText(ai.summary()));
    });
    QObject::connect(&voice, &VoiceController::readyChanged, &application, [&] {
        if (voice.ready()) {
            if (app.reviewing()) speakQuestion();
            else voice.startListening();
        }
    });
    QObject::connect(&app, &AppController::currentCardChanged, &application, [&] {
        voice.cancelPendingRecognition();
        QTimer::singleShot(0, &application, speakQuestion);
    });
    QObject::connect(&app, &AppController::answerRevealedChanged, &application, [&] {
        if (voice.enabled() && app.answerRevealed() && !app.reviewingCompletedCard())
            voice.speak(MarkdownView::plainText(app.currentCard().value(QStringLiteral("answer")).toString())
                + QStringLiteral(". Choose missed, partial, hard, good, or easy."));
    });
    QObject::connect(&app, &AppController::reviewingChanged, &application, [&] {
        if (!app.reviewing()) {
            voice.cancelPendingRecognition();
            voice.stopSpeaking();
        }
        if (voice.enabled() && !app.reviewing() && app.sessionTotal() > 0)
            voice.speak(QStringLiteral("Review ended. %1 of %2 items reviewed. Say start review to begin again.")
                .arg(app.reviewedCount()).arg(app.sessionTotal()));
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
    enum class VoiceCommand : quint8 {
        Unknown, StartReview, ShowAnswer, Repeat, GradeMissed, GradePartial, GradeHard,
        GradeGood, GradeEasy, Defer, Postpone, Pause, Resume, Summarize, Stop
    };
    const QHash<QString, VoiceCommand> commandKinds{
        {QStringLiteral("start_review"), VoiceCommand::StartReview},
        {QStringLiteral("show_answer"), VoiceCommand::ShowAnswer},
        {QStringLiteral("repeat"), VoiceCommand::Repeat},
        {QStringLiteral("missed"), VoiceCommand::GradeMissed},
        {QStringLiteral("again"), VoiceCommand::GradeMissed},
        {QStringLiteral("partial"), VoiceCommand::GradePartial},
        {QStringLiteral("hard"), VoiceCommand::GradeHard},
        {QStringLiteral("good"), VoiceCommand::GradeGood},
        {QStringLiteral("easy"), VoiceCommand::GradeEasy},
        {QStringLiteral("defer"), VoiceCommand::Defer},
        {QStringLiteral("postpone"), VoiceCommand::Postpone},
        {QStringLiteral("pause"), VoiceCommand::Pause},
        {QStringLiteral("resume"), VoiceCommand::Resume},
        {QStringLiteral("summarize"), VoiceCommand::Summarize},
        {QStringLiteral("stop"), VoiceCommand::Stop}
    };
    QObject::connect(&voice, &VoiceController::commandRecognized, &application,
        [&](const QString &command, const QVariant &argument) {
            switch (commandKinds.value(command, VoiceCommand::Unknown)) {
            case VoiceCommand::StartReview: {
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
                break;
            }
            case VoiceCommand::ShowAnswer: app.revealAnswer(); break;
            case VoiceCommand::Repeat: {
                if (app.answerRevealed()) voice.speak(MarkdownView::plainText(app.currentCard().value(QStringLiteral("answer")).toString()));
                else speakQuestion();
                break;
            }
            case VoiceCommand::GradeMissed: app.grade(0); break;
            case VoiceCommand::GradePartial: app.grade(1, argument.isValid() ? argument.toDouble() : 0.5); break;
            case VoiceCommand::GradeHard: app.grade(2); break;
            case VoiceCommand::GradeGood: app.grade(3); break;
            case VoiceCommand::GradeEasy: app.grade(4); break;
            case VoiceCommand::Defer: app.deferCard(); break;
            case VoiceCommand::Postpone: app.postponeDays(argument.toInt()); break;
            case VoiceCommand::Pause:
                app.pauseReview(); voice.speak(QStringLiteral("Review paused. Say resume to continue.")); break;
            case VoiceCommand::Resume: app.resumeReview(); speakQuestion(); break;
            case VoiceCommand::Summarize:
                if (ai.busy()) { voice.speak(QStringLiteral("A summary is being generated.")); return; }
                voiceSummaryPending = true;
                ai.summarizeRemaining();
                break;
            case VoiceCommand::Stop: {
                const bool active = app.reviewing();
                app.stopReview();
                if (!active) voice.speak(QStringLiteral("No review is active. Say start review to begin."));
                break;
            }
            case VoiceCommand::Unknown: break;
            }
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
        if (!parser.isSet(QStringLiteral("data-dir"))) {
            qCritical().noquote() << QStringLiteral("SWEEP_STORAGE: Supply an isolated --data-dir for interface verification.");
            return 1;
        }
        const QString directory = parser.value(QStringLiteral("gui-sweep"));
        QTimer::singleShot(1000, &application, [&engine, &app, &voice, directory] {
            runGuiSweep(engine, app, voice, directory);
        });
    }
#endif
    return application.exec();
}
