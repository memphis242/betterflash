#pragma once

#include <QString>

class QQmlApplicationEngine;
class AppController;
class VoiceController;

void runGuiSweep(QQmlApplicationEngine &engine, AppController &app,
                 VoiceController &voice, const QString &artifactDirectory);
