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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QFileInfo>

#ifdef Q_OS_ANDROID
#include <QtCore/QJniObject>
#include <QtCore/QCoreApplication>
#endif

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

void LogHandler::shareFile(const QString &filePath) {
#ifdef Q_OS_IOS
    shareFileOnIOS(filePath); // iOS-specific sharing method
#elif defined(Q_OS_ANDROID)
    shareFileDirectly(filePath); // Android-specific sharing method
#else
    qWarning("File sharing is only implemented for iOS and Android.");
#endif
}
void LogHandler::shareFileDirectly(const QString &filePath) {
#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();

    if (context.isValid()) {
        QString relativeFilePath = filePath.section("Download/", 1);
        QJniObject javaFile("java/io/File", "(Ljava/lang/String;)V", QJniObject::fromString("/storage/emulated/0/Download/" + relativeFilePath).object<jstring>());

        if (javaFile.isValid()) {
            QString authority = "com.dmr.droidstardmr.fileprovider"; 
            QJniObject authorityObject = QJniObject::fromString(authority);
            QJniObject fileUri = QJniObject::callStaticObjectMethod(
                "androidx/core/content/FileProvider",
                "getUriForFile",
                "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
                context.object(),
                authorityObject.object<jstring>(),
                javaFile.object()
                );

            if (fileUri.isValid()) {
                QJniObject shareIntent("android/content/Intent", "(Ljava/lang/String;)V", QJniObject::fromString("android.intent.action.SEND").object());
                shareIntent.callObjectMethod("setType", "(Ljava/lang/String;)Landroid/content/Intent;", QJniObject::fromString("text/csv").object());
                shareIntent.callObjectMethod("putExtra", "(Ljava/lang/String;Landroid/os/Parcelable;)Landroid/content/Intent;", QJniObject::fromString("android.intent.extra.STREAM").object(), fileUri.object());

                shareIntent.callMethod<void>("addFlags", "(I)V", jint(1));

                context.callObjectMethod("startActivity", "(Landroid/content/Intent;)V", QJniObject::callStaticObjectMethod(
                                                                                             "android/content/Intent", "createChooser",
                                                                                             "(Landroid/content/Intent;Ljava/lang/CharSequence;)Landroid/content/Intent;",
                                                                                             shareIntent.object(),
                                                                                             QJniObject::fromString("Share File").object()
                                                                                             ).object());
            } else {
                qWarning("Failed to obtain file URI for sharing.");
            }
        } else {
            qWarning("Failed to create java.io.File object.");
        }
    } else {
        qWarning("Invalid context or file path object.");
    }
#endif
}

