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

#ifndef LOGHANDLER_H
#define LOGHANDLER_H

#include <QObject>
#include <QJsonArray>

class LogHandler : public QObject
{
    Q_OBJECT
public:
    explicit LogHandler(QObject *parent = nullptr);

    Q_INVOKABLE bool saveLog(const QString &fileName, const QJsonArray &logData);
    Q_INVOKABLE QJsonArray loadLog(const QString &fileName);
    Q_INVOKABLE bool clearLog(const QString &fileName);
    Q_INVOKABLE bool exportLogToCsv(const QString &fileName, const QJsonArray &logData);
    Q_INVOKABLE QString getDSLogPath() const;  // Updated function name
    Q_INVOKABLE QString getFriendlyPath(const QString &path) const; // Mark as Q_INVOKABLE
    
    Q_INVOKABLE bool exportLogToAdif(const QString &fileName, const QJsonArray &logData);
   


private:
    QString getFilePath(const QString &fileName) const;
   
    
};

#endif // LOGHANDLER_H
