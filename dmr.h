/*
    Copyright (C) 2019-2021 Doug McLain

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

class DMR : public Mode
{
    Q_OBJECT
public:
    DMR();
    ~DMR();
    void set_dmr_params(uint8_t essid, QString password, QString lat, QString lon, QString location, QString desc, QString freq, QString url, QString swid, QString pkid, QString options);
    void set_odtp_mode(bool enabled, int fallbackHbPort = 62031);
    void set_tgif_mode(bool enabled) { m_isTgif = enabled; }
    bool is_odtp() const { return m_odtp; }
    bool is_tgif() const { return m_isTgif; }
    uint8_t * get_eot();
private slots:
    void process_udp();
    void process_rx_data();
    void process_modem_data(QByteArray);
    void get_ambe();
    void send_ping();
    void send_disconnect();
    void transmit();
    void hostname_lookup(QHostInfo i);
    void dmr_tgid_changed(int id);
    void dmrpc_state_changed(int p);
    void relink_talkgroup();
    void onAutoLinkTick();
    void cc_changed(int cc) { m_txcc = cc; m_modeinfo.cc = cc; }
    void slot_changed(int s) { m_txslot = s + 1; }
    void send_frame();
private:
    enum AutoLinkState {
        AUTOLINK_IDLE = 0,
        AUTOLINK_TX_UNLINK_4000,
        AUTOLINK_WAIT_4000_CLEAR,
        AUTOLINK_TX_TARGET
    };

    uint32_t m_essid;
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
    uint32_t m_linkedGroupTg;
    uint32_t m_unlinkedStaleTg;
    uint32_t m_linkGeneration;
    QTimer *m_linkTimer;
    AutoLinkState m_autoLinkState;
    uint32_t m_linkTargetTg;
    uint32_t m_linkActiveDst;
    uint32_t m_linkStreamId;
    uint32_t m_linkDmrCnt;
    int m_linkFramesRemaining;
    qint64 m_wait4000StartMs;
    qint64 m_last4000PktMs;
    bool m_saw4000Rx;
    bool m_4000EotReceived;
    uint8_t m_silenceAmbe27[27];
    bool m_silenceAmbeReady;
    uint32_t m_txstreamid;
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

    // BrandMeister Open DMR Terminal Protocol (ODTP / Rewind) state
    bool m_odtp;
    bool m_isTgif;
    bool m_tgifTriedDefaultPass;
    QNetworkAccessManager *m_tgifNam;
    int m_hbFallbackPort;
    uint32_t m_odtpSeq[2];
    int m_odtpAuthAttempts;
    int m_odtpConnectRetries;
    bool m_odtpConfigured;
    uint32_t m_odtpSubscribedTg;
    bool m_odtpSubscribedPrivateSelf;
    uint32_t m_odtpRxSrcId;
    uint32_t m_odtpRxDstId;
    bool m_odtpRxIsPrivate;
    QString m_odtpRxSrcCall;
    uint16_t m_odtpRxFrameCnt;

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
    void stop_rx_stream();
    void prepare_silence_ambe();
    bool send_auto_link_step(uint32_t dstid);
    void abort_auto_link_stream();
    bool send_quick_call(uint32_t dstid, FLCO flco);
    void schedule_group_link(bool force);
    void request_tgif_talkgroup_switch(uint32_t targetTg, bool forceFallback = false);
    bool shouldAcceptRxPacket(uint32_t srcid, uint32_t dstid, bool isPrivate) const;

    void send_odtp_packet(uint16_t type, uint16_t flags, const void *payload, uint16_t length, bool advanceSeq = true);
    void send_odtp_keepalive();
    void update_odtp_subscription(bool force = false);
    void process_odtp_udp(const QByteArray &buf);
    void handle_odtp_rx_header(uint32_t pktSrcId, uint32_t pktDstId, bool pktIsPrivate, const QString &srcCall, uint32_t seqNum);
};

#endif // DMR_H
