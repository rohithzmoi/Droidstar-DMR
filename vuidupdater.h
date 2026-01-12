#ifndef VUIDUPDATER_H
#define VUIDUPDATER_H

#include <QObject>
#include <QDebug>
#include <QString>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

class VUIDUpdater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fetchedFirstName READ fetchedFirstName WRITE setFetchedFirstName NOTIFY fetchedFirstNameChanged)
    Q_PROPERTY(QString fetchedCountry READ fetchedCountry WRITE setFetchedCountry NOTIFY fetchedCountryChanged)

public:
    explicit VUIDUpdater(QObject *parent = nullptr)
        : QObject(parent),
          networkAccessManager(new QNetworkAccessManager(this))
    {
        connect(networkAccessManager, &QNetworkAccessManager::finished,
                this, &VUIDUpdater::onNetworkReply);
    }

    Q_INVOKABLE void fetchFirstNameFromAPI(unsigned int data1)
    {
        if (!data1) return;

        QUrl url("https://radioid.net/api/users?id=" + QString::number(data1));
        QNetworkRequest request(url);

        // Keep-Alive is optional; not harmful
        request.setRawHeader("Connection", "Keep-Alive");

        networkAccessManager->get(request);
    }

    QString fetchedFirstName() const { return m_fetchedFirstName; }
    QString fetchedCountry() const { return m_fetchedCountry; }

    void setFetchedFirstName(const QString &firstName)
    {
        if (m_fetchedFirstName == firstName) return;
        m_fetchedFirstName = firstName;
        emit fetchedFirstNameChanged(m_fetchedFirstName);
        qDebug() << "Emitting fetchedFirstNameChanged signal with name:" << m_fetchedFirstName;
    }

    void setFetchedCountry(const QString &country)
    {
        QString modifiedCountry = country;

        if (modifiedCountry == "United States") {
            modifiedCountry = "US";
        } else if (modifiedCountry == "United Kingdom") {
            modifiedCountry = "UK";
        }

        if (m_fetchedCountry == modifiedCountry) return;
        m_fetchedCountry = modifiedCountry;
        emit fetchedCountryChanged(m_fetchedCountry);
        qDebug() << "Emitting fetchedCountryChanged signal with country:" << m_fetchedCountry;
    }

signals:
    void fetchedFirstNameChanged(const QString &firstName);
    void fetchedCountryChanged(const QString &country);

private slots:
    void onNetworkReply(QNetworkReply *reply)
    {
        if (!reply) return;

        if (reply->error() != QNetworkReply::NoError) {
            qDebug() << "Network error:" << reply->errorString();
            reply->deleteLater();
            return;
        }

        const QByteArray response_data = reply->readAll();
        qDebug() << "onNetworkReply received:" << response_data;

        QJsonParseError parseError;
        QJsonDocument json = QJsonDocument::fromJson(response_data, &parseError);

        if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
            qDebug() << "Failed to parse JSON response:" << parseError.errorString();
            reply->deleteLater();
            return;
        }

        QJsonObject jsonObject = json.object();

        // radioid.net response (as per your logs) uses "rows": [ { "name": "...", "country": "..." } ]
        // We'll also support older/alternate formats safely.
        QJsonArray list = jsonObject.value("rows").toArray();
        if (list.isEmpty()) {
            list = jsonObject.value("results").toArray();
        }

        if (list.isEmpty()) {
            qDebug() << "rows/results array is empty or missing.";
            reply->deleteLater();
            return;
        }

        QJsonObject first = list.first().toObject();

        // Prefer "name", fallback to "fname" if needed
        QString name = first.value("name").toString();
        if (name.isEmpty()) {
            name = first.value("fname").toString();
        }

        QString country = first.value("country").toString();

        qDebug() << "Parsed name:" << name;
        qDebug() << "Parsed country:" << country;

        setFetchedFirstName(name);
        setFetchedCountry(country);

        reply->deleteLater();
    }

private:
    QString m_fetchedFirstName;
    QString m_fetchedCountry;
    QNetworkAccessManager *networkAccessManager;
};

#endif // VUIDUPDATER_H
