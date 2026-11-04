/*
    Copyright (C) 2026 Rohith Namboothiri
    GPL-3.0-or-later
*/

#include "DmrSmsManager.h"

#include "DmrDataCodec.h"
#include "droidstar.h"
#include "ios_notifications.h"
#include "vuidupdater.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

namespace {
DmrSmsManager *s_smsManager = nullptr;
QMutex s_diskMutex;

void nativeSmsTapCallback(const char *threadId)
{
    if (!s_smsManager || !threadId)
        return;
    const QString key = QString::fromUtf8(threadId);
    QMetaObject::invokeMethod(s_smsManager, [key]() {
        if (s_smsManager) {
            s_smsManager->openThread(key);
        }
    }, Qt::QueuedConnection);
}

void nativeKeyboardHeightCallback(double heightPts)
{
    if (!s_smsManager)
        return;
    s_smsManager->updateNativeKeyboardHeight(heightPts);
}

void writeSnapshotToDisk(const QString &path, int nextId, const QVariantList &snapshot)
{
    QMutexLocker diskLock(&s_diskMutex);
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("nextId"), nextId);
    root.insert(QStringLiteral("messages"), QJsonArray::fromVariantList(snapshot));
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.commit();
    }
}
} // namespace

DmrSmsManager::DmrSmsManager(DroidStar *droidStar,
                             VUIDUpdater *vuidUpdater,
                             QObject *parent)
    : QObject(parent),
      m_droidStar(droidStar),
      m_vuidUpdater(vuidUpdater),
      m_saveTimer(new QTimer(this))
{
    s_smsManager = this;
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(120);
    connect(m_saveTimer, &QTimer::timeout, this, &DmrSmsManager::flushSaveAsync);

    QSettings settings(QStringLiteral("dudetronics"), QStringLiteral("droidstar"));
    m_defaultFormat = std::clamp(settings.value(QStringLiteral("DMR_SMS_FORMAT"), 0).toInt(), 0, 3);
    m_sendPreambleCsbk = settings.value(QStringLiteral("DMR_SMS_CSBK"), true).toBool();
    load();

    if (m_droidStar) {
        connect(m_droidStar, &DroidStar::dmr_sms_received,
                this, &DmrSmsManager::onSmsReceived);
        connect(m_droidStar, &DroidStar::dmr_sms_send_status,
                this, &DmrSmsManager::onSmsSendStatus);
        connect(m_droidStar, &DroidStar::dmr_sms_ack_received,
                this, &DmrSmsManager::onSmsAckReceived);
    }
    if (m_vuidUpdater) {
        connect(m_vuidUpdater, &VUIDUpdater::userLookupReady,
                this, &DmrSmsManager::onUserLookupReady);
    }
    ios_notifications_set_sms_tap_callback(nativeSmsTapCallback);
    ios_keyboard_observer_init(nativeKeyboardHeightCallback);
}

void DmrSmsManager::updateNativeKeyboardHeight(double heightPts)
{
    const double clamped = std::max(0.0, heightPts);
    if (qFuzzyCompare(m_keyboardHeight + 1.0, clamped + 1.0))
        return;
    m_keyboardHeight = clamped;
    emit keyboardHeightChanged();
}

void DmrSmsManager::setChatKeyboardMode(bool active)
{
    ios_set_chat_keyboard_mode(active);
}

DmrSmsManager::~DmrSmsManager()
{
    if (m_saveTimer)
        m_saveTimer->stop();
    saveSync();
    if (s_smsManager == this)
        s_smsManager = nullptr;
}

DmrSmsManager *DmrSmsManager::instance()
{
    return s_smsManager;
}

QString DmrSmsManager::storagePath() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
              QStringLiteral("/dudetronics");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/sms_history.json");
}

void DmrSmsManager::load()
{
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return;
    const QJsonObject root = doc.object();
    const QJsonArray rows = root.value(QStringLiteral("messages")).toArray();
    QMutexLocker lock(&m_mutex);
    m_messages.clear();
    int maxId = 0;
    for (const QJsonValue &value : rows) {
        QVariantMap row = value.toObject().toVariantMap();
        if (!row.value(QStringLiteral("threadKey")).toString().isEmpty()) {
            const QString ts = row.value(QStringLiteral("timestamp")).toString();
            if (row.value(QStringLiteral("displayTime")).toString().isEmpty()) {
                row.insert(QStringLiteral("displayTime"), formatDisplayTime(ts));
            }
            m_messages.append(row);
            maxId = std::max(maxId, row.value(QStringLiteral("id")).toInt());
        }
    }
    m_nextId = std::max(root.value(QStringLiteral("nextId")).toInt(maxId + 1), maxId + 1);
    rebuildCachesLocked();
}

void DmrSmsManager::saveSync() const
{
    QVariantList snapshot;
    int nextId = 1;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_messages;
        nextId = m_nextId;
    }
    writeSnapshotToDisk(storagePath(), nextId, snapshot);
}

void DmrSmsManager::scheduleSave()
{
    if (m_saveTimer)
        m_saveTimer->start();
}

void DmrSmsManager::flushSaveAsync()
{
    QVariantList snapshot;
    int nextId = 1;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_messages;
        nextId = m_nextId;
    }
    const QString path = storagePath();
    (void)QtConcurrent::run([path, nextId, snapshot]() {
        writeSnapshotToDisk(path, nextId, snapshot);
    });
}

QString DmrSmsManager::threadKeyFor(uint32_t id, bool isGroup) const
{
    return QStringLiteral("%1:%2").arg(isGroup ? QLatin1String("g") : QLatin1String("p"))
                                  .arg(id);
}

bool DmrSmsManager::isAprsGatewayId(uint32_t id)
{
    return id >= 100999U && id <= 999999U && (id % 1000U) == 999U;
}

bool DmrSmsManager::isServiceId(uint32_t id)
{
    return id == 9990U || id == 31000U || id == 262993U || id == 262994U || id == 262995U ||
           isAprsGatewayId(id);
}

QString DmrSmsManager::labelForId(uint32_t id, bool isGroup) const
{
    if (isGroup) {
        if (m_droidStar) {
            const QString tgName = m_droidStar->getTalkgroupName(QString::number(id));
            if (!tgName.isEmpty())
                return QStringLiteral("TG %1 • %2").arg(id).arg(tgName);
        }
        return QStringLiteral("TG %1").arg(id);
    }
    if (id == 9990U || id == 31000U) return tr("Parrot (voice only)");
    if (id == 262993U) return tr("BM Echo, Weather & GPS");
    if (id == 262994U) return tr("BM Repeater & Pager");
    if (id == 262995U) return tr("BM SMSC");
    if (isAprsGatewayId(id))
        return tr("BM APRS Gateway (%1)").arg(id);
    if (m_droidStar) {
        const QString call = m_droidStar->lookupCallsignForDmrId(id);
        if (!call.isEmpty())
            return call;
    }
    return QString::number(id);
}

QString DmrSmsManager::subtitleForId(uint32_t id, bool isGroup) const
{
    if (isGroup) {
        if (m_droidStar) {
            const QString tgName = m_droidStar->getTalkgroupName(QString::number(id));
            if (!tgName.isEmpty())
                return tgName;
        }
        return tr("Talkgroup %1").arg(id);
    }
    if (id == 9990U || id == 31000U) return tr("Voice echo. Does not reply to text; use ECHO to 262993");
    if (id == 262993U) return tr("ECHO, HELP, WX, WX <city>, METAR <ICAO>, GPS, RSSI, CONFIG SHORT");
    if (id == 262994U) return tr("RPT, or <CALLSIGN> <text> to a DAPNET pager");
    if (id == 262995U) return tr("<CALLSIGN> <text>, INBOX, GET, DELETE, ECHO");
    if (isAprsGatewayId(id))
        return tr("<CALLSIGN-SSID> <text>, or SMSGTE @<phone> <text>");
    return QString();
}

QString DmrSmsManager::formatDisplayTime(const QString &isoTimestamp)
{
    if (isoTimestamp.isEmpty())
        return QString();
    const QDateTime dt = QDateTime::fromString(isoTimestamp, Qt::ISODateWithMs).toLocalTime();
    if (!dt.isValid())
        return QString();
    const QDate today = QDateTime::currentDateTime().date();
    if (dt.date() == today)
        return dt.toString(QStringLiteral("HH:mm"));
    if (dt.date() == today.addDays(-1))
        return QStringLiteral("Yesterday ") + dt.toString(QStringLiteral("HH:mm"));
    return dt.toString(QStringLiteral("MMM d, HH:mm"));
}

QString DmrSmsManager::previewFor(const QString &text)
{
    QString p = text.simplified();
    if (p.size() > 72)
        p = p.left(71) + QChar(0x2026);
    return p;
}

QString DmrSmsManager::timestampNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

int DmrSmsManager::nextMessageId()
{
    QMutexLocker lock(&m_mutex);
    return m_nextId++;
}

void DmrSmsManager::rebuildCachesLocked()
{
    QHash<QString, QVariantMap> grouped;
    int totalUnread = 0;

    for (const QVariant &value : std::as_const(m_messages)) {
        const QVariantMap msg = value.toMap();
        const QString key = msg.value(QStringLiteral("threadKey")).toString();
        if (key.isEmpty())
            continue;

        const bool isUnread = msg.value(QStringLiteral("unread")).toBool();
        if (isUnread)
            ++totalUnread;

        QVariantMap thread = grouped.value(key);
        const QString ts = msg.value(QStringLiteral("timestamp")).toString();
        const uint32_t peerId = msg.value(QStringLiteral("peerId")).toUInt();
        const bool isGroup = msg.value(QStringLiteral("isGroup")).toBool();

        if (thread.isEmpty() || ts >= thread.value(QStringLiteral("timestamp")).toString()) {
            const int prevUnread = thread.value(QStringLiteral("unreadCount")).toInt();
            QString callLabel = msg.value(QStringLiteral("peerCallsign")).toString();
            const bool service = !isGroup && isServiceId(peerId);
            if (service || callLabel.isEmpty() || callLabel == QString::number(peerId)) {
                callLabel = labelForId(peerId, isGroup);
            }
            QString peerName = msg.value(QStringLiteral("peerName")).toString();
            if (service || peerName.isEmpty()) {
                peerName = subtitleForId(peerId, isGroup);
            }
            thread.insert(QStringLiteral("threadKey"), key);
            thread.insert(QStringLiteral("peerId"), peerId);
            thread.insert(QStringLiteral("peerCallsign"), callLabel);
            thread.insert(QStringLiteral("peerName"), peerName);
            thread.insert(QStringLiteral("isGroup"), isGroup);
            thread.insert(QStringLiteral("lastText"), msg.value(QStringLiteral("text")).toString());
            thread.insert(QStringLiteral("lastDirection"), msg.value(QStringLiteral("direction")).toString());
            thread.insert(QStringLiteral("lastFormatName"), msg.value(QStringLiteral("formatName")).toString());
            thread.insert(QStringLiteral("timestamp"), ts);
            thread.insert(QStringLiteral("displayTime"), formatDisplayTime(ts));
            thread.insert(QStringLiteral("lastStatus"), msg.value(QStringLiteral("status")).toInt());
            thread.insert(QStringLiteral("unreadCount"), prevUnread);
        }
        if (isUnread) {
            thread.insert(QStringLiteral("unreadCount"),
                          thread.value(QStringLiteral("unreadCount")).toInt() + 1);
        }
        grouped.insert(key, thread);
    }

    QVariantList sorted;
    sorted.reserve(grouped.size());
    for (const QVariantMap &thread : std::as_const(grouped))
        sorted.append(thread);

    std::sort(sorted.begin(), sorted.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("timestamp")).toString() >
               b.toMap().value(QStringLiteral("timestamp")).toString();
    });

    m_threadsCache = std::move(sorted);
    m_unreadCountCache = totalUnread;
}

QVariantList DmrSmsManager::threads() const
{
    QMutexLocker lock(&m_mutex);
    return m_threadsCache;
}

int DmrSmsManager::unreadCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_unreadCountCache;
}

QVariantList DmrSmsManager::messagesForThread(const QString &threadKey) const
{
    QMutexLocker lock(&m_mutex);
    QVariantList out;
    for (const QVariant &value : m_messages) {
        QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("threadKey")).toString() == threadKey) {
            if (row.value(QStringLiteral("displayTime")).toString().isEmpty()) {
                row.insert(QStringLiteral("displayTime"),
                           formatDisplayTime(row.value(QStringLiteral("timestamp")).toString()));
            }
            out.append(row);
        }
    }
    return out;
}

QVariantMap DmrSmsManager::resolveTarget(const QString &input, bool isGroup) const
{
    QVariantMap result;
    QString cleaned = input.trimmed().toUpper();
    if (cleaned.startsWith(QStringLiteral("TG ")))
        cleaned = cleaned.mid(3).trimmed();
    else if (cleaned.startsWith(QStringLiteral("ID ")))
        cleaned = cleaned.mid(3).trimmed();

    bool ok = false;
    uint32_t id = cleaned.toUInt(&ok);
    if (!ok && !isGroup && m_droidStar) {
        id = m_droidStar->lookupDmrIdForCallsign(cleaned);
        ok = id > 0U;
    }
    result.insert(QStringLiteral("valid"), ok && id > 0U);
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("callsign"), id ? labelForId(id, isGroup) : QString());
    result.insert(QStringLiteral("subtitle"), id ? subtitleForId(id, isGroup) : QString());
    result.insert(QStringLiteral("isGroup"), isGroup);
    return result;
}

QVariantMap DmrSmsManager::ensureThreadMeta(uint32_t id, bool isGroup)
{
    QVariantMap meta;
    const QString key = threadKeyFor(id, isGroup);
    QString callLabel = labelForId(id, isGroup);
    QString subLabel = subtitleForId(id, isGroup);

    {
        QMutexLocker lock(&m_mutex);
        for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
            const QVariantMap row = m_messages.at(i).toMap();
            if (row.value(QStringLiteral("threadKey")).toString() == key) {
                if (!isGroup && isServiceId(id))
                    break;
                const QString savedCall = row.value(QStringLiteral("peerCallsign")).toString();
                const QString savedName = row.value(QStringLiteral("peerName")).toString();
                if (!savedCall.isEmpty() && savedCall != QString::number(id))
                    callLabel = savedCall;
                if (!savedName.isEmpty())
                    subLabel = savedName;
                break;
            }
        }
    }

    if (!isGroup && m_vuidUpdater && id >= 1000000U) {
        m_vuidUpdater->fetchFirstNameFromAPI(id);
    }

    meta.insert(QStringLiteral("threadKey"), key);
    meta.insert(QStringLiteral("peerId"), id);
    meta.insert(QStringLiteral("peerCallsign"), callLabel);
    meta.insert(QStringLiteral("peerName"), subLabel);
    meta.insert(QStringLiteral("isGroup"), isGroup);
    return meta;
}

QVariantList DmrSmsManager::searchRecipients(const QString &query,
                                             bool isGroup,
                                             const QString &hostName) const
{
    QVariantList results;
    const QString q = query.trimmed();
    const QString qLower = q.toLower();
    QSet<uint32_t> seen;

    auto appendCandidate = [&](uint32_t id, const QString &title, const QString &subtitle, const QString &badge) {
        if (id == 0U || seen.contains(id))
            return;
        if (!qLower.isEmpty()) {
            const QString idStr = QString::number(id);
            if (!idStr.contains(qLower) &&
                !title.toLower().contains(qLower) &&
                !subtitle.toLower().contains(qLower)) {
                return;
            }
        }
        seen.insert(id);
        results.append(QVariantMap{
            { QStringLiteral("id"), id },
            { QStringLiteral("callsign"), title },
            { QStringLiteral("subtitle"), subtitle },
            { QStringLiteral("badge"), badge },
            { QStringLiteral("isGroup"), isGroup }
        });
    };

    // 1. Existing conversation threads matching mode
    const QVariantList currentThreads = threads();
    for (const QVariant &v : currentThreads) {
        const QVariantMap t = v.toMap();
        if (t.value(QStringLiteral("isGroup")).toBool() != isGroup)
            continue;
        appendCandidate(t.value(QStringLiteral("peerId")).toUInt(),
                        t.value(QStringLiteral("peerCallsign")).toString(),
                        t.value(QStringLiteral("peerName")).toString(),
                        tr("Recent"));
        if (results.size() >= 16)
            return results;
    }

    if (isGroup) {
        // 2. Talkgroup directory search
        if (m_droidStar) {
            const QVariantList tgs = m_droidStar->searchTalkgroups(q, 14);
            for (const QVariant &v : tgs) {
                const QVariantMap m = v.toMap();
                const uint32_t tgId = m.value(QStringLiteral("tgid")).toString().toUInt();
                const QString tgName = m.value(QStringLiteral("name")).toString();
                appendCandidate(tgId,
                                tgName.isEmpty() ? QStringLiteral("TG %1").arg(tgId)
                                                 : QStringLiteral("TG %1 • %2").arg(tgId).arg(tgName),
                                m.value(QStringLiteral("isLive")).toBool()
                                    ? tr("Live on air (%1)").arg(m.value(QStringLiteral("sourceCall")).toString())
                                    : tr("Talkgroup SMS"),
                                m.value(QStringLiteral("isFavourite")).toBool() ? tr("Fav TG") : tr("TG"));
                if (results.size() >= 16)
                    break;
            }
        }
        return results;
    }

    // 2. Built-in BrandMeister text services (private messages)
    const uint32_t aprsId = aprsServiceIdForHost(hostName);
    appendCandidate(262993U, labelForId(262993U), tr("ECHO test, WX, METAR, GPS, RSSI, CONFIG"), tr("Service"));
    appendCandidate(262995U, labelForId(262995U), tr("Text a callsign: <CALLSIGN> <text>, INBOX"), tr("Service"));
    appendCandidate(aprsId,  labelForId(aprsId),  tr("APRS station, or SMSGTE @<phone> for cell SMS"), tr("APRS"));
    appendCandidate(262994U, labelForId(262994U), tr("RPT, or <CALLSIGN> <text> to a pager"), tr("Service"));

    // 3. Fast DMR Callsign / ID search
    if (m_droidStar && !q.isEmpty()) {
        const QVariantList contacts = m_droidStar->searchDmrContacts(q, 12);
        for (const QVariant &v : contacts) {
            const QVariantMap c = v.toMap();
            const uint32_t cid = c.value(QStringLiteral("id")).toUInt();
            const QString cs = c.value(QStringLiteral("callsign")).toString();
            appendCandidate(cid, cs, tr("DMR ID %1").arg(cid), tr("Station"));
            if (results.size() >= 18)
                break;
        }
    }

    return results;
}

bool DmrSmsManager::canSend() const
{
    return m_droidStar && m_droidStar->isDmrConnected();
}

int DmrSmsManager::sendMessage(const QString &targetIdOrCall, const QString &text,
                               bool isGroup, int formatOverride)
{
    const QVariantMap target = resolveTarget(targetIdOrCall, isGroup);
    const uint32_t peerId = target.value(QStringLiteral("id")).toUInt();
    const QString body = text.trimmed();
    if (!target.value(QStringLiteral("valid")).toBool() || body.isEmpty()) {
        emit operationError(tr("Enter a valid DMR ID or callsign and message."));
        return -1;
    }
    if (!canSend()) {
        emit operationError(tr("Connect to a DMR server first to transmit messages."));
        return -1;
    }
    const int format = formatOverride >= 0 ? std::clamp(formatOverride, 0, 3)
                                           : m_defaultFormat;
    const int id = nextMessageId();
    const QString key = threadKeyFor(peerId, isGroup);
    const QString ts = timestampNow();
    const bool isOdtp = m_droidStar && m_droidStar->is_odtp_active();
    const QString fmtLabel = isOdtp
        ? QStringLiteral("ODTP")
        : DmrDataCodec::formatDisplayName(static_cast<DmrDataCodec::SmsFormat>(format));

    QVariantMap row {
        { QStringLiteral("id"), id },
        { QStringLiteral("threadKey"), key },
        { QStringLiteral("peerId"), peerId },
        { QStringLiteral("peerCallsign"), labelForId(peerId, isGroup) },
        { QStringLiteral("peerName"), subtitleForId(peerId, isGroup) },
        { QStringLiteral("isGroup"), isGroup },
        { QStringLiteral("direction"), QStringLiteral("out") },
        { QStringLiteral("text"), body },
        { QStringLiteral("timestamp"), ts },
        { QStringLiteral("displayTime"), formatDisplayTime(ts) },
        { QStringLiteral("status"), Sending },
        { QStringLiteral("statusDetail"), QString() },
        { QStringLiteral("format"), format },
        { QStringLiteral("formatName"), fmtLabel },
        { QStringLiteral("unread"), false }
    };

    int oldUnread = 0;
    {
        QMutexLocker lock(&m_mutex);
        oldUnread = m_unreadCountCache;
        m_messages.append(row);
        trimHistoryLocked();
        rebuildCachesLocked();
    }
    scheduleSave();
    notifyModels(oldUnread);

    const int wireFormat = format | (m_sendPreambleCsbk ? 0 : 0x100);
    m_droidStar->requestDmrSms(id, peerId, body, isGroup, wireFormat);
    return id;
}

int DmrSmsManager::retryMessage(int msgId)
{
    if (!canSend()) {
        emit operationError(tr("Connect to a DMR server before retrying."));
        return -1;
    }
    uint32_t peerId = 0U;
    QString body;
    bool isGroup = false;
    int format = m_defaultFormat;
    int oldUnread = 0;
    bool found = false;

    {
        QMutexLocker lock(&m_mutex);
        oldUnread = m_unreadCountCache;
        for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
            QVariantMap row = m_messages.at(i).toMap();
            if (row.value(QStringLiteral("id")).toInt() == msgId) {
                peerId = row.value(QStringLiteral("peerId")).toUInt();
                body = row.value(QStringLiteral("text")).toString();
                isGroup = row.value(QStringLiteral("isGroup")).toBool();
                const int savedFmt = row.value(QStringLiteral("format")).toInt();
                if (savedFmt >= 0 && savedFmt <= 3)
                    format = savedFmt;
                const QString ts = timestampNow();
                row.insert(QStringLiteral("timestamp"), ts);
                row.insert(QStringLiteral("displayTime"), formatDisplayTime(ts));
                row.insert(QStringLiteral("status"), Sending);
                row.insert(QStringLiteral("statusDetail"), QString());
                m_messages[i] = row;
                rebuildCachesLocked();
                found = true;
                break;
            }
        }
    }
    if (!found || peerId == 0U || body.isEmpty())
        return -1;

    scheduleSave();
    notifyModels(oldUnread);
    const int wireFormat = format | (m_sendPreambleCsbk ? 0 : 0x100);
    m_droidStar->requestDmrSms(msgId, peerId, body, isGroup, wireFormat);
    return msgId;
}

void DmrSmsManager::deleteMessage(int msgId)
{
    if (msgId <= 0)
        return;
    int oldUnread = 0;
    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        oldUnread = m_unreadCountCache;
        for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
            if (m_messages.at(i).toMap().value(QStringLiteral("id")).toInt() == msgId) {
                m_messages.removeAt(i);
                changed = true;
                break;
            }
        }
        if (changed)
            rebuildCachesLocked();
    }
    if (!changed)
        return;
    scheduleSave();
    notifyModels(oldUnread);
}

void DmrSmsManager::copyToClipboard(const QString &text) const
{
    if (QClipboard *cb = QGuiApplication::clipboard()) {
        cb->setText(text);
    }
}

void DmrSmsManager::onSmsReceived(uint32_t srcId, uint32_t dstId,
                                  const QString &text, bool isGroup,
                                  const QString &format)
{
    const uint32_t peerId = isGroup ? (dstId > 0U ? dstId : srcId) : srcId;
    const QString key = threadKeyFor(peerId, isGroup);
    const bool unread = key != m_openThread;
    const QString ts = timestampNow();
    const QString senderCall = labelForId(srcId, false);
    const QString threadTitle = labelForId(peerId, isGroup);

    QVariantMap row {
        { QStringLiteral("id"), nextMessageId() },
        { QStringLiteral("threadKey"), key },
        { QStringLiteral("peerId"), peerId },
        { QStringLiteral("senderId"), srcId },
        { QStringLiteral("senderCallsign"), senderCall },
        { QStringLiteral("peerCallsign"), threadTitle },
        { QStringLiteral("peerName"), subtitleForId(peerId, isGroup) },
        { QStringLiteral("isGroup"), isGroup },
        { QStringLiteral("destinationId"), dstId },
        { QStringLiteral("direction"), QStringLiteral("in") },
        { QStringLiteral("text"), text },
        { QStringLiteral("timestamp"), ts },
        { QStringLiteral("displayTime"), formatDisplayTime(ts) },
        { QStringLiteral("status"), Received },
        { QStringLiteral("statusDetail"), QString() },
        { QStringLiteral("format"), -1 },
        { QStringLiteral("formatName"), format },
        { QStringLiteral("unread"), unread }
    };

    int oldUnread = 0;
    qint64 replyMs = -1;
    {
        QMutexLocker lock(&m_mutex);
        oldUnread = m_unreadCountCache;
        // Round trip: time since our latest unanswered private message to this peer
        if (!isGroup) {
            const QDateTime now = QDateTime::currentDateTimeUtc();
            for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
                const QVariantMap prev = m_messages.at(i).toMap();
                if (prev.value(QStringLiteral("threadKey")).toString() != key)
                    continue;
                if (prev.value(QStringLiteral("direction")).toString() == QLatin1String("out")) {
                    const QDateTime sent = QDateTime::fromString(
                        prev.value(QStringLiteral("timestamp")).toString(), Qt::ISODateWithMs);
                    const qint64 ms = sent.isValid() ? sent.msecsTo(now) : -1;
                    if (ms >= 0 && ms <= 120000)
                        replyMs = ms;
                }
                break;
            }
        }
        if (replyMs >= 0)
            row.insert(QStringLiteral("replyMs"), replyMs);
        m_messages.append(row);
        trimHistoryLocked();
        rebuildCachesLocked();
    }
    scheduleSave();
    notifyModels(oldUnread);

    if (replyMs >= 0 && m_droidStar) {
        emit m_droidStar->update_log(QStringLiteral("DMR SMS: reply from %1 after %2 s")
                                         .arg(senderCall).arg(replyMs / 1000.0, 0, 'f', 1));
    }

    emit messageReceived(peerId, senderCall, previewFor(text), isGroup, key);
    if (m_vuidUpdater && srcId >= 1000000U)
        m_vuidUpdater->fetchFirstNameFromAPI(srcId);

    if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
        ios_notifications_post_sms_alert(
            key, tr("DMR message from %1").arg(senderCall), previewFor(text));
    }
}

void DmrSmsManager::onSmsSendStatus(int msgId, int status, const QString &detail)
{
    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
            QVariantMap row = m_messages.at(i).toMap();
            if (row.value(QStringLiteral("id")).toInt() == msgId) {
                // Do not downgrade a message that already received a network ACK
                if (row.value(QStringLiteral("status")).toInt() == Delivered && status == Sent)
                    break;
                row.insert(QStringLiteral("status"), status);
                row.insert(QStringLiteral("statusDetail"), detail);
                if (detail == QLatin1String("ODTP") ||
                    (!detail.isEmpty() &&
                     row.value(QStringLiteral("formatName")).toString().isEmpty())) {
                    row.insert(QStringLiteral("formatName"), detail);
                }
                m_messages[i] = row;
                rebuildCachesLocked();
                changed = true;
                break;
            }
        }
    }
    if (changed) {
        scheduleSave();
        emit threadsChanged();
    }
}

void DmrSmsManager::onSmsAckReceived(uint32_t srcId, bool success)
{
    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
            QVariantMap row = m_messages.at(i).toMap();
            if (row.value(QStringLiteral("peerId")).toUInt() == srcId &&
                row.value(QStringLiteral("direction")).toString() == QLatin1String("out") &&
                row.value(QStringLiteral("status")).toInt() != Delivered) {
                row.insert(QStringLiteral("status"), success ? Delivered : Failed);
                row.insert(QStringLiteral("statusDetail"),
                           success ? tr("Delivered (ACK)") : tr("Delivery NACK"));
                m_messages[i] = row;
                rebuildCachesLocked();
                changed = true;
                break;
            }
        }
    }
    if (changed) {
        scheduleSave();
        emit threadsChanged();
    }
}

void DmrSmsManager::onUserLookupReady(unsigned int dmrId, const QString &callsign,
                                      const QString &name, const QString &country)
{
    const QString enrichedName = (!name.isEmpty() && !country.isEmpty())
        ? (name + QStringLiteral(" • ") + country)
        : (!name.isEmpty() ? name : country);

    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        for (int i = 0; i < m_messages.size(); ++i) {
            QVariantMap row = m_messages.at(i).toMap();
            if (!row.value(QStringLiteral("isGroup")).toBool() &&
                row.value(QStringLiteral("peerId")).toUInt() == dmrId) {
                if (!callsign.isEmpty() && row.value(QStringLiteral("peerCallsign")).toString() != callsign) {
                    row.insert(QStringLiteral("peerCallsign"), callsign);
                    changed = true;
                }
                if (!enrichedName.isEmpty() && row.value(QStringLiteral("peerName")).toString() != enrichedName) {
                    row.insert(QStringLiteral("peerName"), enrichedName);
                    changed = true;
                }
                m_messages[i] = row;
            }
        }
        if (changed)
            rebuildCachesLocked();
    }
    if (changed) {
        scheduleSave();
        emit threadsChanged();
    }
}

void DmrSmsManager::markThreadRead(const QString &threadKey)
{
    if (threadKey.isEmpty())
        return;
    int oldUnread = 0;
    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        oldUnread = m_unreadCountCache;
        for (int i = 0; i < m_messages.size(); ++i) {
            QVariantMap row = m_messages.at(i).toMap();
            if (row.value(QStringLiteral("threadKey")).toString() == threadKey &&
                row.value(QStringLiteral("unread")).toBool()) {
                row.insert(QStringLiteral("unread"), false);
                m_messages[i] = row;
                changed = true;
            }
        }
        if (changed)
            rebuildCachesLocked();
    }
    if (!changed)
        return;
    scheduleSave();
    notifyModels(oldUnread);
}

void DmrSmsManager::openThread(const QString &threadKey)
{
    m_openThread = threadKey;
    markThreadRead(threadKey);
    emit threadRequested(threadKey);
}

void DmrSmsManager::setActiveThread(const QString &threadKey)
{
    m_openThread = threadKey;
    if (!threadKey.isEmpty()) {
        markThreadRead(threadKey);
    }
}

void DmrSmsManager::deleteThread(const QString &threadKey)
{
    if (threadKey.isEmpty())
        return;
    int oldUnread = 0;
    bool changed = false;
    {
        QMutexLocker lock(&m_mutex);
        oldUnread = m_unreadCountCache;
        for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
            if (m_messages.at(i).toMap().value(QStringLiteral("threadKey")).toString() == threadKey) {
                m_messages.removeAt(i);
                changed = true;
            }
        }
        if (changed)
            rebuildCachesLocked();
    }
    if (!changed)
        return;
    if (m_openThread == threadKey)
        m_openThread.clear();
    scheduleSave();
    notifyModels(oldUnread);
}

void DmrSmsManager::clearAllHistory()
{
    int oldUnread = 0;
    {
        QMutexLocker lock(&m_mutex);
        if (m_messages.isEmpty())
            return;
        oldUnread = m_unreadCountCache;
        m_messages.clear();
        m_threadsCache.clear();
        m_unreadCountCache = 0;
    }
    m_openThread.clear();
    scheduleSave();
    notifyModels(oldUnread);
}

void DmrSmsManager::setDefaultFormat(int format)
{
    format = std::clamp(format, 0, 3);
    if (m_defaultFormat == format)
        return;
    m_defaultFormat = format;
    QSettings settings(QStringLiteral("dudetronics"), QStringLiteral("droidstar"));
    settings.setValue(QStringLiteral("DMR_SMS_FORMAT"), format);
    emit settingsChanged();
}

void DmrSmsManager::setSendPreambleCsbk(bool enabled)
{
    if (m_sendPreambleCsbk == enabled)
        return;
    m_sendPreambleCsbk = enabled;
    QSettings settings(QStringLiteral("dudetronics"), QStringLiteral("droidstar"));
    settings.setValue(QStringLiteral("DMR_SMS_CSBK"), enabled);
    emit settingsChanged();
}

uint32_t DmrSmsManager::aprsServiceIdForHost(const QString &hostName) const
{
    const QString upper = hostName.toUpper();
    QRegularExpression re(QStringLiteral("(?:BM[_ -]?)?(\\d{3})"));
    const QRegularExpressionMatch match = re.match(upper);
    if (match.hasMatch())
        return match.captured(1).toUInt() * 1000U + 999U;
    return 262999U;
}

void DmrSmsManager::trimHistoryLocked()
{
    constexpr int MAX_MESSAGES = 500;
    while (m_messages.size() > MAX_MESSAGES)
        m_messages.removeFirst();
}

void DmrSmsManager::notifyModels(int oldUnread)
{
    emit threadsChanged();
    if (oldUnread != unreadCount())
        emit unreadCountChanged();
}
