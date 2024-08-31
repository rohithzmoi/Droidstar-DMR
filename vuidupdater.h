#ifndef VUIDUPDATER_H
#define VUIDUPDATER_H

#include <QObject>
#include <QDebug>  // For debugging purposes
#include <QString>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

class VUIDUpdater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString fetchedFirstName READ fetchedFirstName WRITE setFetchedFirstName NOTIFY fetchedFirstNameChanged)
    Q_PROPERTY(QString fetchedCountry READ fetchedCountry WRITE setFetchedCountry NOTIFY fetchedCountryChanged)

public:
    explicit VUIDUpdater(QObject *parent = nullptr) : QObject(parent), networkAccessManager(new QNetworkAccessManager(this)) {
        connect(networkAccessManager, &QNetworkAccessManager::finished, this, &VUIDUpdater::onNetworkReply);
    }

    Q_INVOKABLE void fetchFirstNameFromAPI(unsigned int data1) {
        if (data1) {
            QUrl url("https://radioid.net/api/dmr/user/?id=" + QString::number(data1));
            QNetworkRequest request(url);
            networkAccessManager->get(request);
        }
    }

    Q_INVOKABLE QString fetchedFirstName() const { return m_fetchedFirstName; }
    Q_INVOKABLE QString fetchedCountry() const { return m_fetchedCountry; }

/*
    Q_INVOKABLE void setFetchedFirstName(const QString &firstName) {
        if (m_fetchedFirstName != firstName) {
            m_fetchedFirstName = firstName;
            emit fetchedFirstNameChanged(firstName);
            qDebug() << "Emitting fetchedFirstNameChanged signal with name:" << firstName;
        }
    }
    
    Q_INVOKABLE void setFetchedCountry(const QString &country) {
            if (m_fetchedCountry != country) {
                m_fetchedCountry = country;
                emit fetchedCountryChanged(country);
                qDebug() << "Emitting fetchedCountryChanged signal with country:" << country;
            }
        } */

    Q_INVOKABLE void setFetchedFirstName(const QString &firstName) {
        m_fetchedFirstName = firstName;
        emit fetchedFirstNameChanged(firstName);
        qDebug() << "Emitting fetchedFirstNameChanged signal with name:" << firstName;
    }

    Q_INVOKABLE void setFetchedCountry(const QString &country) {
        QString modifiedCountry = country;

        if (country == "United States") {
            modifiedCountry = "US";
        } else if (country == "United Kingdom") {
            modifiedCountry = "UK";
        }

        m_fetchedCountry = modifiedCountry;
        emit fetchedCountryChanged(modifiedCountry);
        qDebug() << "Emitting fetchedCountryChanged signal with country:" << modifiedCountry;
    }



signals:
    void fetchedFirstNameChanged(const QString &firstName);
    void fetchedCountryChanged(const QString &country);

private slots:
    void onNetworkReply(QNetworkReply *reply) {
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray response_data = reply->readAll();
            QJsonDocument json = QJsonDocument::fromJson(response_data);
            if (!json.isNull()) {
                QJsonObject jsonObject = json.object();
                QJsonArray results = jsonObject["results"].toArray();
                if (!results.isEmpty()) {
                    QJsonObject firstResult = results.first().toObject();
                    QString firstName = firstResult["fname"].toString();
                    QString country = firstResult["country"].toString();
                    qDebug() << "First name fetched from API:" << firstName;
                    qDebug() << "Country fetched from API:" << country;
                    setFetchedFirstName(firstName); // Update the first name property
                    setFetchedCountry(country); // Update the country property
                }
            }
        } else {
            qDebug() << "Network error:" << reply->errorString();
        }
        reply->deleteLater();
    }

private:
    QString m_fetchedFirstName;
    QString m_fetchedCountry;
    QNetworkAccessManager *networkAccessManager;
};

#endif // VUIDUPDATER_H
