#ifndef LOGHANDLER_H
#define LOGHANDLER_H

#include <QObject>
#include <QJsonArray>
#include <functional>

class LogHandler : public QObject
{
    Q_OBJECT
public:
    explicit LogHandler(QObject *parent = nullptr);

    Q_INVOKABLE void saveLogAsync(const QString &fileName, const QJsonArray &logData);
    Q_INVOKABLE void loadLogAsync(const QString &fileName, std::function<void(QJsonArray)> callback);

    Q_INVOKABLE bool saveLog(const QString &fileName, const QJsonArray &logData);
    Q_INVOKABLE QJsonArray loadLog(const QString &fileName);
    Q_INVOKABLE bool clearLog(const QString &fileName);
    Q_INVOKABLE bool exportLogToCsv(const QString &fileName, const QJsonArray &logData);
    Q_INVOKABLE QString getDSLogPath() const;
    Q_INVOKABLE QString getFriendlyPath(const QString &path) const;
    Q_INVOKABLE bool exportLogToAdif(const QString &fileName, const QJsonArray &logData);
    Q_INVOKABLE void shareFile();

private:
    QString getFilePath(const QString &fileName) const;
    QString lastSavedFilePath;
};

#ifdef Q_OS_IOS
extern void shareFileOnIOS(const QString &filePath); // Declare the iOS-specific function
#endif

#endif // LOGHANDLER_H
