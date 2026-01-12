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
