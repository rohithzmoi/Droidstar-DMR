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

#include "NetReminderManager.h"
#include "ios_notifications.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include <QTimeZone>
#include <QGuiApplication>
#include <QDebug>
#include <algorithm>

static NetReminderManager *s_netManagerInstance = nullptr;

static void handleNativeTapCallback(const char *netId)
{
    if (!s_netManagerInstance || !netId) return;
    const QString idStr = QString::fromUtf8(netId);
    QMetaObject::invokeMethod(s_netManagerInstance, [idStr]() {
        if (s_netManagerInstance) {
            const QVariantMap net = s_netManagerInstance->getNet(idStr);
            if (!net.isEmpty()) {
                emit s_netManagerInstance->netNotificationTapped(net);
            }
        }
    }, Qt::QueuedConnection);
}

static void handleNativeDeliveredCallback(const char *netId)
{
    if (!s_netManagerInstance) return;
    Q_UNUSED(netId);
    QMetaObject::invokeMethod(s_netManagerInstance, []() {
        if (s_netManagerInstance) {
            s_netManagerInstance->refreshCountdowns();
            s_netManagerInstance->rescheduleAll();
        }
    }, Qt::QueuedConnection);
}

static void handleNativePermissionCallback(bool granted)
{
    if (!s_netManagerInstance) return;
    QMetaObject::invokeMethod(s_netManagerInstance, [granted]() {
        if (s_netManagerInstance) {
            s_netManagerInstance->onPermissionResult(granted);
        }
    }, Qt::QueuedConnection);
}

NetReminderManager *NetReminderManager::instance()
{
    return s_netManagerInstance;
}

NetReminderManager::NetReminderManager(QObject *parent)
    : QObject(parent)
{
    s_netManagerInstance = this;

    ios_notifications_init(handleNativeTapCallback, handleNativeDeliveredCallback);
    ios_notifications_check_permission(handleNativePermissionCallback);

    loadFromDisk();
    rebuildEnrichedNets();
    rescheduleAll();

    // Update countdown strings every 30 seconds while app is open
    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(30000);
    connect(m_countdownTimer, &QTimer::timeout, this, &NetReminderManager::refreshCountdowns);
    m_countdownTimer->start();

    if (qApp) {
        connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive) {
                ios_notifications_check_permission(handleNativePermissionCallback);
                refreshCountdowns();
                rescheduleAll();
            }
        });
    }
}

NetReminderManager::~NetReminderManager()
{
    if (s_netManagerInstance == this) {
        s_netManagerInstance = nullptr;
    }
}

void NetReminderManager::onPermissionResult(bool granted)
{
    if (m_notificationsAuthorized != granted) {
        m_notificationsAuthorized = granted;
        emit permissionChanged();
    }
    if (granted) {
        rescheduleAll();
    }
}

void NetReminderManager::requestPermission()
{
    ios_notifications_request_permission(handleNativePermissionCallback);
}

int NetReminderManager::activeNetsCount() const
{
    int count = 0;
    for (const QJsonValue &v : m_nets) {
        if (v.toObject()[QStringLiteral("enabled")].toBool(true)) {
            ++count;
        }
    }
    return count;
}

QString NetReminderManager::storageFilePath() const
{
    const QString dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(dirPath);
    }
    return dirPath + QStringLiteral("/nets.json");
}

void NetReminderManager::loadFromDisk()
{
    QFile file(storageFilePath());
    if (!file.exists()) {
        m_nets = QJsonArray();
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (doc.isArray()) {
        m_nets = doc.array();
    }
}

void NetReminderManager::saveToDisk() const
{
    QFile file(storageFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    QJsonDocument doc(m_nets);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

QDate NetReminderManager::nthWeekdayOfMonth(int year, int month, int weekdayIso, int ordinal)
{
    const QDate firstOfMonth(year, month, 1);
    if (!firstOfMonth.isValid()) {
        return QDate();
    }
    const int daysInMonth = firstOfMonth.daysInMonth();
    const int clampedWeekday = std::clamp(weekdayIso, 1, 7);

    if (ordinal == 5) {
        // Last weekday of the month
        QDate d(year, month, daysInMonth);
        while (d.dayOfWeek() != clampedWeekday) {
            d = d.addDays(-1);
        }
        return d;
    }

    const int firstDow = firstOfMonth.dayOfWeek();
    int offset = (clampedWeekday - firstDow + 7) % 7;
    int day = 1 + offset + (std::clamp(ordinal, 1, 4) - 1) * 7;
    if (day > daysInMonth) {
        return QDate();
    }
    return QDate(year, month, day);
}

QVector<QDateTime> NetReminderManager::computeUpcomingOccurrences(const QJsonObject &net,
                                                                  const QDateTime &afterUtc,
                                                                  int maxCount)
{
    QVector<QDateTime> results;
    if (maxCount <= 0) {
        return results;
    }

    const QString timeStr = net[QStringLiteral("time")].toString(QStringLiteral("19:00"));
    const QStringList hm = timeStr.split(QLatin1Char(':'));
    const int hour = (hm.size() > 0) ? std::clamp(hm[0].toInt(), 0, 23) : 19;
    const int minute = (hm.size() > 1) ? std::clamp(hm[1].toInt(), 0, 59) : 0;
    const QTime netTime(hour, minute, 0);

    const bool useUtc = net[QStringLiteral("useUtc")].toBool(false);
    const QTimeZone tz = useUtc ? QTimeZone::utc() : QTimeZone::systemTimeZone();
    const QDateTime nowInTz = afterUtc.toTimeZone(tz);
    const QDate startDate = nowInTz.date();

    const QString recurrence = net[QStringLiteral("recurrence")].toString(QStringLiteral("weekly")).toLower();

    if (recurrence == QStringLiteral("daily")) {
        for (int d = 0; d < 95 && results.size() < maxCount; ++d) {
            const QDate candidateDate = startDate.addDays(d);
            const QDateTime dt(candidateDate, netTime, tz);
            if (dt.isValid() && dt.toUTC() > afterUtc) {
                results.append(dt.toUTC());
            }
        }
    } else if (recurrence == QStringLiteral("weekly")) {
        QVector<int> days;
        const QJsonArray arr = net[QStringLiteral("daysOfWeek")].toArray();
        for (const QJsonValue &v : arr) {
            const int dow = v.toInt();
            if (dow >= 1 && dow <= 7 && !days.contains(dow)) {
                days.append(dow);
            }
        }
        if (days.isEmpty()) {
            days.append(7); // Default to Sunday if empty
        }
        for (int d = 0; d < 120 && results.size() < maxCount; ++d) {
            const QDate candidateDate = startDate.addDays(d);
            if (days.contains(candidateDate.dayOfWeek())) {
                const QDateTime dt(candidateDate, netTime, tz);
                if (dt.isValid() && dt.toUTC() > afterUtc) {
                    results.append(dt.toUTC());
                }
            }
        }
    } else if (recurrence == QStringLiteral("monthly_date")) {
        const int targetDay = std::clamp(net[QStringLiteral("dayOfMonth")].toInt(1), 1, 31);
        int year = startDate.year();
        int month = startDate.month();
        for (int m = 0; m < 18 && results.size() < maxCount; ++m) {
            const QDate firstDay(year, month, 1);
            const int actualDay = std::min(targetDay, firstDay.daysInMonth());
            const QDate candidateDate(year, month, actualDay);
            const QDateTime dt(candidateDate, netTime, tz);
            if (dt.isValid() && dt.toUTC() > afterUtc) {
                results.append(dt.toUTC());
            }
            if (++month > 12) {
                month = 1;
                ++year;
            }
        }
    } else if (recurrence == QStringLiteral("monthly_weekday")) {
        const int ordinal = std::clamp(net[QStringLiteral("monthWeekOrdinal")].toInt(1), 1, 5);
        const int weekday = std::clamp(net[QStringLiteral("monthWeekday")].toInt(7), 1, 7);
        int year = startDate.year();
        int month = startDate.month();
        for (int m = 0; m < 18 && results.size() < maxCount; ++m) {
            const QDate candidateDate = nthWeekdayOfMonth(year, month, weekday, ordinal);
            if (candidateDate.isValid()) {
                const QDateTime dt(candidateDate, netTime, tz);
                if (dt.isValid() && dt.toUTC() > afterUtc) {
                    results.append(dt.toUTC());
                }
            }
            if (++month > 12) {
                month = 1;
                ++year;
            }
        }
    }

    return results;
}

QString NetReminderManager::formatScheduleSummary(const QJsonObject &net)
{
    static const QStringList shortDays = {
        QString(), QStringLiteral("Mon"), QStringLiteral("Tue"), QStringLiteral("Wed"),
        QStringLiteral("Thu"), QStringLiteral("Fri"), QStringLiteral("Sat"), QStringLiteral("Sun")
    };
    static const QStringList longDays = {
        QString(), QStringLiteral("Monday"), QStringLiteral("Tuesday"), QStringLiteral("Wednesday"),
        QStringLiteral("Thursday"), QStringLiteral("Friday"), QStringLiteral("Saturday"), QStringLiteral("Sunday")
    };
    static const QStringList ordinals = {
        QString(), QStringLiteral("1st"), QStringLiteral("2nd"), QStringLiteral("3rd"),
        QStringLiteral("4th"), QStringLiteral("Last")
    };

    const QString timeStr = net[QStringLiteral("time")].toString(QStringLiteral("19:00"));
    const bool useUtc = net[QStringLiteral("useUtc")].toBool(false);
    const QString tzLabel = useUtc ? QStringLiteral("UTC") : QStringLiteral("Local");
    const QString recurrence = net[QStringLiteral("recurrence")].toString(QStringLiteral("weekly")).toLower();

    if (recurrence == QStringLiteral("daily")) {
        return QStringLiteral("Daily • %1 %2").arg(timeStr, tzLabel);
    } else if (recurrence == QStringLiteral("weekly")) {
        const QJsonArray arr = net[QStringLiteral("daysOfWeek")].toArray();
        QVector<int> days;
        for (const QJsonValue &v : arr) {
            const int d = v.toInt();
            if (d >= 1 && d <= 7 && !days.contains(d)) days.append(d);
        }
        std::sort(days.begin(), days.end());
        if (days.size() == 7) {
            return QStringLiteral("Every day • %1 %2").arg(timeStr, tzLabel);
        }
        QStringList names;
        for (int d : days) names.append(shortDays[d]);
        const QString dayPart = names.isEmpty() ? QStringLiteral("Sun") : names.join(QStringLiteral(", "));
        return QStringLiteral("%1 • %2 %3").arg(dayPart, timeStr, tzLabel);
    } else if (recurrence == QStringLiteral("monthly_date")) {
        const int day = std::clamp(net[QStringLiteral("dayOfMonth")].toInt(1), 1, 31);
        return QStringLiteral("Monthly (Day %1) • %2 %3").arg(day).arg(timeStr, tzLabel);
    } else if (recurrence == QStringLiteral("monthly_weekday")) {
        const int ord = std::clamp(net[QStringLiteral("monthWeekOrdinal")].toInt(1), 1, 5);
        const int dow = std::clamp(net[QStringLiteral("monthWeekday")].toInt(7), 1, 7);
        return QStringLiteral("Monthly (%1 %2) • %3 %4").arg(ordinals[ord], longDays[dow], timeStr, tzLabel);
    }
    return QStringLiteral("%1 %2").arg(timeStr, tzLabel);
}

QString NetReminderManager::formatRemindersSummary(const QJsonArray &reminders)
{
    if (reminders.isEmpty()) {
        return QStringLiteral("No reminders");
    }
    QVector<int> mins;
    for (const QJsonValue &v : reminders) {
        const int m = v.toInt(-1);
        if (m >= 0 && !mins.contains(m) && mins.size() < 3) {
            mins.append(m);
        }
    }
    std::sort(mins.begin(), mins.end(), std::greater<int>());
    QStringList parts;
    for (int m : mins) {
        if (m == 0) {
            parts.append(QStringLiteral("At start"));
        } else if (m % 60 == 0 && m >= 60) {
            parts.append(QStringLiteral("%1h early").arg(m / 60));
        } else {
            parts.append(QStringLiteral("%1m early").arg(m));
        }
    }
    return parts.join(QStringLiteral(" • "));
}

QString NetReminderManager::formatCountdown(qint64 secsUntilStart, const QDateTime &nextStartUtc, bool useUtc)
{
    if (secsUntilStart <= 0 || !nextStartUtc.isValid()) {
        return QStringLiteral("Not scheduled");
    }
    const qint64 totalMins = (secsUntilStart + 59) / 60;
    const qint64 days = totalMins / (60 * 24);
    const qint64 hours = (totalMins % (60 * 24)) / 60;
    const qint64 mins = totalMins % 60;

    QString rel;
    if (days > 0) {
        rel = QStringLiteral("In %1d %2h").arg(days).arg(hours);
    } else if (hours > 0) {
        rel = QStringLiteral("In %1h %2m").arg(hours).arg(mins);
    } else {
        rel = QStringLiteral("In %1 min").arg(std::max<qint64>(1, mins));
    }

    const QDateTime localDt = nextStartUtc.toLocalTime();
    const QString localStr = localDt.toString(QStringLiteral("ddd HH:mm"));
    if (useUtc) {
        return QStringLiteral("%1 (%2 local)").arg(rel, localStr);
    }
    return QStringLiteral("%1 (%2)").arg(rel, localStr);
}

void NetReminderManager::rebuildEnrichedNets()
{
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();

    struct SortableNet {
        qint64 nextSecs;
        bool enabled;
        QVariantMap map;
    };
    QVector<SortableNet> list;
    list.reserve(m_nets.size());

    for (const QJsonValue &val : m_nets) {
        const QJsonObject obj = val.toObject();
        QVariantMap m = obj.toVariantMap();

        const bool enabled = obj[QStringLiteral("enabled")].toBool(true);
        const bool useUtc = obj[QStringLiteral("useUtc")].toBool(false);
        const QVector<QDateTime> nextList = computeUpcomingOccurrences(obj, nowUtc, 1);

        qint64 nextSecs = std::numeric_limits<qint64>::max();
        QDateTime nextUtc;
        if (!nextList.isEmpty()) {
            nextUtc = nextList.first();
            nextSecs = nowUtc.secsTo(nextUtc);
        }

        m[QStringLiteral("scheduleSummary")] = formatScheduleSummary(obj);
        m[QStringLiteral("remindersSummary")] = formatRemindersSummary(obj[QStringLiteral("reminders")].toArray());
        m[QStringLiteral("countdownText")] = enabled
                                             ? formatCountdown(nextSecs, nextUtc, useUtc)
                                             : QStringLiteral("Paused");
        m[QStringLiteral("nextStartSecs")] = (nextSecs < std::numeric_limits<qint64>::max()) ? nextSecs : -1;

        list.push_back({ enabled ? nextSecs : (std::numeric_limits<qint64>::max() - 1), enabled, m });
    }

    std::sort(list.begin(), list.end(), [](const SortableNet &a, const SortableNet &b) {
        if (a.enabled != b.enabled) {
            return a.enabled > b.enabled;
        }
        return a.nextSecs < b.nextSecs;
    });

    QVariantList enriched;
    enriched.reserve(list.size());
    for (const SortableNet &item : list) {
        enriched.append(item.map);
    }
    m_enrichedNets = enriched;
    emit netsChanged();
}

void NetReminderManager::refreshCountdowns()
{
    rebuildEnrichedNets();
}

QVariantMap NetReminderManager::getNet(const QString &netId) const
{
    for (const QJsonValue &val : m_nets) {
        const QJsonObject obj = val.toObject();
        if (obj[QStringLiteral("id")].toString() == netId) {
            QVariantMap m = obj.toVariantMap();
            m[QStringLiteral("scheduleSummary")] = formatScheduleSummary(obj);
            m[QStringLiteral("remindersSummary")] = formatRemindersSummary(obj[QStringLiteral("reminders")].toArray());
            return m;
        }
    }
    return QVariantMap();
}

QString NetReminderManager::saveNet(const QVariantMap &netData)
{
    QJsonObject obj;
    QString id = netData.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("name")] = netData.value(QStringLiteral("name")).toString().trimmed();
    obj[QStringLiteral("enabled")] = netData.value(QStringLiteral("enabled"), true).toBool();
    obj[QStringLiteral("mode")] = netData.value(QStringLiteral("mode"), QStringLiteral("DMR")).toString().trimmed();
    obj[QStringLiteral("host")] = netData.value(QStringLiteral("host")).toString().trimmed();
    obj[QStringLiteral("module")] = netData.value(QStringLiteral("module"), QStringLiteral("A")).toString().trimmed();
    obj[QStringLiteral("tgid")] = netData.value(QStringLiteral("tgid")).toString().trimmed();
    obj[QStringLiteral("recurrence")] = netData.value(QStringLiteral("recurrence"), QStringLiteral("weekly")).toString().trimmed().toLower();
    obj[QStringLiteral("time")] = netData.value(QStringLiteral("time"), QStringLiteral("19:00")).toString().trimmed();
    obj[QStringLiteral("useUtc")] = netData.value(QStringLiteral("useUtc"), false).toBool();
    obj[QStringLiteral("dayOfMonth")] = std::clamp(netData.value(QStringLiteral("dayOfMonth"), 1).toInt(), 1, 31);
    obj[QStringLiteral("monthWeekOrdinal")] = std::clamp(netData.value(QStringLiteral("monthWeekOrdinal"), 1).toInt(), 1, 5);
    obj[QStringLiteral("monthWeekday")] = std::clamp(netData.value(QStringLiteral("monthWeekday"), 7).toInt(), 1, 7);
    obj[QStringLiteral("notes")] = netData.value(QStringLiteral("notes")).toString().trimmed();

    QJsonArray daysArr;
    const QVariantList daysVar = netData.value(QStringLiteral("daysOfWeek")).toList();
    for (const QVariant &v : daysVar) {
        const int d = v.toInt();
        if (d >= 1 && d <= 7) {
            daysArr.append(d);
        }
    }
    if (daysArr.isEmpty()) {
        daysArr.append(7);
    }
    obj[QStringLiteral("daysOfWeek")] = daysArr;

    // Normalize up to 3 unique reminder offsets (in minutes)
    QJsonArray remArr;
    QVector<int> uniqueReminders;
    const QVariantList remVar = netData.value(QStringLiteral("reminders")).toList();
    for (const QVariant &v : remVar) {
        bool ok = false;
        const int mins = v.toInt(&ok);
        if (ok && mins >= 0 && mins <= 10080 && !uniqueReminders.contains(mins) && uniqueReminders.size() < 3) {
            uniqueReminders.append(mins);
        }
    }
    if (uniqueReminders.isEmpty()) {
        uniqueReminders.append(15);
    }
    std::sort(uniqueReminders.begin(), uniqueReminders.end(), std::greater<int>());
    for (int m : uniqueReminders) {
        remArr.append(m);
    }
    obj[QStringLiteral("reminders")] = remArr;

    bool updated = false;
    for (int i = 0; i < m_nets.size(); ++i) {
        if (m_nets[i].toObject()[QStringLiteral("id")].toString() == id) {
            m_nets[i] = obj;
            updated = true;
            break;
        }
    }
    if (!updated) {
        m_nets.append(obj);
    }

    saveToDisk();
    rebuildEnrichedNets();

    if (!m_notificationsAuthorized) {
        requestPermission();
    } else {
        rescheduleAll();
    }

    return id;
}

void NetReminderManager::deleteNet(const QString &netId)
{
    for (int i = 0; i < m_nets.size(); ++i) {
        if (m_nets[i].toObject()[QStringLiteral("id")].toString() == netId) {
            m_nets.removeAt(i);
            break;
        }
    }
    saveToDisk();
    rebuildEnrichedNets();
    rescheduleAll();
}

void NetReminderManager::setNetEnabled(const QString &netId, bool enabled)
{
    for (int i = 0; i < m_nets.size(); ++i) {
        QJsonObject obj = m_nets[i].toObject();
        if (obj[QStringLiteral("id")].toString() == netId) {
            obj[QStringLiteral("enabled")] = enabled;
            m_nets[i] = obj;
            break;
        }
    }
    saveToDisk();
    rebuildEnrichedNets();
    if (enabled && !m_notificationsAuthorized) {
        requestPermission();
    } else {
        rescheduleAll();
    }
}

void NetReminderManager::rescheduleAll()
{
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    const qint64 nowSecs = nowUtc.toSecsSinceEpoch();

    std::vector<DSScheduledAlert> candidates;

    for (const QJsonValue &val : m_nets) {
        const QJsonObject net = val.toObject();
        if (!net[QStringLiteral("enabled")].toBool(true)) {
            continue;
        }

        const QString netId = net[QStringLiteral("id")].toString();
        const QString netName = net[QStringLiteral("name")].toString(QStringLiteral("Ham Net"));
        const QString mode = net[QStringLiteral("mode")].toString();
        const QString host = net[QStringLiteral("host")].toString();
        const QString module = net[QStringLiteral("module")].toString();
        const QString tgid = net[QStringLiteral("tgid")].toString();
        const QJsonArray reminders = net[QStringLiteral("reminders")].toArray();

        // Build radio details subtitle (e.g. "DMR • BM_3102 • TG 91")
        QStringList radioParts;
        if (!mode.isEmpty()) radioParts.append(mode);
        if (!host.isEmpty()) radioParts.append(host);
        if ((mode == QStringLiteral("REF") || mode == QStringLiteral("XRF") ||
             mode == QStringLiteral("DCS") || mode == QStringLiteral("M17")) && !module.isEmpty()) {
            radioParts.append(QStringLiteral("Mod ") + module);
        }
        if ((mode == QStringLiteral("DMR") || mode == QStringLiteral("P25") || mode == QStringLiteral("NXDN")) && !tgid.isEmpty()) {
            radioParts.append(QStringLiteral("TG ") + tgid);
        }
        const QString subtitle = radioParts.join(QStringLiteral(" • "));

        // Compute next 20 occurrences per net so daily/weekly/monthly nets populate the 60-slot rolling window
        const QVector<QDateTime> occurrences = computeUpcomingOccurrences(net, nowUtc.addSecs(-3600), 20);

        for (const QDateTime &occUtc : occurrences) {
            const qint64 startSecs = occUtc.toSecsSinceEpoch();
            const QString localStartStr = occUtc.toLocalTime().toString(QStringLiteral("HH:mm"));

            int remIdx = 0;
            for (const QJsonValue &remVal : reminders) {
                if (remIdx >= 3) break;
                const int minsBefore = std::max(0, remVal.toInt(0));
                const qint64 fireSecs = startSecs - static_cast<qint64>(minsBefore) * 60LL;
                ++remIdx;

                if (fireSecs <= nowSecs + 2) {
                    continue;
                }

                DSScheduledAlert alert;
                alert.identifier = QStringLiteral("droidstar.net.%1.%2.%3")
                                   .arg(netId)
                                   .arg(startSecs)
                                   .arg(minsBefore);
                alert.netId = netId;
                alert.title = netName;
                alert.subtitle = subtitle;
                if (minsBefore == 0) {
                    alert.body = QStringLiteral("Starting now (%1). Tap to tune in DroidStar.").arg(localStartStr);
                } else if (minsBefore % 60 == 0 && minsBefore >= 60) {
                    alert.body = QStringLiteral("Starts in %1 hour%2 (at %3). Tap to tune.")
                                 .arg(minsBefore / 60)
                                 .arg(minsBefore >= 120 ? QStringLiteral("s") : QString())
                                 .arg(localStartStr);
                } else {
                    alert.body = QStringLiteral("Starts in %1 minutes (at %2). Tap to tune.")
                                 .arg(minsBefore)
                                 .arg(localStartStr);
                }
                alert.fireTimestampSecs = fireSecs;
                candidates.push_back(alert);
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const DSScheduledAlert &a, const DSScheduledAlert &b) {
        return a.fireTimestampSecs < b.fireTimestampSecs;
    });

    if (candidates.size() > 60U) {
        candidates.resize(60U);
    }

    ios_notifications_replace_scheduled_alerts(candidates);
}
