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

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle("Fusion");
    app.setWindowIcon(QIcon(":/images/droidstar.png"));
    
    // Register DroidStar type if necessary
    qmlRegisterType<DroidStar>("org.dudetronics.droidstar", 1, 0, "DroidStar");
   // qmlRegisterType<VUIDUpdater>("org.dudetronics.droidstar", 1, 0, "VUIDUpdater");
    
  
    QQmlApplicationEngine engine;
    //VUIDUpdater *vuidUpdater = new VUIDUpdater();  // Create instance
    VUIDUpdater vuidUpdater;
       // engine.rootContext()->setContextProperty("vuidUpdater", vuidUpdater); // Provide to QML
    
    
    engine.rootContext()->setContextProperty("vuidUpdater", &vuidUpdater); // Provide to QML by passing a pointer

    // Register LogHandler class with QML
       LogHandler logHandler;
       engine.rootContext()->setContextProperty("logHandler", &logHandler);
   
    
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
