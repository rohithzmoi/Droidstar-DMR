/*
    Copyright (C) 2019-2021 Doug McLain
    Modifications Copyright (C) 2024 Rohith Namboothiri

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

#ifndef DMR_H
#define DMR_H

#include "mode.h"
#include "DMRDefines.h"
#include "cbptc19696.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include "SignalEmitter.h"
#include <QTimer>



class DroidStar;



class DMR : public Mode

{
    Q_OBJECT
    
    
  // Q_PROPERTY(QString m_firstName READ getFirstName WRITE setFirstName NOTIFY firstNameChanged)
   // Q_PROPERTY(QString m_firstName READ firstName WRITE setFirstName NOTIFY firstNameChanged)

    

public:
    DMR();
    ~DMR();
    
    //void setDroidStar(DroidStar *droidStar);
   
    //Q_INVOKABLE QString getFirstName() const { return m_firstName; }
      //  void setFirstName(const QString &name);
    //QString get_firstName() const;
    
   // Q_INVOKABLE void fetchFirstName(uint32_t srcId);

   // Q_INVOKABLE   QString firstName() const;
   // Q_INVOKABLE  void setFirstName(const QString &name);
    
    
    
    
    //QString firstName() const;
     // void setFirstName(const QString &name);
    //QString get_firstName() const;
    
    //Q_INVOKABLE void fetchFirstName(uint32_t srcId);
    
    void set_dmr_params(uint8_t essid, QString password, QString lat, QString lon, QString location, QString desc, QString freq, QString url, QString swid, QString pkid, QString options);
    uint8_t * get_eot();
    
    
    //QString firstName() const;  // Getter
        //void setFirstName(const QString &name);  // Setter

        //void fetchFirstName(uint32_t srcId);

    signals:
        //void firstNameChanged();  // Notifier for Q_PROPERTY

        //void firstNameReceived(const QString &firstName);
       // void firstNameChanged(const QString &name);
       

    

private slots:
    void process_udp();
   // void onNetworkReply(QNetworkReply *reply); //
    //void fetchFirstName(int dmrId);//
    //void handleFirstName(const QString &firstName);  // Declare the slot here

    void process_rx_data();
    void process_modem_data(QByteArray);
    void get_ambe();
    void send_ping();
    void send_disconnect();
    void transmit();
    void hostname_lookup(QHostInfo i);
    void dmr_tgid_changed(int id) { m_txdstid = id; }
    void dmrpc_state_changed(int p){m_flco = p ? FLCO_USER_USER : FLCO_GROUP; }
    void cc_changed(int cc) {m_txcc = cc;}
    void slot_changed(int s) {m_txslot = s + 1; }
    void send_frame();
private:
    uint32_t m_essid;
    //QNetworkAccessManager* networkManager;
    //SignalEmitter *signalEmitter;
   
    //void fetchFirstName(uint32_t srcId);
    //QString m_firstName; // Declare m_firstName
    QString m_password;
    QString m_lat;
    QString m_lon;
    QString m_location;
    QString m_desc;
    QString m_freq;
    QString m_url;
    QString m_swid;
    QString m_pkid;
    uint32_t m_txsrcid;
    uint32_t m_txdstid;
    uint32_t m_txstreamid;
    uint32_t m_currentSrcId; //for fetching handle
    uint8_t m_txslot;
    uint8_t m_txcc;
    uint8_t packet_size;
    uint8_t m_ambe[27];
    uint32_t m_defsrcid;
    uint8_t m_dmrFrame[55];
    uint8_t m_dataType;
    uint32_t m_dmrcnt;
    FLCO m_flco;
    CBPTC19696 m_bptc;
    bool m_raw[128U];
    bool m_data[72U];
    QString m_options;
    //QString m_firstName;

    void byteToBitsBE(uint8_t byte, bool* bits);
    void bitsToByteBE(const bool* bits, uint8_t& byte);
    void build_frame();
    void encode_header(uint8_t);
    void encode_data();
    void encode16114(bool* d);
    void encode_qr1676(uint8_t* data);
    void get_slot_data(uint8_t* data);
    void lc_get_data(uint8_t*);
    void lc_get_data(bool* bits);
    void encode_embedded_data();
    uint8_t get_embedded_data(uint8_t* data, uint8_t n);
    void get_emb_data(uint8_t* data, uint8_t lcss);
    void full_lc_encode(uint8_t* data, uint8_t type);
    void addDMRDataSync(uint8_t* data, bool duplex);
    void addDMRAudioSync(uint8_t* data, bool duplex);
    void setup_connection();
};


#endif // DMR_H
