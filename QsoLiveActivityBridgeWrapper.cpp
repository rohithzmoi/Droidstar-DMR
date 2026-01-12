/*
    Copyright (C) 2025 Rohith Namboothiri

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

#include "QsoLiveActivityBridgeWrapper.h"
#include <QtCore/QString>
#include <QtCore/QObject>

// Initialize static member
QsoLiveActivityBridge* QsoLiveActivityBridge::m_instance = nullptr;

// Forward declarations of the C interface functions
extern "C" {
    QsoLiveActivityBridge_t* qso_live_activity_bridge_create(void);
    void qso_live_activity_bridge_release(QsoLiveActivityBridge_t* bridge);
    void qso_live_activity_bridge_start_or_update(QsoLiveActivityBridge_t* bridge, 
                                                const char* callsign,
                                                const char* handle,
                                                const char* country);
    void qso_live_activity_bridge_end(QsoLiveActivityBridge_t* bridge);
    bool qso_live_activity_bridge_is_dynamic_island_available(void);
    void qso_live_activity_bridge_update_qso_details(QsoLiveActivityBridge_t* bridge,
                                                   const char* callsign,
                                                   const char* handle,
                                                   const char* country);
}

QsoLiveActivityBridge::QsoLiveActivityBridge(QObject *parent)
    : QObject(parent)
    , m_bridge(qso_live_activity_bridge_create())
{
}

QsoLiveActivityBridge::~QsoLiveActivityBridge()
{
    if (m_bridge) {
        qso_live_activity_bridge_release(m_bridge);
        m_bridge = nullptr;
    }
}

QsoLiveActivityBridge* QsoLiveActivityBridge::instance()
{
    if (!m_instance) {
        m_instance = new QsoLiveActivityBridge();
    }
    return m_instance;
}

void QsoLiveActivityBridge::startOrUpdateLiveActivity(const QString &callsign, 
                                                     const QString &handle, 
                                                     const QString &country)
{
    if (m_bridge) {
        qso_live_activity_bridge_start_or_update(m_bridge,
                                               callsign.toUtf8().constData(),
                                               handle.toUtf8().constData(),
                                               country.toUtf8().constData());
    }
}

void QsoLiveActivityBridge::endLiveActivity()
{
    if (m_bridge) {
        qso_live_activity_bridge_end(m_bridge);
    }
}

bool QsoLiveActivityBridge::isDynamicIslandAvailable() const
{
    return qso_live_activity_bridge_is_dynamic_island_available();
}

void QsoLiveActivityBridge::updateQsoDetails(const QString &callsign,
                                           const QString &handle,
                                           const QString &country)
{
    if (m_bridge) {
        qso_live_activity_bridge_update_qso_details(m_bridge,
                                                  callsign.toUtf8().constData(),
                                                  handle.toUtf8().constData(),
                                                  country.toUtf8().constData());
    }
}

QsoLiveActivityBridge* QsoLiveActivityBridge::instance() {
    if (!m_instance) {
        m_instance = new QsoLiveActivityBridge();
    }
    return m_instance;
}

void QsoLiveActivityBridge::startOrUpdateLiveActivity(const QString &callsign, 
                                                    const QString &handle, 
                                                    const QString &country) {
    if (m_bridge) {
        qso_live_activity_bridge_start_or_update_live_activity(
            m_bridge,
            callsign.toUtf8().constData(),
            handle.toUtf8().constData(),
            country.toUtf8().constData()
        );
    }
}

void QsoLiveActivityBridge::endLiveActivity() {
    if (m_bridge) {
        qso_live_activity_bridge_end_live_activity(m_bridge);
    }
}

bool QsoLiveActivityBridge::isDynamicIslandAvailable() const {
    return m_bridge ? qso_live_activity_bridge_is_dynamic_island_available() : false;
}

void QsoLiveActivityBridge::updateQsoDetails(const QString &callsign, 
                                           const QString &handle, 
                                           const QString &country) {
    if (m_bridge) {
        qso_live_activity_bridge_update_qso_details(
            m_bridge,
            callsign.toUtf8().constData(),
            handle.toUtf8().constData(),
            country.toUtf8().constData()
        );
    }
}
