/*
    Copyright (C) 2026 Rohith Namboothiri

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

// Hardware PTT buttons: headset media button, Bluetooth LE buttons (iOS CoreBluetooth) and
// keyboard-type HID buttons. Lives on the GUI thread; QML applies the resulting TX edges.

#ifndef PTTBUTTONMANAGER_H
#define PTTBUTTONMANAGER_H

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "pttclassifier.h"

class PttButtonManager;

// Catches hardware keyboard presses (keyboard-type Bluetooth PTT buttons). Foreground only on iOS.
class PttKeyEventFilter : public QObject
{
    Q_OBJECT
public:
    explicit PttKeyEventFilter(PttButtonManager *owner);
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    PttButtonManager *m_owner;
};

class PttButtonManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList scanResults READ scanResults NOTIFY scanResultsChanged)
    Q_PROPERTY(bool mediaButtonEnabled READ mediaButtonEnabled WRITE setMediaButtonEnabled NOTIFY mediaButtonEnabledChanged)
    Q_PROPERTY(bool mediaTrackButtonsEnabled READ mediaTrackButtonsEnabled WRITE setMediaTrackButtonsEnabled NOTIFY mediaTrackButtonsEnabledChanged)
    Q_PROPERTY(bool holdToTalkMode READ holdToTalkMode WRITE setHoldToTalkMode NOTIFY holdToTalkModeChanged)
    Q_PROPERTY(bool holdActive READ holdActive NOTIFY holdActiveChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString bluetoothState READ bluetoothState NOTIFY bluetoothStateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString learnState READ learnState NOTIFY learnStateChanged)
    Q_PROPERTY(QVariantMap learnResult READ learnResult NOTIFY learnStateChanged)
    Q_PROPERTY(QString learnProgress READ learnProgress NOTIFY learnProgressChanged)
    Q_PROPERTY(QString lastEventText READ lastEventText NOTIFY lastEventChanged)
    Q_PROPERTY(bool bleSupported READ bleSupported CONSTANT)
    Q_PROPERTY(bool keyLearning READ keyLearning NOTIFY keyLearningChanged)
    Q_PROPERTY(int holdSafetySeconds READ holdSafetySeconds WRITE setHoldSafetySeconds NOTIFY holdSafetySecondsChanged)
    Q_PROPERTY(int deviceCount READ deviceCount NOTIFY devicesChanged)

public:
    explicit PttButtonManager(QObject *parent = nullptr);
    ~PttButtonManager() override;

    QVariantList devices() const { return m_deviceModel; }
    QVariantList scanResults() const { return m_scanModel; }
    bool mediaButtonEnabled() const { return m_mediaEnabled; }
    void setMediaButtonEnabled(bool on);
    bool mediaTrackButtonsEnabled() const { return m_mediaTrackButtons; }
    void setMediaTrackButtonsEnabled(bool on);
    bool holdToTalkMode() const { return m_holdToTalkMode; }
    void setHoldToTalkMode(bool hold);
    bool holdActive() const { return m_holdActive; }
    bool scanning() const { return m_scanning; }
    QString bluetoothState() const { return m_bluetoothState; }
    QString statusText() const { return m_statusText; }
    QString learnState() const { return m_learnState; }
    QVariantMap learnResult() const { return m_learnResult; }
    QString learnProgress() const { return m_learnProgress; }
    QString lastEventText() const { return m_lastEventText; }
    bool bleSupported() const;
    bool keyLearning() const { return m_keyLearning; }
    int holdSafetySeconds() const { return m_holdSafetySeconds; }
    void setHoldSafetySeconds(int s);
    int deviceCount() const { return static_cast<int>(m_devices.size()); }

    // Registry
    Q_INVOKABLE void setDeviceEnabled(const QString &id, bool on);
    Q_INVOKABLE void setDeviceBehaviour(const QString &id, const QString &behaviour); // auto|hold|toggle
    Q_INVOKABLE void renameDevice(const QString &id, const QString &name);
    Q_INVOKABLE void forgetDevice(const QString &id);
    Q_INVOKABLE void recordAutoSwitchDecision(const QString &id, bool accepted);
    Q_INVOKABLE QVariantMap deviceInfo(const QString &id) const;

    // Add flow (Bluetooth LE)
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void addScannedDevice(const QString &identifier);
    Q_INVOKABLE void cancelAdd();
    Q_INVOKABLE void relearnDevice(const QString &id);
    Q_INVOKABLE void finishAdd();           // closes a "done"/"failed" learn state

    // Add flow (keyboard-type HID)
    Q_INVOKABLE void startKeyLearning();
    Q_INVOKABLE void cancelKeyLearning();

    // Safety hooks from QML
    Q_INVOKABLE void resetAll();            // reflector disconnected / TX no longer possible
    Q_INVOKABLE void openBluetoothSettings();

    // Called by the key event filter (GUI thread). Returns true when the event was consumed.
    bool handleKeyEvent(int key, int modifiers, bool down, bool autoRepeat);

signals:
    void holdActiveChanged(bool active);
    void clicked(const QString &id);
    void stopRequested();
    void safetyReleased(const QString &reason);
    void suggestToggleTxMode(const QString &id, const QString &name);
    void devicesChanged();
    void scanResultsChanged();
    void scanningChanged();
    void bluetoothStateChanged();
    void statusTextChanged();
    void learnStateChanged();
    void learnProgressChanged();
    void lastEventChanged();
    void mediaButtonEnabledChanged();
    void mediaTrackButtonsEnabledChanged();
    void holdToTalkModeChanged();
    void keyLearningChanged();
    void holdSafetySecondsChanged();
    void deviceAdded(const QString &id, const QString &name);

public slots:
    // Backends (always invoked on the GUI thread via queued connection)
    void onMediaCommand(int kind);
    void onIntentCommand(int kind);   // Action Button / Siri / Shortcuts: 0 toggle, 1 stop
    void onBleState(int state);
    void onBleScanResult(const QString &id, const QString &name, int rssi, const QStringList &services, const QByteArray &mfg);
    void onBleConnected(const QString &id, const QString &name);
    void onBleServicesReady(const QString &id, const QStringList &channels);
    void onBleConnectFailed(const QString &id, int err);
    void onBleDisconnected(const QString &id, int err);
    void onBleNotification(const QString &id, const QString &channel, const QByteArray &value);

private slots:
    void onHoldSafetyTimeout();
    void onScanTimeout();
    void onLearnTimeout();
    void onConnectTimeout();
    void onApplicationStateChanged(Qt::ApplicationState state);
    void flushScanModel();
    void saveNow();

private:
    enum class Conn { Off, Waiting, Connecting, Discovering, Ready, Unavailable };

    struct Device {
        QString id;
        QString name;
        ptt::Kind kind = ptt::Kind::Ble;
        ptt::Mapping mapping;
        ptt::Behaviour behaviour = ptt::Behaviour::Auto;
        bool enabled = true;
        ptt::AutoSwitch autoSwitch = ptt::AutoSwitch::Unset;
        QString signature;        // "anytone-elet" | "zello-ffe1" | "learned"
        QString manufacturerHex;
        QDateTime addedAt;
        QDateTime lastSeen;
        QDateTime lastEvent;
        bool tentative = false;
        // runtime (not persisted)
        Conn conn = Conn::Off;
        ptt::DeviceRuntime rt;
    };

    struct ScanEntry {
        QString id;
        QString name;
        int rssi = -100;
        QStringList services;
        QByteArray mfg;
        QString signatureId;
        QString signatureName;
        qint64 lastSeenMs = 0;
    };

    struct AddFlow {
        bool active = false;
        bool learning = false;
        bool relearn = false;     // device already saved; only the mapping is refreshed
        QString id;
        QString name;
        QByteArray mfg;
        QStringList advServices;
        QString advSignature;
    };

    Device *findDevice(const QString &id);
    const Device *findDevice(const QString &id) const;
    Device *findHidDevice(int key, int mods);

    void handleAction(Device *d, const QString &id, const QString &name, ptt::DeviceRuntime::Action a);
    void setHoldActive(bool on);
    bool takeToggleSuggestion(Device *d, const QString &id);   // true when the UI should be told
    void applyMediaTrackButtons();
    void noteEvent(const QString &text);

    void load();
    void scheduleSave();
    QString storagePath() const;

    void ensureBleCentral();
    void connectSavedBleDevices();
    void updateBleAvailability();
    void bleConnect(const QString &id, bool keepAlive);
    void bleSubscribeMapping(const Device &d);

    void setLearnState(const QString &state, const QVariantMap &result = QVariantMap());
    void setLearnProgress(const QString &text);
    void failAdd(const QString &reason);
    void completeAdd(const ptt::Mapping &mapping, const QString &signature, bool tentative);
    void setScanning(bool on);

    void rebuildModel();
    void updateStatusText();
    QString connLabel(const Device &d) const;
    QString connKey(const Device &d) const;

    QList<Device> m_devices;
    QVariantList m_deviceModel;

    // Headset media button ("device" that cannot be removed)
    bool m_mediaEnabled = true;
    bool m_mediaTrackButtons = false;
    ptt::DeviceRuntime m_intentRt;
    ptt::AutoSwitch m_mediaAutoSwitch = ptt::AutoSwitch::Unset;
    ptt::DeviceRuntime m_mediaRt;
    ptt::Mapping m_mediaMapping;

    ptt::Arbiter m_arbiter;
    bool m_holdActive = false;
    bool m_holdToTalkMode = false;
    int m_holdSafetySeconds = 180;

    QString m_bluetoothState = QStringLiteral("unknown");
    QString m_statusText;
    QString m_lastEventText;

    bool m_scanning = false;
    QHash<QString, ScanEntry> m_scanEntries;
    QVariantList m_scanModel;
    QTimer m_scanFlushTimer;

    AddFlow m_add;
    ptt::Learner m_learner;
    QString m_learnState = QStringLiteral("idle");
    QVariantMap m_learnResult;
    QString m_learnProgress;

    bool m_keyLearning = false;
    int m_keyLearnKey = 0;
    int m_keyLearnMods = 0;

    QTimer m_holdSafetyTimer;
    QTimer m_scanTimer;
    QTimer m_learnTimer;
    QTimer m_connectTimer;
    QTimer m_saveTimer;

    PttKeyEventFilter *m_keyFilter = nullptr;
    bool m_loaded = false;
    bool m_centralRequested = false;
};

#endif // PTTBUTTONMANAGER_H
