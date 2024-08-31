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
    //QString getDownloadsPath() const;

};

#endif // LOGHANDLER_H
