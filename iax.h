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

#ifndef IAX_H
#define IAX_H

#include "mode.h"

class IAX : public Mode
{
	Q_OBJECT
public:
	IAX();//QString callsign, QString username, QString password, QString node, QString host, int port, QString audioin, QString audioout);
	~IAX();
	void set_iax_params(QString username, QString password, QString callingname, QString node, QString host, int port);
	//uint8_t get_status(){ return m_status; }
	QString get_host() { return m_host; }
	int get_port() { return m_port; }
	int get_cnt() { return m_cnt; }
private slots:
	void deleteLater();
	void process_udp();
	void send_connect();
	void send_disconnect();
	void hostname_lookup(QHostInfo i);
	void send_registration(uint16_t dcall = 0);
	void send_calltoken_request();
	void on_connect_retry();
	void send_call();
	void send_call_auth();
	void send_ack(uint16_t, uint16_t, uint8_t, uint8_t, uint32_t ts = 0);
	void send_lag_response(uint32_t);
	void send_ping();
	void send_pong(uint32_t);
	void toggle_tx(bool);
	void start_tx();
	void stop_tx();
	void transmit();
	void process_rx_data();
	void send_voice_frame(int16_t *);
	void send_dtmf(QByteArray);
	void send_radio_key(bool);
	void connected();
private:
	void complete_connection() { connected(); }
	QString m_callsign;
	QString m_username;
	QString m_password;
	QString m_callingname;
	QByteArray m_calltoken;
	QString m_node;
	QString m_context;
	bool m_hasExplicitContext = false;
	QString m_host;
	int m_port = 0;
	uint16_t m_scallno;
	uint16_t m_dcallno;
	uint16_t m_regscallno;
	uint16_t m_regdcallno;
	uint16_t m_authMethods = 0;
	int m_connectRetries = 0;
	int m_id = 0;
	qint64 m_timestamp = 0;
	uint8_t m_regstat = 0;
	QByteArray m_md5seed;
	QTimer *m_regtimer = nullptr;
	QTimer *m_pingtimer = nullptr;
	uint8_t m_iseq;
	uint8_t m_oseq;
	QQueue<int16_t> m_audioq;
	uint32_t m_rxjitter;
	uint32_t m_rxloss;
	uint32_t m_rxframes;
	uint16_t m_rxdelay;
	uint32_t m_rxdropped;
	uint32_t m_rxooo;
	int m_cnt;
	bool m_wt = false;
	bool m_regreq = false;
	bool m_regAccepted = true;
	bool m_callSent = false;
	//qreal m_rxgain;
#ifdef USE_FLITE
	cst_voice *voice_slt;
	cst_voice *voice_kal;
	cst_voice *voice_awb;
	cst_wave *tts_audio;
#endif
};

#endif // IAX_H
