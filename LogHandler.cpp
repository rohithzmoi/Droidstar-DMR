#include "LogHandler.h"
#include <QDir>
#include <QDebug>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QFileInfo>

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
    qDebug() << "Log file path:" << filePath;  // Log the file path
    return filePath;
}

bool LogHandler::saveLog(const QString &fileName, const QJsonArray &logData)
{
    QString filePath = getFilePath(fileName);
    if (filePath.isEmpty()) {
        return false;  // Failed to create directory, so return false
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open file for writing:" << file.errorString();
        return false;
    }
    QJsonDocument doc(logData);
    file.write(doc.toJson());
    file.close();
    qDebug() << "Log saved successfully.";
    return true;
}

QJsonArray LogHandler::loadLog(const QString &fileName)
{
    QString filePath = getFilePath(fileName);
    QJsonArray logData;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open file for reading:" << file.errorString();
        return logData;  // Return empty array if the file can't be opened
    }

    QByteArray data = file.readAll();
    QJsonDocument doc(QJsonDocument::fromJson(data));
    if (!doc.isNull() && doc.isArray()) {
        logData = doc.array();
        qDebug() << "Log loaded successfully.";
    } else {
        qDebug() << "Failed to parse JSON log.";
    }

    file.close();
    return logData;
}

bool LogHandler::clearLog(const QString &fileName)
{
    QString filePath = getFilePath(fileName);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.close();
        qDebug() << "Log cleared successfully.";
        return true;
    }
    qDebug() << "Failed to clear log:" << file.errorString();
    return false;
}

QString LogHandler::getDSLogPath() const {
    QString externalStoragePath = "/storage/emulated/0/Download";
    QString dsLogPath = externalStoragePath + "/DSLog";
    QDir dsLogDir(dsLogPath);

    qDebug() << "External storage path: " << externalStoragePath;  // Debugging line
    qDebug() << "DSLog path: " << dsLogPath;  // Debugging line

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

bool LogHandler::exportLogToCsv(const QString &fileName, const QJsonArray &logData) {
    QString dsLogPath = getDSLogPath();
    if (dsLogPath.isEmpty()) {
        qDebug() << "DSLog path is not available.";
        return false;
    }

    // Ensure that only the filename is appended to the directory path
    QString filePath = dsLogPath + "/" + QFileInfo(fileName).fileName();

    qDebug() << "Attempting to save file at: " << filePath;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file for writing:" << file.errorString();
        return false;
    }

    QTextStream out(&file);

    // Write the CSV headers
    out << "Sr.No,Callsign,DMR ID,TGID,Handle,Country,Time\n";

    // Write the log data to the CSV file
    for (int i = 0; i < logData.size(); ++i) {
        QJsonObject entry = logData[i].toObject();
        out << entry["serialNumber"].toInt() << ","
            << entry["callsign"].toString() << ","
            << entry["dmrID"].toInt() << ","
            << entry["tgid"].toInt() << ","
            << entry["fname"].toString() << ","
            << entry["country"].toString() << ","
            << entry["currentTime"].toString() << "\n";
    }

    file.close();
    qDebug() << "Log exported successfully to" << filePath;
    return true;
}

bool LogHandler::exportLogToAdif(const QString &fileName, const QJsonArray &logData) {
    QString dsLogPath = getDSLogPath();
    if (dsLogPath.isEmpty()) {
        qDebug() << "DSLog path is not available.";
        return false;
    }

    QString filePath = dsLogPath + "/" + QFileInfo(fileName).fileName(); // Ensure the file name is properly handled

    qDebug() << "Attempting to save ADIF file at: " << filePath;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file for writing:" << file.errorString();
        return false;
    }

    QTextStream out(&file);

    // Write the ADIF headers
    out << "ADIF Export\n";
    out << "<EOH>\n";  // End of Header

    // Write each QSO record in ADIF format
    for (int i = 0; i < logData.size(); ++i) {
        QJsonObject entry = logData[i].toObject();

        // Extract and format date and time
        QString currentTime = entry["currentTime"].toString();
        QString qsoDate = currentTime.left(10).remove('-'); // Format: YYYYMMDD
        QString timeOn = currentTime.mid(11, 8).remove(':'); // Format: HHMMSS

        // Write each QSO record with valid ADIF tags
        out << "<CALL:" << entry["callsign"].toString().length() << ">" << entry["callsign"].toString();
        out << "<BAND:4>70CM";  // Band is hardcoded as "70CM"
        out << "<MODE:12>DIGITALVOICE";    // Mode is set to "DIGITALVOICE"
        // Include the first name in the ADIF record
        out << "<NAME:" << entry["fname"].toString().length() << ">" << entry["fname"].toString();
        out << "<QSO_DATE:" << qsoDate.length() << ">" << qsoDate;
        out << "<TIME_ON:6>" << timeOn;
        out << "<EOR>\n";  // End of Record
    }

    file.close();
    qDebug() << "Log exported successfully to" << filePath;
    return true;
}


// Extract and display a user-friendly path
QString LogHandler::getFriendlyPath(const QString &fullPath) const {
    return fullPath.mid(fullPath.indexOf("/Download/"));
}
