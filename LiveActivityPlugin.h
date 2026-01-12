#ifndef LIVEACTIVITYPLUGIN_H
#define LIVEACTIVITYPLUGIN_H

#include <QQmlExtensionPlugin>

class LiveActivityPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QQmlExtensionInterface")
    
public:
    void registerTypes(const char *uri) override;
};

#endif // LIVEACTIVITYPLUGIN_H
