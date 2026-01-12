#include "LiveActivityPlugin.h"
#include "QsoLiveActivityBridgeWrapper.h"
#include <qqml.h>

void LiveActivityPlugin::registerTypes(const char *uri)
{
    // @uri DroidStar.LiveActivity
    qmlRegisterType<QsoLiveActivityBridge>(uri, 1, 0, "LiveActivityBridge");
    
    // Also register the singleton instance
    qmlRegisterSingletonInstance(uri, 1, 0, "LiveActivity", QsoLiveActivityBridge::instance());
}
