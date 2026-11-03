#include "LogHandler.h"
#include <QDir>
#include <QDebug>
#include <QTextStream>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDesktopServices>
#include <QtConcurrent> // Include for running asynchronous tasks
#include <QMetaObject>
#include <QSaveFile>
#include <QMutexLocker>

LogHandler::LogHandler(QObject *parent) : QObject(parent)
{
}

QString LogHandler::getFilePath(const QString &fileName) const
{
    QString dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dirPath);

    if (!dir.exists()) {
        if (!dir.mkpath(dirPath)) {
            qDebug() << "Failed to create directory:" << dirPath;
            return QString();
        }
    }

    QString filePath = dirPath + "/" + fileName;
    return filePath;
}

void LogHandler::saveLogAsync(const QString &fileName, const QJsonArray &logData)
{
    {
        QMutexLocker locker(&m_mutex);
        m_cache.insert(fileName, logData);
    }
    (void)QtConcurrent::run([this, fileName, logData]() {
        const bool ok = saveLog(fileName, logData);
        // Ensure QML observers run on the UI thread
        QMetaObject::invokeMethod(this, [this, fileName, ok]() {
            if (ok) emit logSaved(fileName);
        }, Qt::QueuedConnection);
    });
}

void LogHandler::loadLogAsync(const QString &fileName, std::function<void(QJsonArray)> callback)
{
    (void)QtConcurrent::run([this, fileName, callback]() {
        QJsonArray logData = loadLog(fileName);
        callback(logData);
    });
}

bool LogHandler::saveLog(const QString &fileName, const QJsonArray &logData)
{
    QMutexLocker locker(&m_mutex);
    m_cache.insert(fileName, logData);
    QString filePath = getFilePath(fileName);
    if (filePath.isEmpty()) {
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open file for atomic writing:" << file.errorString();
        return false;
    }

    QJsonDocument doc(logData);
    file.write(doc.toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        qDebug() << "Failed to commit log file:" << file.errorString();
        return false;
    }
    return true;
}

QJsonArray LogHandler::loadLog(const QString &fileName)
{
    QMutexLocker locker(&m_mutex);
    auto cachedIt = m_cache.constFind(fileName);
    if (cachedIt != m_cache.cend()) {
        return cachedIt.value();
    }

    QString filePath = getFilePath(fileName);
    QJsonArray logData;

    QFile file(filePath);
    // If the log doesn't exist yet, treat as empty without logging an error.
    if (!file.exists()) {
        m_cache.insert(fileName, logData);
        return logData;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open file for reading:" << file.errorString();
        return logData;
    }

    QByteArray data = file.readAll();
    file.close();
    if (data.trimmed().isEmpty()) {
        m_cache.insert(fileName, logData);
        return logData;
    }

    QJsonDocument doc(QJsonDocument::fromJson(data));
    if (!doc.isNull() && doc.isArray()) {
        logData = doc.array();
    } else {
        qDebug() << "Failed to parse JSON log.";
    }

    m_cache.insert(fileName, logData);
    return logData;
}

bool LogHandler::clearLog(const QString &fileName)
{
    {
        QMutexLocker locker(&m_mutex);
        m_cache.insert(fileName, QJsonArray());
        QString filePath = getFilePath(fileName);
        QSaveFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            qDebug() << "Failed to clear log:" << file.errorString();
            return false;
        }
        file.write("[]");
        if (!file.commit()) {
            qDebug() << "Failed to commit cleared log:" << file.errorString();
            return false;
        }
    }
    QMetaObject::invokeMethod(this, [this, fileName]() {
        emit logCleared(fileName);
    }, Qt::QueuedConnection);
    return true;
}

QString LogHandler::getDSLogPath() const
{
    QString documentsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString dsLogPath = documentsPath + "/DSLog";
    QDir dsLogDir(dsLogPath);

    qDebug() << "Documents path: " << documentsPath;
    qDebug() << "DSLog path: " << dsLogPath;

    if (!dsLogDir.exists()) {
        if (!dsLogDir.mkpath(dsLogPath)) {
            qDebug() << "Failed to create DSLog directory.";
            return QString();
        } else {
            qDebug() << "DSLog directory created successfully.";
        }
    } else {
        qDebug() << "DSLog directory already exists.";
    }

    return dsLogPath;
}

static QString csvEscapeField(const QString &val)
{
    QString s = val;
    s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    s.replace(QLatin1Char('\r'), QLatin1Char(' '));
    s.replace(QLatin1Char('\n'), QStringLiteral(" | "));
    return QStringLiteral("\"") + s + QStringLiteral("\"");
}

static QString formatEntryTimestampedTranscript(const QJsonObject &entry)
{
    const QJsonArray turns = entry[QStringLiteral("transcriptTurns")].toArray();
    if (!turns.isEmpty()) {
        QStringList parts;
        for (const QJsonValue &v : turns) {
            const QJsonObject t = v.toObject();
            const QString timeStr = t[QStringLiteral("time")].toString().trimmed();
            const QString textStr = t[QStringLiteral("text")].toString().trimmed();
            if (textStr.isEmpty()) continue;
            if (!timeStr.isEmpty()) {
                parts.append(QStringLiteral("[%1] %2").arg(timeStr, textStr));
            } else {
                parts.append(textStr);
            }
        }
        if (!parts.isEmpty()) {
            return parts.join(QStringLiteral(" | "));
        }
    }

    QString fallback = entry[QStringLiteral("transcript")].toString().trimmed();
    fallback.replace(QLatin1Char('\r'), QLatin1Char(' '));
    fallback.replace(QLatin1Char('\n'), QStringLiteral(" | "));
    return fallback;
}

bool LogHandler::exportLogToCsv(const QString &fileName, const QJsonArray &logData, bool includeTranscripts)
{
    QString dsLogPath = getDSLogPath();
    if (dsLogPath.isEmpty()) {
        qDebug() << "DSLog path is not available.";
        return false;
    }

    QString filePath = dsLogPath + "/" + QFileInfo(fileName).fileName();

    qDebug() << "Attempting to save file at: " << filePath;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file for writing:" << file.errorString();
        return false;
    }

    QTextStream out(&file);

    out << "Sr.No,Callsign,DMR ID,TGID,Handle,Country,Time";
    out << (includeTranscripts ? ",Transcript\n" : "\n");

    for (int i = 0; i < logData.size(); ++i) {
        QJsonObject entry = logData[i].toObject();
        out << entry["serialNumber"].toInt() << ","
            << csvEscapeField(entry["callsign"].toString()) << ","
            << entry["dmrID"].toInt() << ","
            << entry["tgid"].toInt() << ","
            << csvEscapeField(entry["fname"].toString()) << ","
            << csvEscapeField(entry["country"].toString()) << ","
            << csvEscapeField(entry["currentTime"].toString());
        if (includeTranscripts) {
            out << "," << csvEscapeField(formatEntryTimestampedTranscript(entry));
        }
        out << "\n";
    }

    file.close();
    qDebug() << "Log exported successfully to" << filePath;
    lastSavedFilePath = filePath;
    return true;
}

bool LogHandler::exportLogToAdif(const QString &fileName, const QJsonArray &logData, bool includeTranscripts)
{
    QString dsLogPath = getDSLogPath();
    if (dsLogPath.isEmpty()) {
        qDebug() << "DSLog path is not available.";
        return false;
    }

    QString filePath = dsLogPath + "/" + QFileInfo(fileName).fileName();

    qDebug() << "Attempting to save ADIF file at: " << filePath;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file for writing:" << file.errorString();
        return false;
    }

    QTextStream out(&file);

    out << "ADIF Export\n";
    out << "<EOH>\n";

    for (int i = 0; i < logData.size(); ++i) {
        QJsonObject entry = logData[i].toObject();

        QString currentTime = entry["currentTime"].toString();
        QString qsoDate = currentTime.left(10).remove('-'); // Format: YYYYMMDD
        QString timeOn = currentTime.mid(11, 8).remove(':'); // Format: HHMMSS

        // Write each QSO record with valid ADIF tags
        // ADIF field lengths count bytes of the UTF-8 output, not UTF-16 code units
        const QString call = entry["callsign"].toString();
        const QString name = entry["fname"].toString();
        out << "<CALL:" << call.toUtf8().size() << ">" << call;
        out << "<BAND:4>70CM";  // Band is hardcoded as "70CM"
        out << "<MODE:12>DIGITALVOICE";    // Mode is set to "DIGITALVOICE"
        out << "<NAME:" << name.toUtf8().size() << ">" << name;

        out << "<QSO_DATE:" << qsoDate.length() << ">" << qsoDate;
        out << "<TIME_ON:6>" << timeOn;
        if (includeTranscripts) {
            const QString transcript = formatEntryTimestampedTranscript(entry);
            if (!transcript.isEmpty()) {
                out << "<COMMENT:" << transcript.toUtf8().size() << ">" << transcript;
            }
        }
        out << "<EOR>\n";  // End of Record
    }

    file.close();
    qDebug() << "Log exported successfully to" << filePath;
    lastSavedFilePath = filePath;
    return true;
}

// Extract and display a user-friendly path
QString LogHandler::getFriendlyPath(const QString &fullPath) const {
    return fullPath.mid(fullPath.indexOf("/Documents/"));
}

void LogHandler::shareFile() {
#if defined(Q_OS_IOS) && !defined(Q_OS_IOS_APP_EXTENSION)
    if (lastSavedFilePath.isEmpty()) {
        qDebug() << "No file has been saved to share.";
        return;
    }

    QFileInfo fileInfo(lastSavedFilePath);
    if (!fileInfo.exists()) {
        qDebug() << "File does not exist: " << lastSavedFilePath;
        return;
    }

    shareFileOnIOS(lastSavedFilePath);
#else
    qDebug() << "Share functionality is only implemented for iOS.";
#endif
}
