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

#ifndef QSOLIVEACTIVITYBRIDGE_H
#define QSOLIVEACTIVITYBRIDGE_H

#include <QObject>
#include <QString>

// Forward declaration of the C bridge type
typedef struct QsoLiveActivityBridge_t QsoLiveActivityBridge_t;

class QsoLiveActivityBridge : public QObject
{
    Q_OBJECT
    
public:
    explicit QsoLiveActivityBridge(QObject *parent = nullptr);
    ~QsoLiveActivityBridge();
    
    // Singleton access
    static QsoLiveActivityBridge* instance();
    
    // Public API
    Q_INVOKABLE void startOrUpdateLiveActivity(const QString &callsign, 
                                             const QString &handle = QString(),
                                             const QString &country = QString());
    Q_INVOKABLE void endLiveActivity();
    Q_INVOKABLE bool isDynamicIslandAvailable() const;
    
    // Update QSO details in the live activity
    Q_INVOKABLE void updateQsoDetails(const QString &callsign,
                                    const QString &handle = QString(),
                                    const QString &country = QString());
    
private:
    // The C bridge instance
    QsoLiveActivityBridge_t *m_bridge;
    
    // Singleton instance
    static QsoLiveActivityBridge *m_instance;
};

#endif // QSOLIVEACTIVITYBRIDGE_H
