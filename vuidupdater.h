/*
    Copyright (C) 2025 Rohith Namboothiri

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
        m_lastRequestedId = data1;

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
    // Emits one complete record for callers (e.g., realtime QSO logger in App2026.qml).
    void userLookupReady(unsigned int dmrId, const QString &callsign, const QString &name, const QString &country);

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
        QString callsign = first.value("callsign").toString();
        unsigned int dmrId = first.value("radio_id").toInt();
        if (!dmrId) {
            // Some responses use "id"
            dmrId = first.value("id").toInt();
        }
        if (!dmrId) {
            // Fall back to what we requested (best-effort)
            dmrId = m_lastRequestedId;
        }

        qDebug() << "Parsed name:" << name;
        qDebug() << "Parsed country:" << country;

        setFetchedFirstName(name);
        setFetchedCountry(country);
        emit userLookupReady(dmrId, callsign, name, m_fetchedCountry);

        reply->deleteLater();
    }

private:
    QString m_fetchedFirstName;
    QString m_fetchedCountry;
    unsigned int m_lastRequestedId = 0;
    QNetworkAccessManager *networkAccessManager;
};

#endif // VUIDUPDATER_H
