#include "LiveActivityQtBridge.h"

#include "ios_live_activity.h"

LiveActivityQtBridge::LiveActivityQtBridge(QObject *parent)
    : QObject(parent)
{
}

bool LiveActivityQtBridge::available() const
{
    return ios_live_activity_is_available();
}

void LiveActivityQtBridge::startOrUpdate(const QString &mode,
                                        const QString &callsign,
                                        const QString &handle,
                                        const QString &country,
                                        const QString &tgid)
{
    ios_live_activity_start_or_update(mode.toUtf8().constData(),
                                      callsign.toUtf8().constData(),
                                      handle.toUtf8().constData(),
                                      country.toUtf8().constData(),
                                      tgid.toUtf8().constData());
}

void LiveActivityQtBridge::end()
{
    ios_live_activity_end();
}

void LiveActivityQtBridge::endAll()
{
    ios_live_activity_end_all();
}

