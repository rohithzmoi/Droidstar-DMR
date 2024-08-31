
/*
	Original Copyright (C) 2019-2021 Doug McLain
	Modification Copyright (C) 2024 Rohith Namboothiri

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

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
    
    // Register DroidStar type
    qmlRegisterType<DroidStar>("org.dudetronics.droidstar", 1, 0, "DroidStar");
  
    
  
    QQmlApplicationEngine engine;
    //VUIDUpdater *vuidUpdater = new VUIDUpdater();  // Create instance
    VUIDUpdater vuidUpdater;
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
