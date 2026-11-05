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

#include "pttbuttonmanager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QTime>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <atomic>

#ifdef Q_OS_IOS
#include "AudioSessionManager.h"
#include "ios_ble_ptt.h"
#endif

namespace {

std::atomic<PttButtonManager *> s_instance{nullptr};

QElapsedTimer &monotonic()
{
    static QElapsedTimer t = [] { QElapsedTimer x; x.start(); return x; }();
    return t;
}

uint64_t nowMs() { return static_cast<uint64_t>(monotonic().elapsed()); }

QString qs(const std::string &s) { return QString::fromStdString(s); }

ptt::Bytes toBytes(const QByteArray &b)
{
    return ptt::Bytes(reinterpret_cast<const uint8_t *>(b.constData()),
                      reinterpret_cast<const uint8_t *>(b.constData()) + b.size());
}

const char *kBleStateNames[] = {"unknown", "resetting", "unsupported", "unauthorized", "poweredOff", "poweredOn"};

QString bleStateName(int st)
{
    if (st < 0 || st > 5) return QStringLiteral("unknown");
    return QString::fromLatin1(kBleStateNames[st]);
}

#ifdef Q_OS_IOS
// ---- native callbacks: copy everything, hop to the GUI thread ----

template <typename F>
void post(F &&fn)
{
    PttButtonManager *inst = s_instance.load();
    if (!inst) return;
    // The receiver is the context object: Qt drops the call if the manager is gone before it runs.
    QMetaObject::invokeMethod(inst, std::forward<F>(fn), Qt::QueuedConnection);
}

void cb_media(int kind)
{
    post([kind]() { if (auto *m = s_instance.load()) m->onMediaCommand(kind); });
}

void cb_state(int state)
{
    post([state]() { if (auto *m = s_instance.load()) m->onBleState(state); });
}

void cb_scan(const char *identifier, const char *name, int rssi, const char *services, const uint8_t *mfg, int mfgLen)
{
    const QString id = QString::fromUtf8(identifier ? identifier : "");
    const QString nm = QString::fromUtf8(name ? name : "");
    const QStringList svc = QString::fromUtf8(services ? services : "").split(',', Qt::SkipEmptyParts);
    const QByteArray mf = (mfg && mfgLen > 0) ? QByteArray(reinterpret_cast<const char *>(mfg), mfgLen) : QByteArray();
    post([=]() { if (auto *m = s_instance.load()) m->onBleScanResult(id, nm, rssi, svc, mf); });
}

void cb_connected(const char *identifier, const char *name)
{
    const QString id = QString::fromUtf8(identifier ? identifier : "");
    const QString nm = QString::fromUtf8(name ? name : "");
    post([=]() { if (auto *m = s_instance.load()) m->onBleConnected(id, nm); });
}

void cb_services(const char *identifier, const char *channels)
{
    const QString id = QString::fromUtf8(identifier ? identifier : "");
    const QStringList ch = QString::fromUtf8(channels ? channels : "").split(',', Qt::SkipEmptyParts);
    post([=]() { if (auto *m = s_instance.load()) m->onBleServicesReady(id, ch); });
}

void cb_connect_failed(const char *identifier, int err)
{
    const QString id = QString::fromUtf8(identifier ? identifier : "");
    post([=]() { if (auto *m = s_instance.load()) m->onBleConnectFailed(id, err); });
}

void cb_disconnected(const char *identifier, int err)
{
    const QString id = QString::fromUtf8(identifier ? identifier : "");
    post([=]() { if (auto *m = s_instance.load()) m->onBleDisconnected(id, err); });
}

void cb_notification(const char *identifier, const char *svc, const char *chr, const uint8_t *bytes, int len)
{
    const QString id = QString::fromUtf8(identifier ? identifier : "");
    const QString channel = qs(ptt::makeChannel(svc ? svc : "", chr ? chr : ""));
    const QByteArray value = (bytes && len > 0) ? QByteArray(reinterpret_cast<const char *>(bytes), len) : QByteArray();
    post([=]() { if (auto *m = s_instance.load()) m->onBleNotification(id, channel, value); });
}
#endif

bool isRejectedKey(int key)
{
    switch (key) {
    case Qt::Key_unknown:
    case Qt::Key_VolumeUp:
    case Qt::Key_VolumeDown:
    case Qt::Key_VolumeMute:
    case Qt::Key_PowerOff:
    case Qt::Key_PowerDown:
    case Qt::Key_Sleep:
    case Qt::Key_WakeUp:
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
    case Qt::Key_Back:
    case Qt::Key_Menu:
        return true;
    default:
        return false;
    }
}

QString keyName(int key, int mods)
{
    QString name = QKeySequence(key).toString(QKeySequence::NativeText);
    if (name.isEmpty()) name = QStringLiteral("Key %1").arg(key);
    QStringList parts;
    if (mods & Qt::ControlModifier) parts << QStringLiteral("Ctrl");
    if (mods & Qt::AltModifier) parts << QStringLiteral("Alt");
    if (mods & Qt::ShiftModifier) parts << QStringLiteral("Shift");
    if (mods & Qt::MetaModifier) parts << QStringLiteral("Cmd");
    parts << name;
    return parts.join(QStringLiteral("+"));
}

int keyModsMask(int mods)
{
    return mods & (Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier | Qt::MetaModifier);
}

} // namespace

// ============================================================ App Intents entry (Swift)

extern "C" void ptt_intent_event(int kind)
{
    PttButtonManager *inst = s_instance.load();
    if (!inst) return;
    QMetaObject::invokeMethod(inst, [kind]() { if (auto *m = s_instance.load()) m->onIntentCommand(kind); }, Qt::QueuedConnection);
}

// ============================================================ PttKeyEventFilter

PttKeyEventFilter::PttKeyEventFilter(PttButtonManager *owner)
    : QObject(owner), m_owner(owner)
{
}

bool PttKeyEventFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_owner) return false;
    const QEvent::Type t = event->type();
    if (t != QEvent::KeyPress && t != QEvent::KeyRelease) return false;
    // QQuickWindow re-dispatches the same QKeyEvent to focus items: act on the window only.
    if (!watched || !watched->isWindowType()) return false;
    auto *ke = static_cast<QKeyEvent *>(event);
    return m_owner->handleKeyEvent(ke->key(), static_cast<int>(ke->modifiers()), t == QEvent::KeyPress, ke->isAutoRepeat());
}

// ============================================================ PttButtonManager

PttButtonManager::PttButtonManager(QObject *parent)
    : QObject(parent)
{
    s_instance.store(this);

    m_mediaMapping.channel = "media";
    m_mediaMapping.match = ptt::MatchMode::AnyValue;
    m_mediaMapping.capability = ptt::Capability::Click;

    m_holdSafetyTimer.setSingleShot(true);
    connect(&m_holdSafetyTimer, &QTimer::timeout, this, &PttButtonManager::onHoldSafetyTimeout);
    m_scanTimer.setSingleShot(true);
    m_scanTimer.setInterval(30000);
    connect(&m_scanTimer, &QTimer::timeout, this, &PttButtonManager::onScanTimeout);
    m_learnTimer.setSingleShot(true);
    m_learnTimer.setInterval(15000);
    connect(&m_learnTimer, &QTimer::timeout, this, &PttButtonManager::onLearnTimeout);
    m_connectTimer.setSingleShot(true);
    m_connectTimer.setInterval(20000);
    connect(&m_connectTimer, &QTimer::timeout, this, &PttButtonManager::onConnectTimeout);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &PttButtonManager::saveNow);
    m_scanFlushTimer.setSingleShot(true);
    m_scanFlushTimer.setInterval(250);
    connect(&m_scanFlushTimer, &QTimer::timeout, this, &PttButtonManager::flushScanModel);

    load();

    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        connect(app, &QGuiApplication::applicationStateChanged, this, &PttButtonManager::onApplicationStateChanged);
        m_keyFilter = new PttKeyEventFilter(this);
        app->installEventFilter(m_keyFilter);
    }

#ifdef Q_OS_IOS
    DSBlePttCallbacks cbs;
    cbs.state_changed = cb_state;
    cbs.scan_result = cb_scan;
    cbs.connected = cb_connected;
    cbs.services_ready = cb_services;
    cbs.connect_failed = cb_connect_failed;
    cbs.disconnected = cb_disconnected;
    cbs.notification = cb_notification;
    ios_ble_ptt_init(&cbs);
    setMediaButtonCallback(cb_media);
    applyMediaTrackButtons();

    // Permission was granted when the first button was added, so creating the central at launch
    // is silent and arms the pending connections that let a sleeping button wake us.
    bool anyBle = false;
    for (const Device &d : m_devices) if (d.kind == ptt::Kind::Ble && d.enabled) anyBle = true;
    if (anyBle) ensureBleCentral();
#else
    m_bluetoothState = QStringLiteral("unsupported");
#endif

    rebuildModel();
    updateStatusText();
}

PttButtonManager::~PttButtonManager()
{
    m_holdSafetyTimer.stop();
    m_scanTimer.stop();
    m_learnTimer.stop();
    m_connectTimer.stop();
    m_scanFlushTimer.stop();
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        saveNow();
    }
#ifdef Q_OS_IOS
    ios_ble_ptt_shutdown();              // synchronous: no native callback can fire after this
    setMediaButtonCallback(nullptr);
#endif
    if (m_keyFilter) {
        if (auto *app = QCoreApplication::instance()) app->removeEventFilter(m_keyFilter);
        m_keyFilter = nullptr;             // child of this; deleted with us
    }
    s_instance.store(nullptr);
}

bool PttButtonManager::bleSupported() const
{
#ifdef Q_OS_IOS
    return true;
#else
    return false;
#endif
}

// ------------------------------------------------------------ lookup

PttButtonManager::Device *PttButtonManager::findDevice(const QString &id)
{
    for (Device &d : m_devices) if (d.id == id) return &d;
    return nullptr;
}

const PttButtonManager::Device *PttButtonManager::findDevice(const QString &id) const
{
    for (const Device &d : m_devices) if (d.id == id) return &d;
    return nullptr;
}

PttButtonManager::Device *PttButtonManager::findHidDevice(int key, int mods)
{
    const QString id = QStringLiteral("hid:%1:%2").arg(key).arg(keyModsMask(mods));
    return findDevice(id);
}

// ------------------------------------------------------------ properties

void PttButtonManager::setMediaButtonEnabled(bool on)
{
    if (m_mediaEnabled == on) return;
    m_mediaEnabled = on;
    applyMediaTrackButtons();
    emit mediaButtonEnabledChanged();
    updateStatusText();
    scheduleSave();
}

void PttButtonManager::setHoldToTalkMode(bool hold)
{
    if (m_holdToTalkMode == hold) return;
    m_holdToTalkMode = hold;
    emit holdToTalkModeChanged();
    // Switching to Toggle while a hardware hold is active: drop the hold so the UI mode wins.
    if (!hold && m_arbiter.anyPressed()) resetAll();
    rebuildModel();
}

void PttButtonManager::setHoldSafetySeconds(int s)
{
    s = qBound(10, s, 900);
    if (m_holdSafetySeconds == s) return;
    m_holdSafetySeconds = s;
    emit holdSafetySecondsChanged();
    scheduleSave();
}

// ------------------------------------------------------------ registry

void PttButtonManager::setDeviceEnabled(const QString &id, bool on)
{
    Device *d = findDevice(id);
    if (!d || d->enabled == on) return;
    d->enabled = on;
    if (!on) {
        handleAction(d, d->id, d->name, d->rt.forceRelease());
    }
#ifdef Q_OS_IOS
    if (d->kind == ptt::Kind::Ble) {
        if (on) {
            ensureBleCentral();
            d->conn = Conn::Waiting;
            bleConnect(d->id, true);
        } else {
            ios_ble_ptt_disconnect(d->id.toUtf8().constData());
            d->conn = Conn::Off;
        }
    }
#endif
    rebuildModel();
    updateStatusText();
    scheduleSave();
}

void PttButtonManager::setDeviceBehaviour(const QString &id, const QString &behaviour)
{
    Device *d = findDevice(id);
    if (!d) return;
    const ptt::Behaviour b = ptt::behaviourFromString(behaviour.toStdString());
    if (d->behaviour == b) return;
    d->behaviour = b;
    if (d->rt.pressed()) handleAction(d, d->id, d->name, d->rt.forceRelease());
    rebuildModel();
    scheduleSave();
}

void PttButtonManager::renameDevice(const QString &id, const QString &name)
{
    Device *d = findDevice(id);
    if (!d) return;
    const QString n = name.trimmed().left(48);
    if (n.isEmpty() || d->name == n) return;
    d->name = n;
    rebuildModel();
    scheduleSave();
}

void PttButtonManager::forgetDevice(const QString &id)
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices[i].id != id) continue;
        Device &d = m_devices[i];
        handleAction(&d, d.id, d.name, d.rt.forceRelease());
        if (m_add.active && m_add.id == id) cancelAdd();
#ifdef Q_OS_IOS
        if (d.kind == ptt::Kind::Ble) ios_ble_ptt_forget(id.toUtf8().constData());
#endif
        m_devices.removeAt(i);
        rebuildModel();
        updateStatusText();
        scheduleSave();
        return;
    }
}

void PttButtonManager::recordAutoSwitchDecision(const QString &id, bool accepted)
{
    const ptt::AutoSwitch v = accepted ? ptt::AutoSwitch::Accepted : ptt::AutoSwitch::Reverted;
    if (id == QLatin1String("media") || id == QLatin1String("action")) {
        m_mediaAutoSwitch = v;
    } else if (Device *d = findDevice(id)) {
        d->autoSwitch = v;
    } else {
        return;
    }
    scheduleSave();
}

QVariantMap PttButtonManager::deviceInfo(const QString &id) const
{
    for (const QVariant &v : m_deviceModel) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == id) return m;
    }
    return QVariantMap();
}

// ------------------------------------------------------------ TX arbitration

void PttButtonManager::setHoldActive(bool on)
{
    if (m_holdActive == on) return;
    m_holdActive = on;
    emit holdActiveChanged(on);
}

void PttButtonManager::handleAction(Device *d, const QString &id, const QString &name, ptt::DeviceRuntime::Action a)
{
    using Action = ptt::DeviceRuntime::Action;
    if (a == Action::None) return;
    if (d) d->lastEvent = QDateTime::currentDateTime();
    const std::string sid = id.toStdString();

    switch (a) {
    case Action::Pressed:
        noteEvent(tr("%1: pressed").arg(name));
        if (m_arbiter.press(sid)) {
            setHoldActive(true);
            m_holdSafetyTimer.start(m_holdSafetySeconds * 1000);
        }
        break;
    case Action::Released:
        noteEvent(tr("%1: released").arg(name));
        if (m_arbiter.release(sid)) {
            setHoldActive(false);
            m_holdSafetyTimer.stop();
        }
        break;
    case Action::RekeyPressed:
        noteEvent(tr("%1: pressed again (release was lost)").arg(name));
        if (m_arbiter.release(sid)) {
            setHoldActive(false);
            m_holdSafetyTimer.stop();
        }
        if (m_arbiter.press(sid)) {
            setHoldActive(true);
            m_holdSafetyTimer.start(m_holdSafetySeconds * 1000);
        }
        break;
    case Action::Clicked: {
        noteEvent(tr("%1: click").arg(name));
        if (m_arbiter.anyPressed()) {
            qDebug() << "[PttButton] click ignored while a hold is active:" << id;
            break;
        }
        // Decide before emitting: a QML handler may change the registry while it runs.
        const bool suggest = takeToggleSuggestion(d, id);
        emit clicked(id);
        if (suggest) emit suggestToggleTxMode(id, name);
        break;
    }
    case Action::None:
        break;
    }
}

bool PttButtonManager::takeToggleSuggestion(Device *d, const QString &id)
{
    if (!m_holdToTalkMode) return false;
    ptt::AutoSwitch *slot = nullptr;
    ptt::Capability cap = ptt::Capability::Click;
    if (d) {
        slot = &d->autoSwitch;
        cap = d->mapping.capability;
    } else if (id == QLatin1String("media") || id == QLatin1String("action")) {
        slot = &m_mediaAutoSwitch;
    }
    if (!slot || cap == ptt::Capability::Hold) return false;
    if (*slot != ptt::AutoSwitch::Unset) return false;
    // Persist first so a missed toast never leads to a second prompt.
    *slot = ptt::AutoSwitch::Accepted;
    scheduleSave();
    return true;
}

void PttButtonManager::resetAll()
{
    for (Device &d : m_devices) d.rt.reset();
    m_mediaRt.reset();
    m_arbiter.releaseAll();
    m_holdSafetyTimer.stop();
    setHoldActive(false);
}

void PttButtonManager::onHoldSafetyTimeout()
{
    if (!m_arbiter.anyPressed()) return;
    for (Device &d : m_devices) d.rt.forceRelease();
    m_arbiter.releaseAll();
    setHoldActive(false);
    emit safetyReleased(tr("PTT button held for %1 s, transmission stopped").arg(m_holdSafetySeconds));
}

void PttButtonManager::noteEvent(const QString &text)
{
    m_lastEventText = QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + text;
    emit lastEventChanged();
}

// ------------------------------------------------------------ media backend

void PttButtonManager::onMediaCommand(int kind)
{
    if (kind == 1) {
        // Stop: always honoured, even with the media button disabled, as a safety release.
        noteEvent(tr("Headset: stop"));
        const bool wasHeld = m_arbiter.anyPressed();
        for (Device &d : m_devices) d.rt.forceRelease();
        m_arbiter.releaseAll();
        m_holdSafetyTimer.stop();
        setHoldActive(false);
        if (!wasHeld) emit stopRequested();
        return;
    }
    if (!m_mediaEnabled) return;
    ptt::RawEvent ev{nowMs(), "media", {0}};
    const auto a = m_mediaRt.feed(ev, m_mediaMapping, ptt::Effective::Toggle, ev.tMs);
    handleAction(nullptr, QStringLiteral("media"), tr("Headset button"), a);
}

void PttButtonManager::onIntentCommand(int kind)
{
    if (kind == 1) {
        onMediaCommand(1);
        return;
    }
    ptt::RawEvent ev{nowMs(), "media", {0}};
    const auto a = m_intentRt.feed(ev, m_mediaMapping, ptt::Effective::Toggle, ev.tMs);
    handleAction(nullptr, QStringLiteral("action"), tr("Action Button"), a);
}

void PttButtonManager::setMediaTrackButtonsEnabled(bool on)
{
    if (m_mediaTrackButtons == on) return;
    m_mediaTrackButtons = on;
    applyMediaTrackButtons();
    emit mediaTrackButtonsEnabledChanged();
    scheduleSave();
}

void PttButtonManager::applyMediaTrackButtons()
{
#ifdef Q_OS_IOS
    setMediaTrackButtonsEnabled(m_mediaTrackButtons && m_mediaEnabled);
#endif
}

// ------------------------------------------------------------ HID backend

bool PttButtonManager::handleKeyEvent(int key, int modifiers, bool down, bool autoRepeat)
{
    const int mods = keyModsMask(modifiers);

    if (m_keyLearning) {
        if (isRejectedKey(key)) return false;
        if (autoRepeat) return true;
        if (down) {
            if (m_keyLearnKey == 0) {
                m_keyLearnKey = key;
                m_keyLearnMods = mods;
                setLearnProgress(tr("Got %1, now release it").arg(keyName(key, mods)));
            }
            return true;
        }
        if (m_keyLearnKey != key) return true;
        // Release of the learned key completes the flow.
        const QString id = QStringLiteral("hid:%1:%2").arg(key).arg(mods);
        m_keyLearning = false;
        emit keyLearningChanged();
        Device *existing = findDevice(id);
        bool isNew = false;
        if (!existing) {
            Device d;
            d.id = id;
            d.kind = ptt::Kind::Hid;
            d.name = tr("Keyboard button (%1)").arg(keyName(key, mods));
            d.mapping.channel = QStringLiteral("key:%1:%2").arg(key).arg(mods).toStdString();
            d.mapping.match = ptt::MatchMode::ExactValue;
            d.mapping.press = {1};
            d.mapping.release = {0};
            d.mapping.capability = ptt::Capability::Hold;
            d.signature = QStringLiteral("hid");
            d.addedAt = QDateTime::currentDateTime();
            d.conn = Conn::Ready;
            m_devices.append(d);
            existing = &m_devices.last();
            isNew = true;
        }
        const QString savedName = existing->name;
        const QString channel = qs(existing->mapping.channel);
        existing = nullptr;
        QVariantMap r;
        r[QStringLiteral("ok")] = true;
        r[QStringLiteral("kind")] = QStringLiteral("hid");
        r[QStringLiteral("deviceId")] = id;
        r[QStringLiteral("name")] = savedName;
        r[QStringLiteral("capability")] = QStringLiteral("hold");
        r[QStringLiteral("channel")] = channel;
        r[QStringLiteral("tentative")] = false;
        m_keyLearnKey = 0;
        m_keyLearnMods = 0;
        rebuildModel();
        updateStatusText();
        scheduleSave();
        setLearnState(QStringLiteral("done"), r);
        if (isNew) emit deviceAdded(id, savedName);
        return true;
    }

    Device *d = findHidDevice(key, mods);
    if (!d || !d->enabled) return false;
    if (autoRepeat) return true;   // swallow repeats so the key never types
    ptt::RawEvent ev{nowMs(), d->mapping.channel, {static_cast<uint8_t>(down ? 1 : 0)}};
    const ptt::Effective eff = ptt::resolveBehaviour(d->mapping.capability, d->behaviour, !m_holdToTalkMode);
    handleAction(d, d->id, d->name, d->rt.feed(ev, d->mapping, eff, ev.tMs));
    return true;
}

void PttButtonManager::startKeyLearning()
{
    if (m_add.active) cancelAdd();
    m_keyLearning = true;
    m_keyLearnKey = 0;
    m_keyLearnMods = 0;
    emit keyLearningChanged();
    setLearnProgress(tr("Press and release the button you want to use"));
    setLearnState(QStringLiteral("learning"));
}

void PttButtonManager::cancelKeyLearning()
{
    if (!m_keyLearning) return;
    m_keyLearning = false;
    m_keyLearnKey = 0;
    m_keyLearnMods = 0;
    emit keyLearningChanged();
    setLearnState(QStringLiteral("idle"));
}

void PttButtonManager::onApplicationStateChanged(Qt::ApplicationState state)
{
    if (state == Qt::ApplicationActive) return;
    // iOS delivers no key events to a backgrounded app: never leave a keyboard hold keyed.
    for (Device &d : m_devices) {
        if (d.kind == ptt::Kind::Hid && d.rt.pressed()) handleAction(&d, d.id, d.name, d.rt.forceRelease());
    }
    if (m_keyLearning) cancelKeyLearning();
}

// ------------------------------------------------------------ BLE backend

void PttButtonManager::ensureBleCentral()
{
#ifdef Q_OS_IOS
    m_centralRequested = true;
    ios_ble_ptt_ensure_central();
#endif
}

void PttButtonManager::bleConnect(const QString &id, bool keepAlive)
{
#ifdef Q_OS_IOS
    ios_ble_ptt_connect(id.toUtf8().constData(), keepAlive);
#else
    Q_UNUSED(id); Q_UNUSED(keepAlive);
#endif
}

void PttButtonManager::bleSubscribeMapping(const Device &d)
{
#ifdef Q_OS_IOS
    const QString ch = qs(d.mapping.channel);
    const qsizetype sep = ch.indexOf(':');
    if (sep <= 0) return;
    const QByteArray svc = ch.left(sep).toUtf8();
    const QByteArray chr = ch.mid(sep + 1).toUtf8();
    const QByteArray id = d.id.toUtf8();
    ios_ble_ptt_subscribe(id.constData(), svc.constData(), chr.constData());
    // A button that is still held after a wake-up reconnect reports its state on read.
    ios_ble_ptt_read(id.constData(), svc.constData(), chr.constData());
#else
    Q_UNUSED(d);
#endif
}

void PttButtonManager::connectSavedBleDevices()
{
    for (Device &d : m_devices) {
        if (d.kind != ptt::Kind::Ble || !d.enabled) continue;
        d.conn = Conn::Waiting;
        bleConnect(d.id, true);
    }
}

void PttButtonManager::updateBleAvailability()
{
    const bool on = (m_bluetoothState == QLatin1String("poweredOn"));
    for (Device &d : m_devices) {
        if (d.kind != ptt::Kind::Ble) continue;
        if (!on) {
            handleAction(&d, d.id, d.name, d.rt.forceRelease());
            d.conn = d.enabled ? Conn::Unavailable : Conn::Off;
        }
    }
    if (on) connectSavedBleDevices();
}

void PttButtonManager::onBleState(int state)
{
    const QString name = bleStateName(state);
    if (name == m_bluetoothState) return;
    m_bluetoothState = name;
    emit bluetoothStateChanged();
    if (name != QLatin1String("poweredOn")) {
        if (m_scanning) setScanning(false);
        if (m_add.active) {
            failAdd(name == QLatin1String("unauthorized") ? tr("Allow Bluetooth for DroidStar in Settings.")
                  : name == QLatin1String("unsupported") ? tr("Bluetooth LE is not available on this device.")
                  : tr("Bluetooth is off."));
        }
    }
    updateBleAvailability();
#ifdef Q_OS_IOS
    // A scan requested while the central was still starting up is issued now.
    if (m_scanning && name == QLatin1String("poweredOn")) ios_ble_ptt_start_scan();
#endif
    rebuildModel();
    updateStatusText();
}

void PttButtonManager::onBleScanResult(const QString &id, const QString &name, int rssi, const QStringList &services, const QByteArray &mfg)
{
    if (!m_scanning || id.isEmpty()) return;
    ScanEntry &e = m_scanEntries[id];
    e.id = id;
    if (!name.isEmpty()) e.name = name;
    e.rssi = rssi;
    if (!services.isEmpty()) e.services = services;
    if (!mfg.isEmpty()) e.mfg = mfg;
    e.lastSeenMs = static_cast<qint64>(nowMs());
    std::vector<std::string> svc;
    for (const QString &s : e.services) svc.push_back(s.toStdString());
    if (const ptt::KnownSignature *sig = ptt::matchKnownSignature(e.name.toStdString(), svc, toBytes(e.mfg))) {
        e.signatureId = QString::fromLatin1(sig->id);
        e.signatureName = QString::fromLatin1(sig->displayName);
    }
    if (!m_scanFlushTimer.isActive()) m_scanFlushTimer.start();
}

void PttButtonManager::flushScanModel()
{
    QList<ScanEntry> entries = m_scanEntries.values();
    auto bars = [](int rssi) { return rssi > -60 ? 4 : rssi > -70 ? 3 : rssi > -80 ? 2 : 1; };
    // Stable order: recognised first, then signal bucket, then name. RSSI jitter must not reorder
    // or rebuild the list while the user is scrolling it.
    std::sort(entries.begin(), entries.end(), [&](const ScanEntry &a, const ScanEntry &b) {
        if (a.signatureId.isEmpty() != b.signatureId.isEmpty()) return !a.signatureId.isEmpty();
        if (bars(a.rssi) != bars(b.rssi)) return bars(a.rssi) > bars(b.rssi);
        if (a.name != b.name) return a.name < b.name;
        return a.id < b.id;
    });
    QVariantList model;
    for (const ScanEntry &e : entries) {
        QVariantMap m;
        m[QStringLiteral("id")] = e.id;
        m[QStringLiteral("name")] = e.name.isEmpty() ? tr("Unnamed device") : e.name;
        m[QStringLiteral("named")] = !e.name.isEmpty();
        m[QStringLiteral("bars")] = bars(e.rssi);
        m[QStringLiteral("recognised")] = !e.signatureId.isEmpty();
        m[QStringLiteral("signatureName")] = e.signatureName;
        m[QStringLiteral("saved")] = findDevice(e.id) != nullptr;
        model.append(m);
    }
    if (model == m_scanModel) return;
    m_scanModel = model;
    emit scanResultsChanged();
}

void PttButtonManager::setScanning(bool on)
{
    if (m_scanning == on) return;
    m_scanning = on;
    emit scanningChanged();
}

void PttButtonManager::startScan()
{
    // Off-iOS the backend is a no-op; the flow itself stays usable for tests.
    ensureBleCentral();
    m_scanEntries.clear();
    m_scanModel.clear();
    emit scanResultsChanged();
    setScanning(true);
    m_scanTimer.start();
#ifdef Q_OS_IOS
    // The central may still be coming up; onBleState re-issues the scan once powered on.
    ios_ble_ptt_start_scan();
#endif
}

void PttButtonManager::stopScan()
{
    m_scanTimer.stop();
#ifdef Q_OS_IOS
    ios_ble_ptt_stop_scan();
#endif
    setScanning(false);
}

void PttButtonManager::onScanTimeout()
{
    stopScan();
}

void PttButtonManager::addScannedDevice(const QString &identifier)
{
    if (identifier.isEmpty()) return;
    if (m_keyLearning) cancelKeyLearning();
    if (m_add.active) cancelAdd();
    stopScan();

    const ScanEntry e = m_scanEntries.value(identifier);
    m_add = AddFlow();
    m_add.active = true;
    m_add.id = identifier;
    m_add.name = e.name;
    m_add.mfg = e.mfg;
    m_add.advServices = e.services;
    m_add.advSignature = e.signatureId;
    m_add.relearn = findDevice(identifier) != nullptr;

    setLearnProgress(tr("Press the button once to wake it"));
    setLearnState(QStringLiteral("connecting"));
    m_connectTimer.start();
    bleConnect(identifier, m_add.relearn);
}

void PttButtonManager::relearnDevice(const QString &id)
{
    Device *d = findDevice(id);
    if (!d || d->kind != ptt::Kind::Ble) return;
    if (m_keyLearning) cancelKeyLearning();
    if (m_add.active) cancelAdd();
    handleAction(d, d->id, d->name, d->rt.forceRelease());
    m_add = AddFlow();
    m_add.active = true;
    m_add.relearn = true;
    m_add.id = id;
    m_add.name = d->name;
    m_add.mfg = QByteArray::fromHex(d->manufacturerHex.toLatin1());
    setLearnProgress(tr("Press the button once to wake it"));
    setLearnState(QStringLiteral("connecting"));
    m_connectTimer.start();
    ensureBleCentral();
    d->enabled = true;
    bleConnect(id, true);   // already connected: the central re-runs discovery and reports services
}

void PttButtonManager::cancelAdd()
{
    if (!m_add.active) {
        if (m_learnState != QLatin1String("idle")) setLearnState(QStringLiteral("idle"));
        return;
    }
    m_connectTimer.stop();
    m_learnTimer.stop();
    const QString id = m_add.id;
    const bool keep = findDevice(id) != nullptr;
    m_add = AddFlow();
#ifdef Q_OS_IOS
    ios_ble_ptt_unsubscribe_all(id.toUtf8().constData());
    if (keep) {
        if (Device *d = findDevice(id)) {
            if (d->conn == Conn::Ready) bleSubscribeMapping(*d);
        }
    } else {
        ios_ble_ptt_forget(id.toUtf8().constData());
    }
#else
    Q_UNUSED(keep);
#endif
    setLearnState(QStringLiteral("idle"));
}

void PttButtonManager::finishAdd()
{
    if (m_add.active) cancelAdd();
    else setLearnState(QStringLiteral("idle"));
}

void PttButtonManager::onConnectTimeout()
{
    if (!m_add.active || m_add.learning) return;
    failAdd(tr("The button did not connect. Press it once and try again."));
}

void PttButtonManager::onLearnTimeout()
{
    if (!m_add.active || !m_add.learning) return;
    const ptt::Learner::Result r = m_learner.finish(nowMs());
    if (!r.ok) {
        failAdd(qs(r.reason));
        return;
    }
    completeAdd(r.mapping, QStringLiteral("learned"), r.tentative);
}

void PttButtonManager::failAdd(const QString &reason)
{
    m_connectTimer.stop();
    m_learnTimer.stop();
    const QString id = m_add.id;
    const bool keep = findDevice(id) != nullptr;
    m_add = AddFlow();
#ifdef Q_OS_IOS
    if (!id.isEmpty()) {
        ios_ble_ptt_unsubscribe_all(id.toUtf8().constData());
        if (keep) {
            if (Device *d = findDevice(id)) { if (d->conn == Conn::Ready) bleSubscribeMapping(*d); }
        } else {
            ios_ble_ptt_forget(id.toUtf8().constData());
        }
    }
#else
    Q_UNUSED(keep);
#endif
    QVariantMap r;
    r[QStringLiteral("ok")] = false;
    r[QStringLiteral("reason")] = reason;
    r[QStringLiteral("deviceId")] = id;
    setLearnState(QStringLiteral("failed"), r);
}

void PttButtonManager::completeAdd(const ptt::Mapping &mapping, const QString &signature, bool tentative)
{
    m_connectTimer.stop();
    m_learnTimer.stop();
    const QString id = m_add.id;
    QString name = m_add.name;
    Device *d = findDevice(id);
    const bool isNew = (d == nullptr);
    if (isNew) {
        Device nd;
        nd.id = id;
        nd.kind = ptt::Kind::Ble;
        nd.addedAt = QDateTime::currentDateTime();
        m_devices.append(nd);
        d = &m_devices.last();
    }
    if (d->name.isEmpty() || (isNew && !name.isEmpty())) {
        if (name.isEmpty()) {
            const ptt::KnownSignature *sig = ptt::findSignatureById(signature.toStdString());
            name = sig ? QString::fromLatin1(sig->displayName) : tr("PTT button");
        }
        d->name = name;
    }
    d->mapping = mapping;
    d->signature = signature;
    d->tentative = tentative;
    d->enabled = true;
    d->manufacturerHex = QString::fromLatin1(m_add.mfg.toHex()).toUpper();
    d->lastSeen = QDateTime::currentDateTime();
    d->conn = Conn::Ready;
    d->rt.reset();
    m_add = AddFlow();

#ifdef Q_OS_IOS
    ios_ble_ptt_unsubscribe_all(id.toUtf8().constData());
    ios_ble_ptt_set_keep_alive(id.toUtf8().constData(), true);
    bleSubscribeMapping(*d);
#endif

    const QString savedName = d->name;
    // A click-only button while in hold mode: offer the mode switch right away (user is watching).
    const bool suggest = (mapping.capability != ptt::Capability::Hold) && takeToggleSuggestion(d, id);
    d = nullptr;   // nothing below may touch the registry through this pointer

    QVariantMap r;
    r[QStringLiteral("ok")] = true;
    r[QStringLiteral("kind")] = QStringLiteral("ble");
    r[QStringLiteral("deviceId")] = id;
    r[QStringLiteral("name")] = savedName;
    r[QStringLiteral("capability")] = QString::fromLatin1(ptt::capabilityToString(mapping.capability));
    r[QStringLiteral("channel")] = qs(mapping.channel);
    r[QStringLiteral("press")] = qs(ptt::toHex(mapping.press));
    r[QStringLiteral("release")] = qs(ptt::toHex(mapping.release));
    r[QStringLiteral("tentative")] = tentative;
    r[QStringLiteral("signature")] = signature;
    r[QStringLiteral("isNew")] = isNew;
    rebuildModel();
    updateStatusText();
    scheduleSave();
    setLearnState(QStringLiteral("done"), r);
    if (isNew) emit deviceAdded(id, savedName);
    if (suggest) emit suggestToggleTxMode(id, savedName);
}

void PttButtonManager::onBleConnected(const QString &id, const QString &name)
{
    if (Device *d = findDevice(id)) {
        d->conn = Conn::Discovering;
        d->lastSeen = QDateTime::currentDateTime();
        if (!name.isEmpty() && d->name.isEmpty()) d->name = name;
        rebuildModel();
        updateStatusText();
    }
    if (m_add.active && m_add.id == id) {
        if (!name.isEmpty() && m_add.name.isEmpty()) m_add.name = name;
        setLearnProgress(tr("Connected, reading the button"));
        setLearnState(QStringLiteral("discovering"));
    }
}

void PttButtonManager::onBleServicesReady(const QString &id, const QStringList &channels)
{
    Device *d = findDevice(id);
    const bool adding = m_add.active && m_add.id == id;

    if (adding) {
        std::vector<std::string> chans;
        for (const QString &c : channels) chans.push_back(c.toStdString());
        const ptt::KnownSignature *sig = ptt::matchKnownSignatureByChannels(m_add.name.toStdString(), chans);
        if (!sig && !m_add.advSignature.isEmpty()) {
            const ptt::KnownSignature *adv = ptt::findSignatureById(m_add.advSignature.toStdString());
            if (adv && std::find(chans.begin(), chans.end(), adv->mapping.channel) != chans.end()) sig = adv;
        }
        m_connectTimer.stop();
        if (sig) {
            completeAdd(sig->mapping, QString::fromLatin1(sig->id), false);
            return;
        }
        if (channels.isEmpty()) {
            failAdd(tr("No readable button signal. If this is a keyboard-type button, use Add keyboard button instead."));
            return;
        }
        m_add.learning = true;
        m_learner.start(nowMs());
        setLearnProgress(tr("Hold the button for one second, release, then do it again"));
        setLearnState(QStringLiteral("learning"));
        m_learnTimer.start();
#ifdef Q_OS_IOS
        ios_ble_ptt_subscribe_all_candidates(id.toUtf8().constData());
#endif
        return;
    }

    if (d && d->kind == ptt::Kind::Ble) {
        d->conn = d->enabled ? Conn::Ready : Conn::Off;
        d->lastSeen = QDateTime::currentDateTime();
        if (d->enabled) bleSubscribeMapping(*d);
        rebuildModel();
        updateStatusText();
        scheduleSave();
    }
}

void PttButtonManager::onBleConnectFailed(const QString &id, int err)
{
    Q_UNUSED(err);
    if (Device *d = findDevice(id)) {
        if (d->kind == ptt::Kind::Ble && d->enabled) d->conn = Conn::Waiting;
        rebuildModel();
    }
    if (m_add.active && m_add.id == id && !m_add.learning) {
        failAdd(tr("Could not connect. Check the battery and press the button once."));
    }
}

void PttButtonManager::onBleDisconnected(const QString &id, int err)
{
    Q_UNUSED(err);
    if (Device *d = findDevice(id)) {
        handleAction(d, d->id, d->name, d->rt.forceRelease());
        if (d->kind == ptt::Kind::Ble) d->conn = d->enabled ? Conn::Waiting : Conn::Off;
        rebuildModel();
        updateStatusText();
    }
    if (m_add.active && m_add.id == id) {
        if (m_add.learning) {
            // Some buttons drop the link right after a cycle: keep whatever was learned.
            const ptt::Learner::Result r = m_learner.finish(nowMs());
            if (r.ok) completeAdd(r.mapping, QStringLiteral("learned"), r.tentative);
            else failAdd(tr("The button disconnected before it could be learned. Try again."));
        } else {
            failAdd(tr("The button disconnected. Press it once and try again."));
        }
    }
}

void PttButtonManager::onBleNotification(const QString &id, const QString &channel, const QByteArray &value)
{
    if (m_add.active && m_add.id == id && m_add.learning) {
        ptt::RawEvent ev{nowMs(), channel.toStdString(), toBytes(value)};
        m_learner.feed(ev);
        setLearnProgress(qs(m_learner.progressText()) + QStringLiteral("  [") + channel + QStringLiteral(" ") + qs(m_learner.lastValueHex()) + QLatin1Char(']'));
        if (m_learner.finished()) {
            const ptt::Learner::Result r = m_learner.finish(ev.tMs);
            if (r.ok) completeAdd(r.mapping, QStringLiteral("learned"), r.tentative);
        }
        return;
    }
    Device *d = findDevice(id);
    if (!d || !d->enabled || d->kind != ptt::Kind::Ble) return;
    ptt::RawEvent ev{nowMs(), channel.toStdString(), toBytes(value)};
    const ptt::Effective eff = ptt::resolveBehaviour(d->mapping.capability, d->behaviour, !m_holdToTalkMode);
    const auto a = d->rt.feed(ev, d->mapping, eff, ev.tMs);
    if (a != ptt::DeviceRuntime::Action::None) {
        d->lastSeen = QDateTime::currentDateTime();
        handleAction(d, d->id, d->name, a);
    }
}

// ------------------------------------------------------------ learn state

void PttButtonManager::setLearnState(const QString &state, const QVariantMap &result)
{
    m_learnState = state;
    m_learnResult = result;
    emit learnStateChanged();
}

void PttButtonManager::setLearnProgress(const QString &text)
{
    if (m_learnProgress == text) return;
    m_learnProgress = text;
    emit learnProgressChanged();
}

void PttButtonManager::openBluetoothSettings()
{
#ifdef Q_OS_IOS
    QDesktopServices::openUrl(QUrl(QStringLiteral("app-settings:")));
#endif
}

// ------------------------------------------------------------ model / status

QString PttButtonManager::connKey(const Device &d) const
{
    if (d.kind == ptt::Kind::Hid) return d.enabled ? QStringLiteral("ready") : QStringLiteral("off");
    if (!d.enabled) return QStringLiteral("off");
    if (m_bluetoothState != QLatin1String("poweredOn")) return QStringLiteral("unavailable");
    switch (d.conn) {
    case Conn::Ready: return QStringLiteral("connected");
    case Conn::Connecting:
    case Conn::Discovering: return QStringLiteral("connecting");
    case Conn::Waiting: return QStringLiteral("sleeping");
    case Conn::Unavailable: return QStringLiteral("unavailable");
    case Conn::Off: break;
    }
    return QStringLiteral("off");
}

QString PttButtonManager::connLabel(const Device &d) const
{
    const QString k = connKey(d);
    if (k == QLatin1String("connected")) return tr("Connected");
    if (k == QLatin1String("connecting")) return tr("Connecting…");
    if (k == QLatin1String("sleeping")) return tr("Sleeping. Press it to wake");
    if (k == QLatin1String("unavailable")) {
        if (m_bluetoothState == QLatin1String("unauthorized")) return tr("Bluetooth permission needed");
        if (m_bluetoothState == QLatin1String("unsupported")) return tr("Bluetooth LE unavailable");
        if (m_bluetoothState == QLatin1String("poweredOff")) return tr("Bluetooth is off");
        return tr("Waiting for Bluetooth");
    }
    if (k == QLatin1String("ready")) return tr("Ready (foreground only)");
    return tr("Off");
}

void PttButtonManager::rebuildModel()
{
    QVariantList model;
    for (const Device &d : m_devices) {
        QVariantMap m;
        m[QStringLiteral("id")] = d.id;
        m[QStringLiteral("name")] = d.name;
        m[QStringLiteral("kind")] = QString::fromLatin1(ptt::kindToString(d.kind));
        m[QStringLiteral("enabled")] = d.enabled;
        m[QStringLiteral("statusKey")] = connKey(d);
        m[QStringLiteral("status")] = connLabel(d);
        m[QStringLiteral("connected")] = (d.kind == ptt::Kind::Ble) ? (d.conn == Conn::Ready) : d.enabled;
        m[QStringLiteral("capability")] = QString::fromLatin1(ptt::capabilityToString(d.mapping.capability));
        m[QStringLiteral("behaviour")] = QString::fromLatin1(ptt::behaviourToString(d.behaviour));
        const ptt::Effective eff = ptt::resolveBehaviour(d.mapping.capability, d.behaviour, !m_holdToTalkMode);
        m[QStringLiteral("effective")] = (eff == ptt::Effective::Hold) ? QStringLiteral("hold") : QStringLiteral("toggle");
        m[QStringLiteral("tentative")] = d.tentative;
        m[QStringLiteral("signature")] = d.signature;
        const ptt::KnownSignature *sig = ptt::findSignatureById(d.signature.toStdString());
        m[QStringLiteral("signatureName")] = sig ? QString::fromLatin1(sig->displayName) : QString();
        m[QStringLiteral("channel")] = qs(d.mapping.channel);
        m[QStringLiteral("pressed")] = d.rt.pressed();
        m[QStringLiteral("lastEvent")] = d.lastEvent.isValid() ? d.lastEvent.toString(QStringLiteral("dd MMM HH:mm")) : QString();
        m[QStringLiteral("lastSeen")] = d.lastSeen.isValid() ? d.lastSeen.toString(QStringLiteral("dd MMM HH:mm")) : QString();
        model.append(m);
    }
    m_deviceModel = model;
    emit devicesChanged();
}

void PttButtonManager::updateStatusText()
{
    QString s;
    int ble = 0, connected = 0, hid = 0;
    for (const Device &d : m_devices) {
        if (!d.enabled) continue;
        if (d.kind == ptt::Kind::Ble) { ++ble; if (d.conn == Conn::Ready) ++connected; }
        else if (d.kind == ptt::Kind::Hid) ++hid;
    }
    if (ble == 0 && hid == 0) {
        s = m_mediaEnabled ? tr("Headset button") : tr("Off");
    } else {
        QStringList parts;
        if (ble > 0) {
            if (m_bluetoothState == QLatin1String("poweredOn")) parts << tr("%1/%2 connected").arg(connected).arg(ble);
            else if (m_bluetoothState == QLatin1String("unauthorized")) parts << tr("Bluetooth permission needed");
            else if (m_bluetoothState == QLatin1String("poweredOff")) parts << tr("Bluetooth off");
            else parts << tr("%1 Bluetooth").arg(ble);
        }
        if (hid > 0) parts << tr("%1 keyboard").arg(hid);
        s = parts.join(QStringLiteral(", "));
    }
    if (s == m_statusText) return;
    m_statusText = s;
    emit statusTextChanged();
}

// ------------------------------------------------------------ persistence

QString PttButtonManager::storagePath() const
{
    const QString dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dirPath);
    if (!dir.exists()) dir.mkpath(dirPath);
    return dirPath + QStringLiteral("/ptt_buttons.json");
}

void PttButtonManager::scheduleSave()
{
    if (!m_loaded) return;
    if (!m_saveTimer.isActive()) m_saveTimer.start();
}

void PttButtonManager::saveNow()
{
    if (!m_loaded) return;
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("holdSafetySeconds")] = m_holdSafetySeconds;
    QJsonObject media;
    media[QStringLiteral("enabled")] = m_mediaEnabled;
    media[QStringLiteral("trackButtons")] = m_mediaTrackButtons;
    media[QStringLiteral("autoSwitch")] = QString::fromLatin1(ptt::autoSwitchToString(m_mediaAutoSwitch));
    root[QStringLiteral("media")] = media;
    QJsonArray devs;
    for (const Device &d : m_devices) {
        QJsonObject o;
        o[QStringLiteral("id")] = d.id;
        o[QStringLiteral("kind")] = QString::fromLatin1(ptt::kindToString(d.kind));
        o[QStringLiteral("name")] = d.name;
        o[QStringLiteral("enabled")] = d.enabled;
        o[QStringLiteral("signature")] = d.signature;
        o[QStringLiteral("channel")] = qs(d.mapping.channel);
        o[QStringLiteral("match")] = QString::fromLatin1(ptt::matchToString(d.mapping.match));
        o[QStringLiteral("press")] = qs(ptt::toHex(d.mapping.press));
        o[QStringLiteral("release")] = qs(ptt::toHex(d.mapping.release));
        o[QStringLiteral("capability")] = QString::fromLatin1(ptt::capabilityToString(d.mapping.capability));
        o[QStringLiteral("behaviour")] = QString::fromLatin1(ptt::behaviourToString(d.behaviour));
        o[QStringLiteral("autoSwitch")] = QString::fromLatin1(ptt::autoSwitchToString(d.autoSwitch));
        o[QStringLiteral("manufacturerData")] = d.manufacturerHex;
        o[QStringLiteral("addedAt")] = d.addedAt.isValid() ? d.addedAt.toString(Qt::ISODate) : QString();
        o[QStringLiteral("lastSeen")] = d.lastSeen.isValid() ? d.lastSeen.toString(Qt::ISODate) : QString();
        o[QStringLiteral("lastEvent")] = d.lastEvent.isValid() ? d.lastEvent.toString(Qt::ISODate) : QString();
        o[QStringLiteral("tentative")] = d.tentative;
        devs.append(o);
    }
    root[QStringLiteral("devices")] = devs;

    QSaveFile f(storagePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[PttButton] cannot write" << f.fileName() << f.errorString();
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit()) qWarning() << "[PttButton] save failed:" << f.errorString();
}

void PttButtonManager::load()
{
    m_devices.clear();
    const QString path = storagePath();
    QFile f(path);
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        const QByteArray raw = f.readAll();
        f.close();
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "[PttButton] corrupt settings, starting empty:" << err.errorString();
            QFile::remove(path + QStringLiteral(".bad"));
            QFile::copy(path, path + QStringLiteral(".bad"));
        } else {
            const QJsonObject root = doc.object();
            if (root.value(QStringLiteral("version")).toInt(1) == 1) {
                m_holdSafetySeconds = qBound(10, root.value(QStringLiteral("holdSafetySeconds")).toInt(180), 900);
                const QJsonObject media = root.value(QStringLiteral("media")).toObject();
                m_mediaEnabled = media.value(QStringLiteral("enabled")).toBool(true);
                m_mediaTrackButtons = media.value(QStringLiteral("trackButtons")).toBool(false);
                m_mediaAutoSwitch = ptt::autoSwitchFromString(media.value(QStringLiteral("autoSwitch")).toString().toStdString());
                for (const QJsonValue &v : root.value(QStringLiteral("devices")).toArray()) {
                    const QJsonObject o = v.toObject();
                    Device d;
                    d.id = o.value(QStringLiteral("id")).toString();
                    if (d.id.isEmpty() || findDevice(d.id)) continue;
                    d.kind = ptt::kindFromString(o.value(QStringLiteral("kind")).toString().toStdString());
                    if (d.kind == ptt::Kind::Media) continue;
                    d.name = o.value(QStringLiteral("name")).toString();
                    d.enabled = o.value(QStringLiteral("enabled")).toBool(true);
                    d.signature = o.value(QStringLiteral("signature")).toString();
                    d.mapping.channel = o.value(QStringLiteral("channel")).toString().toStdString();
                    d.mapping.match = ptt::matchFromString(o.value(QStringLiteral("match")).toString().toStdString());
                    d.mapping.press = ptt::fromHex(o.value(QStringLiteral("press")).toString().toStdString());
                    d.mapping.release = ptt::fromHex(o.value(QStringLiteral("release")).toString().toStdString());
                    d.mapping.capability = ptt::capabilityFromString(o.value(QStringLiteral("capability")).toString().toStdString());
                    d.behaviour = ptt::behaviourFromString(o.value(QStringLiteral("behaviour")).toString().toStdString());
                    d.autoSwitch = ptt::autoSwitchFromString(o.value(QStringLiteral("autoSwitch")).toString().toStdString());
                    d.manufacturerHex = o.value(QStringLiteral("manufacturerData")).toString();
                    d.addedAt = QDateTime::fromString(o.value(QStringLiteral("addedAt")).toString(), Qt::ISODate);
                    d.lastSeen = QDateTime::fromString(o.value(QStringLiteral("lastSeen")).toString(), Qt::ISODate);
                    d.lastEvent = QDateTime::fromString(o.value(QStringLiteral("lastEvent")).toString(), Qt::ISODate);
                    d.tentative = o.value(QStringLiteral("tentative")).toBool(false);
                    if (d.mapping.channel.empty()) continue;
                    d.conn = (d.kind == ptt::Kind::Hid) ? Conn::Ready : (d.enabled ? Conn::Waiting : Conn::Off);
                    m_devices.append(d);
                }
            } else {
                qWarning() << "[PttButton] unknown settings version, starting empty";
            }
        }
    }
    m_loaded = true;
}
