#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QIcon>
#include <QQmlContext>
#include "droidstar.h"
#include "dmr.h"  // Ensure this is the correct include for your DMR class
#include <QSharedPointer>
#include "vuidupdater.h"  // Include the new header
#include "LogHandler.h"
#include "AudioSessionManager.h"
#include "LiveActivityQtBridge.h"
#include "TranscriptionEngine.h"
#include "NetReminderManager.h"
#include "DmrSmsManager.h"
#include "pttbuttonmanager.h"
#include "QsoReplayEngine.h"
#include "QsoReplayPlayer.h"



int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("dudetronics"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("dudetronics.org"));
    QCoreApplication::setApplicationName(QStringLiteral("droidstar"));
    // Use Material styling for a modern dark UI (Fusion defaults to a light palette).
    QQuickStyle::setStyle("Material");
    // QRC icon (registered via `DroidStar.pro` resources.files)
    app.setWindowIcon(QIcon(":/DroidStar/images/droidstar.png"));
    
    // Register DroidStar type
    qmlRegisterType<DroidStar>("org.dudetronics.droidstar", 1, 0, "DroidStar");
  
    QQmlApplicationEngine engine;

    // Local RX transcription engine & Ham Net Reminder manager
    TranscriptionEngine transcriptionEngine;
    engine.rootContext()->setContextProperty("transcriptionEngine", &transcriptionEngine);

    NetReminderManager netReminderManager;
    engine.rootContext()->setContextProperty("netReminderManager", &netReminderManager);

    // QSO Replay engine. Declared before DroidStar so it outlives every Mode/AudioEngine that taps
    // into it (the capture ring itself is static storage, see QsoReplayCapture.h).
    QsoReplayEngine replayEngine;
    engine.rootContext()->setContextProperty("replayEngine", &replayEngine);
    
    // Create an instance of DroidStar
        DroidStar droidStarInstance;
        
        // Expose the DroidStar instance to QML as a context property
        engine.rootContext()->setContextProperty("droidStar", &droidStarInstance);

    // Hardware PTT buttons (headset media button, Bluetooth LE buttons, keyboard-type buttons).
    // Declared after DroidStar so it is torn down first and its native callbacks are gone
    // before the audio session and modes go away.
    PttButtonManager pttButtons;
    engine.rootContext()->setContextProperty("pttButtons", &pttButtons);

    // QSO Replay playback. Declared after DroidStar (torn down first) so the native player is
    // released before the audio session goes away.
    QsoReplayPlayer replayPlayer(&replayEngine);
    engine.rootContext()->setContextProperty("replayPlayer", &replayPlayer);
    
    // Expose instances to QML
    VUIDUpdater vuidUpdater;
    engine.rootContext()->setContextProperty("vuidUpdater", &vuidUpdater);

    DmrSmsManager smsManager(&droidStarInstance, &vuidUpdater);
    engine.rootContext()->setContextProperty("smsManager", &smsManager);

    LogHandler logHandler;
    engine.rootContext()->setContextProperty("logHandler", &logHandler);
    replayEngine.attachLogHandler(&logHandler);
    replayEngine.start();

    // iOS Dynamic Island / Live Activity bridge (no-op on non-iOS)
    LiveActivityQtBridge liveActivity;
    engine.rootContext()->setContextProperty("liveActivity", &liveActivity);

    
    
    
      // Check for FLITE support
  #ifdef USE_FLITE
      engine.rootContext()->setContextProperty("USE_FLITE", QVariant(true));
  #else
      engine.rootContext()->setContextProperty("USE_FLITE", QVariant(false));
  #endif

      // Load the main QML file
      const QUrl url(u"qrc:/DroidStar/main.qml"_qs);
      QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                       &app, [url](QObject *obj, const QUrl &objUrl) {
                           if (!obj && url == objUrl)
                               QCoreApplication::exit(-1);
                       }, Qt::QueuedConnection);
      engine.load(url);

    
      return app.exec();
    
  }
