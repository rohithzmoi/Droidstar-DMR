/*
    Copyright (C) 2024 Rohith Namboothiri

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "LogHandler.h"
#include <QDir>
#include <QDebug>
#include <QTextStream>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDesktopServices>



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
    qDebug() << "Log file path:" << filePath;
    return filePath;
}

bool LogHandler::saveLog(const QString &fileName, const QJsonArray &logData)
{
    QString filePath = getFilePath(fileName);
    if (filePath.isEmpty()) {
        return false;
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
        return logData;
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

bool LogHandler::exportLogToCsv(const QString &fileName, const QJsonArray &logData) {
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
    lastSavedFilePath = filePath;
    return true;
}

bool LogHandler::exportLogToAdif(const QString &fileName, const QJsonArray &logData) {
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

    // Write the ADIF headers
    out << "ADIF Export\n";
    out << "<EOH>\n";  // End of Header

    // Write each QSO record in ADIF format
    for (int i = 0; i < logData.size(); ++i) {
        QJsonObject entry = logData[i].toObject();
        QString currentTime = entry["currentTime"].toString();
        QString qsoDate = currentTime.left(10).remove('-'); // Format: YYYYMMDD
        QString timeOn = currentTime.mid(11, 8).remove(':'); // Format: HHMMSS
        out << "<CALL:" << entry["callsign"].toString().length() << ">" << entry["callsign"].toString();
        out << "<BAND:4>70CM";  // Band is hardcoded as "70CM"
        out << "<MODE:12>DIGITALVOICE";    // Mode is set to "DIGITALVOICE , Do not change"
        // Include the first name in the ADIF record
        out << "<NAME:" << entry["fname"].toString().length() << ">" << entry["fname"].toString();

        out << "<QSO_DATE:" << qsoDate.length() << ">" << qsoDate;
        out << "<TIME_ON:6>" << timeOn;
        out << "<EOR>\n";  // End of Record, Important
    }

    file.close();
    qDebug() << "Log exported successfully to" << filePath;
    lastSavedFilePath = filePath;
    return true;
}


QString LogHandler::getFriendlyPath(const QString &fullPath) const {
    return fullPath.mid(fullPath.indexOf("/Documents/"));
}

void LogHandler::shareFile() {
#ifdef Q_OS_IOS
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
