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

#include "iax.h"
#include "iaxdefines.h"
#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#ifdef USE_FLITE
extern "C" {
extern cst_voice * register_cmu_us_slt(const char *);
extern cst_voice * register_cmu_us_kal16(const char *);
extern cst_voice * register_cmu_us_awb(const char *);
}
#endif

#define DEBUG

IAX::IAX() :
	m_scallno(1),
	m_dcallno(0),
	m_regscallno(0x7fff),
	m_regdcallno(0),
	m_authMethods(0),
	m_connectRetries(0),
	m_iseq(0),
	m_oseq(0),
	m_rxjitter(0),
	m_rxloss(1),
	m_rxframes(0),
	m_rxdelay(0),
	m_rxdropped(0),
	m_rxooo(0),
	m_cnt(0),
	m_wt(false),
	m_regreq(false),
	m_regAccepted(true),
	m_callSent(false)
{
	m_mode = "IAX";
#ifdef USE_FLITE
	flite_init();
	voice_slt = register_cmu_us_slt(nullptr);
	voice_kal = register_cmu_us_kal16(nullptr);
	voice_awb = register_cmu_us_awb(nullptr);
#endif
}

IAX::~IAX()
{
}

void IAX::set_iax_params(QString username, QString password, QString callingname, QString node, QString host, int port)
{
	m_username = username.trimmed();
	m_password = password.trimmed();
	m_callingname = callingname.trimmed();
	m_node = node.trimmed();
	m_host = host.trimmed();
	m_port = port;

	// Strip optional URL scheme if user pasted one into custom hosts
	if (m_host.startsWith("iax://", Qt::CaseInsensitive) || m_host.startsWith("udp://", Qt::CaseInsensitive)) {
		m_host = m_host.mid(6).trimmed();
	}
	// If host contains host:port (single colon, non-IPv6), split out port
	if (m_host.count(':') == 1) {
		const int colonIdx = m_host.indexOf(':');
		const QString portPart = m_host.mid(colonIdx + 1).trimmed();
		bool ok = false;
		const int parsedPort = portPart.toInt(&ok);
		if (ok && parsedPort > 0 && parsedPort <= 65535) {
			m_host = m_host.left(colonIdx).trimmed();
			if (m_port <= 0) {
				m_port = parsedPort;
			}
		}
	}
	if (m_port <= 0) {
		m_port = 4569;
	}

	QStringList l = m_node.split('@');
	if(l.size() == 2){
		m_node = l.at(0).simplified();
		m_context = l.at(1).simplified();
		m_hasExplicitContext = !m_context.isEmpty();
	}
	else{
		m_context = "iax-client";
		m_hasExplicitContext = false;
	}

	if (m_username == "allstar-public" && m_password == "allstar") {
		m_wt = true;
	}
}

int16_t ulaw_decode(int8_t number)
{
   const uint16_t MULAW_BIAS = 33;
   uint8_t sign = 0, position = 0;
   int16_t decoded = 0;
   number = ~number;
   if (number & 0x80)
   {
	  number &= ~(1 << 7);
	  sign = -1;
   }
   position = ((number & 0xF0) >> 4) + 5;
   decoded = ((1 << position) | ((number & 0x0F) << (position - 4))
			 | (1 << (position - 5))) - MULAW_BIAS;
   return (sign == 0) ? (decoded) : (-(decoded));
}

int8_t ulaw_encode(int16_t number)
{
   const uint16_t MULAW_MAX = 0x1FFF;
   const uint16_t MULAW_BIAS = 33;
   uint16_t mask = 0x1000;
   uint8_t sign = 0;
   uint8_t position = 12;
   uint8_t lsb = 0;
   if (number < 0)
   {
	  number = -number;
	  sign = 0x80;
   }
   number += MULAW_BIAS;
   if (number > MULAW_MAX)
   {
	  number = MULAW_MAX;
   }
   for (; ((number & mask) != mask && position >= 5); mask >>= 1, position--)
		;
   lsb = (number >> (position - 4)) & 0x0f;
   return (~(sign | ((position - 5) << 4) | lsb));
}

void IAX::send_calltoken_request()
{
	if (!m_udp) return;
	uint16_t scall = htons(m_scallno | 0x8000);
	QByteArray out;
	out.append((char *)&scall, 2);
	out.append('\x00');
	out.append('\x00');
	out.append('\x00');
	out.append('\x00');
	out.append('\x00');
	out.append('\x00');
	out.append('\x00');
	out.append('\x00');
	out.append(AST_FRAME_IAX);
	out.append(IAX_COMMAND_NEW);
	out.append(IAX_IE_CALLTOKEN);
	out.append('\x00');

	m_udp->writeDatagram(out, m_address, m_port);

	if(m_debug){
		QDebug debug = qDebug();
		debug.noquote();
		QString s = "SEND:";
		for(int i = 0; i < out.size(); ++i){
			s += " " + QString("%1").arg((uint8_t)out.data()[i], 2, 16, QChar('0'));
		}
		debug << s;
	}
}

void IAX::send_call()
{
	if (!m_udp) return;
	m_callSent = true;
	const QByteArray nodeUtf8 = m_node.toUtf8();
	const QByteArray callUtf8 = m_modeinfo.callsign.trimmed().toUtf8();
	const QByteArray wtCallUtf8 = m_callingname.toUtf8();
	const QByteArray userUtf8 = m_username.toUtf8();

	uint16_t scall = htons(m_scallno | 0x8000);
	m_oseq = m_iseq = 0;
	QByteArray out;
	out.append((char *)&scall, 2);
	out.append('\x00');
	out.append('\x00');
	out.append('\x00');
	out.append('\x00');
	out.append('\x00');
	out.append('\x00');
	out.append(m_oseq);
	out.append(m_iseq);
	out.append(AST_FRAME_IAX);
	out.append(IAX_COMMAND_NEW);
	out.append(IAX_IE_VERSION);
	out.append(sizeof(short));
	out.append('\x00');
	out.append(IAX_PROTO_VERSION);
	out.append(IAX_IE_CALLED_NUMBER);
	if (m_wt) {
		out.append('\x01');
		out.append('s');
	} else {
		out.append(static_cast<char>(nodeUtf8.size()));
		out.append(nodeUtf8);
	}
	out.append(IAX_IE_CALLING_NUMBER);
	if (m_wt) {
		out.append(static_cast<char>(nodeUtf8.size()));
		out.append(nodeUtf8);
	} else {
		out.append('\x00');
	}
	out.append(IAX_IE_CALLING_NAME);
	if (m_wt) {
		out.append(static_cast<char>(wtCallUtf8.size()));
		out.append(wtCallUtf8);
	} else {
		out.append(static_cast<char>(callUtf8.size()));
		out.append(callUtf8);
	}
	out.append(IAX_IE_USERNAME);
	out.append(static_cast<char>(userUtf8.size()));
	out.append(userUtf8);
	out.append(IAX_IE_FORMAT);
	out.append(sizeof(int));
	out.append('\x00');
	out.append('\x00');
	out.append('\x00');
	out.append(AST_FORMAT_ULAW);
	out.append(IAX_IE_CALLTOKEN);
	out.append(static_cast<char>(m_calltoken.size()));
	out.append(m_calltoken);
	m_timestamp = QDateTime::currentMSecsSinceEpoch();
	m_udp->writeDatagram(out, m_address, m_port);

	if(m_debug){
		QDebug debug = qDebug();
		debug.noquote();
		QString s = "SEND:";
		for(int i = 0; i < out.size(); ++i){
			s += " " + QString("%1").arg((uint8_t)out.data()[i], 2, 16, QChar('0'));
		}
		debug << s;
	}
}

void IAX::send_call_auth()
{
	if (!m_udp) return;
	QByteArray out;
	uint16_t scall = htons(m_scallno | 0x8000);
	uint16_t dcall = htons(m_dcallno);
	uint32_t ts = htonl((QDateTime::currentMSecsSinceEpoch() - m_timestamp));

	out.append((char *)&scall, 2);
	out.append((char *)&dcall, 2);
	out.append((char *)&ts, 4);
	out.append(m_oseq);
	out.append(m_iseq);
	out.append(AST_FRAME_IAX);
	out.append(IAX_COMMAND_AUTHREP);

	if ((m_authMethods & IAX_AUTH_MD5) || !m_md5seed.isEmpty() || m_authMethods == 0) {
		QByteArray seed = m_md5seed;
		seed.append(m_password.toUtf8());
		QByteArray result = QCryptographicHash::hash(seed, QCryptographicHash::Md5).toHex();
		out.append(IAX_IE_MD5_RESULT);
		out.append(static_cast<char>(result.size()));
		out.append(result);
	} else {
		QByteArray pw = m_password.toUtf8();
		out.append(IAX_IE_PASSWORD);
		out.append(static_cast<char>(pw.size()));
		out.append(pw);
	}
	m_udp->writeDatagram(out, m_address, m_port);
#ifdef DEBUG
	fprintf(stderr, "SEND: ");
	for(int i = 0; i < out.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
}

void IAX::send_dtmf(QByteArray dtmf)
{
	QByteArray out;
	uint16_t scall = htons(m_scallno | 0x8000);
	uint16_t dcall = htons(m_dcallno);
	//uint32_t ts = htonl((QDateTime::currentMSecsSinceEpoch() - m_timestamp) + 3);
	for(int i = 0; i < dtmf.size(); ++i){
		uint32_t ts = htonl((QDateTime::currentMSecsSinceEpoch() - m_timestamp) + 3 + (i*3));
		out.clear();
		out.append((char *)&scall, 2);
		out.append((char *)&dcall, 2);
		out.append((char *)&ts, 4);
		out.append(m_oseq+i);
		out.append(m_iseq);
		out.append(AST_FRAME_DTMF);
		out.append(dtmf.data()[i]);
		m_udp->writeDatagram(out, m_address, m_port);
#ifdef DEBUG
		fprintf(stderr, "SEND: ");
		for(int i = 0; i < out.size(); ++i){
			fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
		}
		fprintf(stderr, "\n");
		fflush(stderr);
#endif
	}
}

void IAX::send_radio_key(bool key)
{
	QByteArray out;
	uint16_t scall = htons(m_scallno | 0x8000);
	uint16_t dcall = htons(m_dcallno);
	uint32_t ts = htonl((QDateTime::currentMSecsSinceEpoch() - m_timestamp));// + 3);
	out.clear();
	out.append((char *)&scall, 2);
	out.append((char *)&dcall, 2);
	out.append((char *)&ts, 4);
	out.append(m_oseq);
	out.append(m_iseq);
	out.append(AST_FRAME_CONTROL);
	out.append(key ? AST_CONTROL_KEY : AST_CONTROL_UNKEY);
	m_udp->writeDatagram(out, m_address, m_port);
#ifdef DEBUG
		fprintf(stderr, "SEND: ");
		for(int i = 0; i < out.size(); ++i){
			fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
		}
		fprintf(stderr, "\n");
		fflush(stderr);
#endif
}

void IAX::send_ping()
{
	if(++m_watchdog > 6){
		m_modeinfo.status = TIMEOUT;
		emit update(m_modeinfo);
	}
}

void IAX::send_pong(uint32_t ts)
{
	if (!m_udp) return;
	QByteArray out;
	uint16_t scall = htons(m_scallno | 0x8000);
	uint16_t dcall = htons(m_dcallno);
	ts = htonl(ts);
	uint32_t jitter = htonl(m_rxjitter);
	//uint32_t loss = htonl(m_rxloss);
	uint32_t frames = htonl(m_rxframes);
	uint16_t delay = htons(m_rxdelay);
	uint32_t dropped = htonl(m_rxdropped);
	uint32_t ooo = htonl(m_rxooo);

	out.append((char *)&scall, 2);
	out.append((char *)&dcall, 2);
	out.append((char *)&ts, 4);
	out.append(m_oseq);
	out.append(m_iseq);
	out.append(AST_FRAME_IAX);
	out.append(IAX_COMMAND_PONG);
	out.append(IAX_IE_RR_JITTER);
	out.append(sizeof(jitter));
	out.append((char *)&jitter, sizeof(jitter));
	out.append(IAX_IE_RR_LOSS);
	out.append(sizeof(m_rxloss));
	out.append((char *)&m_rxloss, sizeof(m_rxloss));
	out.append(IAX_IE_RR_PKTS);
	out.append(sizeof(frames));
	out.append((char *)&frames, sizeof(frames));
	out.append(IAX_IE_RR_DELAY);
	out.append(sizeof(delay));
	out.append((char *)&delay, sizeof(delay));
	out.append(IAX_IE_RR_DROPPED);
	out.append(sizeof(dropped));
	out.append((char *)&dropped, sizeof(dropped));
	out.append(IAX_IE_RR_OOO);
	out.append(sizeof(ooo));
	out.append((char *)&ooo, sizeof(ooo));
	m_udp->writeDatagram(out, m_address, m_port);
#ifdef DEBUG
	fprintf(stderr, "SEND: ");
	for(int i = 0; i < out.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
}

void IAX::send_ack(uint16_t scall, uint16_t dcall, uint8_t oseq, uint8_t iseq, uint32_t ts)
{
	if (!m_udp) return;
	QByteArray out;
	scall = htons(scall | 0x8000);
	dcall = htons(dcall);
	if (!ts) {
		ts = htonl((QDateTime::currentMSecsSinceEpoch() - m_timestamp));
	} else {
		ts = htonl(ts);
	}

	out.append((char *)&scall, 2);
	out.append((char *)&dcall, 2);
	out.append((char *)&ts, 4);
	out.append(oseq);
	out.append(iseq);
	out.append(AST_FRAME_IAX);
	out.append(IAX_COMMAND_ACK);
	m_udp->writeDatagram(out, m_address, m_port);
#ifdef DEBUG
	fprintf(stderr, "SEND: ");
	for(int i = 0; i < out.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
}

void IAX::send_lag_response(uint32_t ts)
{
	if (!m_udp) return;
	QByteArray out;
	uint16_t scall = htons(m_scallno | 0x8000);
	uint16_t dcall = htons(m_dcallno);
	ts = htonl(ts);

	out.append((char *)&scall, 2);
	out.append((char *)&dcall, 2);
	out.append((char *)&ts, 4);
	out.append(m_oseq);
	out.append(m_iseq);
	out.append(AST_FRAME_IAX);
	out.append(IAX_COMMAND_LAGRP);
	m_udp->writeDatagram(out, m_address, m_port);
#ifdef DEBUG
	fprintf(stderr, "SEND: ");
	for(int i = 0; i < out.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
}

void IAX::send_voice_frame(int16_t *f)
{
	QByteArray out;
	uint16_t scall = htons(m_scallno | 0x8000);
	uint16_t dcall = htons(m_dcallno);
	uint32_t ts = htonl((QDateTime::currentMSecsSinceEpoch() - m_timestamp));// + 3);

	out.append((char *)&scall, 2);
	out.append((char *)&dcall, 2);
	out.append((char *)&ts, 4);
	out.append(m_oseq);
	out.append(m_iseq);
	out.append(AST_FRAME_VOICE);
	out.append(AST_FORMAT_ULAW);

	for(int i = 0; i < 160; ++i){
		out.append(ulaw_encode(f[i]));
	}

	m_udp->writeDatagram(out, m_address, m_port);
#ifdef DEBUG
	fprintf(stderr, "SEND: ");
	for(int i = 0; i < out.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
}

void IAX::send_registration(uint16_t dcall)
{
	if (!m_udp) return;
	uint32_t ts;
	uint8_t seq;
	const QByteArray userUtf8 = m_username.toUtf8();

	if(dcall){
		dcall = htons(dcall);
		seq = 1;
		ts = htonl((QDateTime::currentMSecsSinceEpoch() - m_timestamp));
	}
	else{
		--m_regscallno;
		seq = 0;
		ts  = 0;
		m_md5seed.clear();
	}

	uint16_t scall = htons(m_regscallno | 0x8000);
	uint16_t refresh = htons(60);
	QByteArray out;
	out.append((char *)&scall, 2);
	out.append((char *)&dcall, 2);
	out.append((char *)&ts, 4);
	out.append(seq);
	out.append(seq);
	out.append(AST_FRAME_IAX);
	out.append(IAX_COMMAND_REGREQ);
	out.append(IAX_IE_USERNAME);
	out.append(static_cast<char>(userUtf8.size()));
	out.append(userUtf8);

	if(dcall){
		if ((m_authMethods & IAX_AUTH_MD5) || !m_md5seed.isEmpty() || m_authMethods == 0) {
			QByteArray seed = m_md5seed;
			seed.append(m_password.toUtf8());
			QByteArray result = QCryptographicHash::hash(seed, QCryptographicHash::Md5).toHex();
			out.append(IAX_IE_MD5_RESULT);
			out.append(static_cast<char>(result.size()));
			out.append(result);
		} else {
			QByteArray pw = m_password.toUtf8();
			out.append(IAX_IE_PASSWORD);
			out.append(static_cast<char>(pw.size()));
			out.append(pw);
		}
	}

	out.append(IAX_IE_REFRESH);
	out.append(0x02);
	out.append((char *)&refresh, 2);			// refresh time = 60 secs
	if (!dcall) {
		out.append(IAX_IE_CALLTOKEN);
		out.append(static_cast<char>(m_calltoken.size()));
		out.append(m_calltoken);
	}
	m_udp->writeDatagram(out, m_address, m_port);
#ifdef DEBUG
	fprintf(stderr, "SEND: ");
	for(int i = 0; i < out.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
}

void IAX::send_disconnect()
{
	if (!m_udp) return;
	QByteArray out;
	uint16_t scall = htons(m_scallno | 0x8000);
	uint16_t dcall = htons(m_dcallno);
	uint32_t ts = htonl((QDateTime::currentMSecsSinceEpoch() - m_timestamp));
	QString bye("BuhBye Dudesters");
	const QByteArray byeUtf8 = bye.toUtf8();
	out.append((char *)&scall, 2);
	out.append((char *)&dcall, 2);
	out.append((char *)&ts, 4);
	out.append(m_oseq);
	out.append(m_iseq);
	out.append(AST_FRAME_IAX);
	out.append(IAX_COMMAND_HANGUP);
	out.append(IAX_IE_CAUSE);
	out.append(static_cast<char>(byeUtf8.size()));
	out.append(byeUtf8);
	m_udp->writeDatagram(out, m_address, m_port);
#ifdef DEBUG
	fprintf(stderr, "SEND: ");
	for(int i = 0; i < out.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
}

void IAX::on_connect_retry()
{
	if (!m_udp || m_modeinfo.status != CONNECTING) {
		if (m_regtimer && m_regtimer->interval() < 60000) {
			m_regtimer->setInterval(60000);
		}
		return;
	}
	if (++m_connectRetries > 5) {
		emit update_log(QString("IAX: Connection timed out waiting for %1:%2").arg(m_host).arg(m_port));
		if (m_regtimer) {
			m_regtimer->stop();
		}
		m_modeinfo.status = DISCONNECTED;
		emit update(m_modeinfo);
		return;
	}
	// Retry calltoken request if the initial UDP packet was dropped across WAN
	if (m_dcallno == 0) {
		send_calltoken_request();
	}
}

void IAX::connected()
{
	if (m_modeinfo.status != CONNECTING) {
		return;
	}
	m_regreq = true;
	m_modeinfo.status = CONNECTED_RW;
	if (m_regtimer) {
		disconnect(m_regtimer, SIGNAL(timeout()), this, SLOT(on_connect_retry()));
		if (!m_wt && m_regAccepted) {
			connect(m_regtimer, SIGNAL(timeout()), this, SLOT(send_calltoken_request()), Qt::UniqueConnection);
			m_regtimer->start(60000);
		} else {
			m_regtimer->stop();
		}
	}
	if (!m_txtimer) {
		m_txtimer = new QTimer(this);
		m_txtimer->setTimerType(Qt::PreciseTimer);
		connect(m_txtimer, SIGNAL(timeout()), this, SLOT(transmit()));
	}
	if (!m_rxtimer) {
		m_rxtimer = new QTimer(this);
		m_rxtimer->setTimerType(Qt::PreciseTimer);
		connect(m_rxtimer, SIGNAL(timeout()), this, SLOT(process_rx_data()));
	}
	if (!m_rxtimer->isActive()) {
		m_rxtimer->start(19);
	}
	if (!m_ping_timer) {
		m_ping_timer = new QTimer(this);
		connect(m_ping_timer, SIGNAL(timeout()), this, SLOT(send_ping()));
	}
	m_ping_timer->start(10000);
	if (!m_audio) {
		m_audio = new AudioEngine(m_audioin, m_audioout);
		m_audio->init();
		m_audio->set_agc(m_pendingAgc);
		m_audio->set_input_volume(m_pendingInVol);
		m_audio->set_output_volume(m_pendingOutVol);
		m_audio->start_playback();
		m_audio->set_input_buffer_size(640);
		m_audio->start_capture();
	}
	m_modeinfo.sw_vocoder_loaded = true;
}

void IAX::hostname_lookup(QHostInfo i)
{
	m_callsign = m_modeinfo.callsign.trimmed();
	if (m_host.isEmpty()) {
		m_host = m_modeinfo.host.trimmed();
	}
	if (m_port <= 0) {
		m_port = (m_modeinfo.port > 0) ? m_modeinfo.port : 4569;
	}
	m_modeinfo.host = m_host;
	m_modeinfo.port = m_port;

	QHostAddress resolved;
	if (!i.addresses().isEmpty()) {
		for (const QHostAddress &addr : i.addresses()) {
			if (!m_ipv6 && addr.protocol() == QAbstractSocket::IPv4Protocol) {
				resolved = addr;
				break;
			}
			if (m_ipv6 && addr.protocol() == QAbstractSocket::IPv6Protocol) {
				resolved = addr;
				break;
			}
		}
		if (resolved.isNull()) {
			resolved = i.addresses().first();
		}
	} else {
		QHostAddress literalAddr;
		if (literalAddr.setAddress(m_host)) {
			resolved = literalAddr;
		}
	}

	if (!resolved.isNull()) {
		m_address = resolved;
		m_connectRetries = 0;
		m_callSent = false;
		m_regAccepted = !m_wt;
		m_regreq = !m_wt;
		m_udp = new QUdpSocket(this);
		m_regtimer = new QTimer(this);
		connect(m_udp, SIGNAL(readyRead()), this, SLOT(process_udp()));
		connect(m_regtimer, SIGNAL(timeout()), this, SLOT(on_connect_retry()));
		send_calltoken_request();
		m_timestamp = QDateTime::currentMSecsSinceEpoch();
		m_regtimer->start(2000);
	} else {
		emit update_log("IAX: DNS lookup failed for host " + m_host);
		m_modeinfo.status = DISCONNECTED;
		emit update(m_modeinfo);
	}
}

void IAX::send_connect()
{
	m_modeinfo.status = CONNECTING;
	qDebug() << "lookup IP = " << m_host << ":" << m_port;
	QHostInfo::lookupHost(m_host, this, SLOT(hostname_lookup(QHostInfo)));
}

void IAX::process_udp()
{
	if (!m_udp) return;
	while (m_udp->hasPendingDatagrams()) {
		QByteArray buf;
		QHostAddress sender;
		quint16 senderPort = 0;

		const qint64 pendingSize = m_udp->pendingDatagramSize();
		if (pendingSize <= 0) {
			break;
		}
		buf.resize(static_cast<int>(pendingSize));
		const qint64 readLen = m_udp->readDatagram(buf.data(), buf.size(), &sender, &senderPort);
		if (readLen <= 0) {
			break;
		}
		buf.resize(static_cast<int>(readLen));

		if(m_debug){
			QDebug debug = qDebug();
			debug.noquote();
			QString s = "RECV:";
			for(int i = 0; i < buf.size(); ++i){
				s += " " + QString("%1").arg((uint8_t)buf.data()[i], 2, 16, QChar('0'));
			}
			debug << s;
		}

		// Helper to parse IAX2 Information Elements (IEs) in any order
		auto parseIEs = [&](uint16_t &authMethods, QByteArray &challenge, QByteArray &callToken, QString &causeStr) {
			authMethods = 0;
			challenge.clear();
			callToken.clear();
			causeStr.clear();
			int pos = 12;
			const uint8_t *raw = reinterpret_cast<const uint8_t *>(buf.constData());
			while (pos + 2 <= buf.size()) {
				const uint8_t ieId = raw[pos];
				const uint8_t ieLen = raw[pos + 1];
				pos += 2;
				if (pos + ieLen > buf.size()) {
					break;
				}
				if (ieId == IAX_IE_AUTHMETHODS && ieLen >= 2) {
					authMethods = static_cast<uint16_t>((raw[pos] << 8) | raw[pos + 1]);
				} else if (ieId == IAX_IE_CHALLENGE) {
					challenge = buf.mid(pos, ieLen);
				} else if (ieId == IAX_IE_CALLTOKEN) {
					callToken = buf.mid(pos, ieLen);
				} else if (ieId == IAX_IE_CAUSE) {
					causeStr = QString::fromUtf8(buf.constData() + pos, ieLen).trimmed();
				}
				pos += ieLen;
			}
		};

		if (buf.size() >= 12 && (buf.data()[0] & 0x80)) {
			const uint8_t frametype = static_cast<uint8_t>(buf.data()[10]);
			const uint8_t subclass  = static_cast<uint8_t>(buf.data()[11]);
			const uint16_t peerCallNo = static_cast<uint16_t>(((static_cast<uint8_t>(buf.data()[0]) & 0x7f) << 8) |
			                                                  static_cast<uint8_t>(buf.data()[1]));
			const uint16_t dstCallNo  = static_cast<uint16_t>(((static_cast<uint8_t>(buf.data()[2]) & 0x7f) << 8) |
			                                                  static_cast<uint8_t>(buf.data()[3]));
			const uint32_t rxTs = (static_cast<uint32_t>(static_cast<uint8_t>(buf.data()[4])) << 24) |
			                      (static_cast<uint32_t>(static_cast<uint8_t>(buf.data()[5])) << 16) |
			                      (static_cast<uint32_t>(static_cast<uint8_t>(buf.data()[6])) << 8) |
			                      static_cast<uint32_t>(static_cast<uint8_t>(buf.data()[7]));

			if (frametype == AST_FRAME_IAX && subclass == IAX_COMMAND_REGAUTH) {
				uint16_t authMethods = 0;
				QByteArray challenge, callToken;
				QString cause;
				parseIEs(authMethods, challenge, callToken, cause);
				m_authMethods = authMethods;
				m_md5seed = challenge;
				send_registration(peerCallNo);
			}
			else if (frametype == AST_FRAME_IAX && subclass == IAX_COMMAND_REGACK) {
				send_ack(dstCallNo, peerCallNo, 2, 2);
				if (m_modeinfo.status == CONNECTING) {
					m_regreq = false;
					send_calltoken_request();
				}
			}
			else if (frametype == AST_FRAME_IAX && subclass == IAX_COMMAND_REGREJ) {
				const uint8_t rOseq = static_cast<uint8_t>(buf.data()[8]);
				const uint8_t rIseq = static_cast<uint8_t>(buf.data()[9]);
				send_ack(dstCallNo, peerCallNo, rIseq, rOseq + 1);
				if (m_modeinfo.status == CONNECTING && !m_callSent) {
					// AllStar / Asterisk stanzas with `type = user` (no `host = dynamic`) reject
					// REGREQ while allowing NEW calls. Proceed with calltoken + send_call().
					m_regAccepted = false;
					m_regreq = false;
					send_calltoken_request();
				} else {
					m_modeinfo.status = DISCONNECTED;
				}
			}
			else if (frametype == AST_FRAME_IAX && subclass == IAX_COMMAND_CALLTOKEN) {
				uint16_t authMethods = 0;
				QByteArray challenge, callToken;
				QString cause;
				parseIEs(authMethods, challenge, callToken, cause);
				m_calltoken = callToken;
				send_ack(dstCallNo, peerCallNo, 1, 1);
				if (m_regreq) {
					send_registration(0);
				} else {
					send_call();
				}
			}
			else if (frametype == AST_FRAME_IAX && subclass == IAX_COMMAND_AUTHREQ) {
				uint16_t authMethods = 0;
				QByteArray challenge, callToken;
				QString cause;
				parseIEs(authMethods, challenge, callToken, cause);
				++m_rxframes;
				m_dcallno = peerCallNo;
				m_authMethods = authMethods;
				m_md5seed = challenge;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_call_auth();
			}
			else if (frametype == AST_FRAME_IAX && subclass == IAX_COMMAND_ACK) {
				if (dstCallNo == m_scallno) {
					m_dcallno = peerCallNo;
					m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
					m_oseq = static_cast<uint8_t>(buf.data()[9]);
				}
			}
			else if (frametype == AST_FRAME_IAX && subclass == IAX_COMMAND_ACCEPT) {
				++m_rxframes;
				m_dcallno = peerCallNo;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_ack(m_scallno, m_dcallno, m_oseq, m_iseq);
			}
			else if (frametype == AST_FRAME_IAX && (subclass == IAX_COMMAND_REJECT || subclass == IAX_COMMAND_HANGUP)) {
				uint16_t authMethods = 0;
				QByteArray challenge, callToken;
				QString cause;
				parseIEs(authMethods, challenge, callToken, cause);
				const uint8_t rOseq = static_cast<uint8_t>(buf.data()[8]);
				const uint8_t rIseq = static_cast<uint8_t>(buf.data()[9]);
				send_ack(dstCallNo, peerCallNo, rIseq, rOseq + 1);
				if (!cause.isEmpty()) {
					emit update_log("IAX: " + cause);
				}
				m_modeinfo.status = DISCONNECTED;
			}
			else if (frametype == AST_FRAME_CONTROL && subclass == AST_CONTROL_RINGING) {
				if (m_wt && m_modeinfo.status == CONNECTING) {
					connected();
				}
				++m_rxframes;
				m_dcallno = peerCallNo;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_ack(m_scallno, m_dcallno, m_oseq, m_iseq);
			}
			else if (frametype == AST_FRAME_CONTROL && subclass == AST_CONTROL_ANSWER) {
				if (m_modeinfo.status == CONNECTING) {
					connected();
				}
				++m_rxframes;
				m_dcallno = peerCallNo;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_ack(m_scallno, m_dcallno, m_oseq, m_iseq);
			}
			else if (frametype == AST_FRAME_IAX && subclass == IAX_COMMAND_PING) {
				m_watchdog = 0;
				++m_rxframes;
				++m_cnt;
				m_dcallno = peerCallNo;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_pong(rxTs);
				++(m_modeinfo.count);
			}
			else if (frametype == AST_FRAME_IAX && subclass == IAX_COMMAND_PONG) {
				m_watchdog = 0;
				++m_rxframes;
				m_dcallno = peerCallNo;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_ack(m_scallno, m_dcallno, m_oseq, m_iseq, rxTs);
				++(m_modeinfo.count);
			}
			else if (frametype == AST_FRAME_IAX && subclass == IAX_COMMAND_VNAK) {
				++m_rxframes;
				m_dcallno = peerCallNo;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_ack(m_scallno, m_dcallno, m_oseq, m_iseq);
			}
			else if (frametype == AST_FRAME_VOICE && (subclass == AST_FORMAT_ULAW || subclass == 0)) {
				if (m_modeinfo.status == CONNECTING) {
					connected();
				}
				int16_t zeropcm[160];
				memset(zeropcm, 0, 160 * sizeof(int16_t));
				++m_rxframes;
				m_dcallno = peerCallNo;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_ack(m_scallno, m_dcallno, m_oseq, m_iseq);
				for (int i = 0; i < buf.size() - 12; ++i) {
					m_audioq.append(ulaw_decode(buf.data()[12 + i]));
				}
				send_voice_frame(zeropcm);
				if (m_txtimer && !m_txtimer->isActive()) {
					m_txtimer->start(19);
				}
			}
			else if (frametype == AST_FRAME_TEXT) {
				++m_rxframes;
				m_dcallno = peerCallNo;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_ack(m_scallno, m_dcallno, m_oseq, m_iseq);
			}
			else if (frametype == AST_FRAME_CONTROL && subclass == AST_CONTROL_OPTION) {
				++m_rxframes;
				m_dcallno = peerCallNo;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_ack(m_scallno, m_dcallno, m_oseq, m_iseq);
			}
			else if (frametype == AST_FRAME_IAX && subclass == IAX_COMMAND_LAGRQ) {
				++m_rxframes;
				m_dcallno = peerCallNo;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_lag_response(rxTs);
			}
			else if (frametype == AST_FRAME_CONTROL) {
				// Acknowledge any other control frames (e.g. PROGRESS, PROCEEDING, UNKEY, KEY)
				++m_rxframes;
				m_dcallno = peerCallNo;
				m_iseq = static_cast<uint8_t>(buf.data()[8]) + 1;
				m_oseq = static_cast<uint8_t>(buf.data()[9]);
				send_ack(m_scallno, m_dcallno, m_oseq, m_iseq);
			}
		}
		else if (buf.size() > 4 && !(buf.data()[0] & 0x80)) {
			const uint16_t dcallno = static_cast<uint16_t>((static_cast<uint8_t>(buf.data()[0]) << 8) |
			                                               static_cast<uint8_t>(buf.data()[1]));
			if (dcallno == m_dcallno) {
				for (int i = 0; i < buf.size() - 4; ++i) {
					m_audioq.append(ulaw_decode(buf.data()[4 + i]));
				}
			}
		}
		emit update(m_modeinfo);
	}
}

void IAX::process_rx_data()
{
	int16_t pcm[160];

	if(m_audioq.size() > 160){
		for(int i = 0; i < 160; ++i){
			pcm[i] = (qreal)m_audioq.dequeue(); // * m_rxgain;
		}
		m_audio->write(pcm, 160);
		emit update_output_level(m_audio->level());
	}
	else return;
}

void IAX::toggle_tx(bool tx)
{
	qDebug() << "IAXCodec::toggle_tx(bool tx) == " << tx;
	tx ? start_tx() : stop_tx();
}

void IAX::start_tx()
{
	int16_t pcm[160];
	send_radio_key(true);
	m_ttscnt = 0;
	qDebug() << "start_tx() " << m_ttsid << " " << m_ttstext;
	if (m_audio) {
		m_audio->flush_capture_for_tx();
	}
	m_tx = true;
#ifdef USE_FLITE
	if(m_ttsid == 1){
		tts_audio = flite_text_to_wave(m_ttstext.toStdString().c_str(), voice_kal);
	}
	else if(m_ttsid == 2){
		tts_audio = flite_text_to_wave(m_ttstext.toStdString().c_str(), voice_awb);
	}
	else if(m_ttsid == 3){
		tts_audio = flite_text_to_wave(m_ttstext.toStdString().c_str(), voice_slt);
	}
#endif
}

void IAX::stop_tx()
{
	m_tx = false;
	send_radio_key(false);
}

void IAX::transmit()
{
	QByteArray out;
	int16_t pcm[160];
	 uint16_t s = 0;
#ifdef USE_FLITE
	if(m_ttsid > 0){
		m_audio->read(pcm);
		s = 160;
		if(m_tx){
			for(int i = 0; i < 160; ++i){
				if(m_ttscnt >= tts_audio->num_samples/2){
					//audiotx_cnt = 0;
					pcm[i] = 0;
				}
				else{
					pcm[i] = tts_audio->samples[m_ttscnt*2] / 8;
					m_ttscnt++;
				}
			}
		}
	}
#endif
	if(m_ttsid == 0){
		s = m_audio->read(pcm);
	}
	if (s == 0) return;

	uint16_t scall = htons(m_scallno);
	uint16_t ts = htons( (QDateTime::currentMSecsSinceEpoch() - m_timestamp));// + 3 );
	out.append((char *)&scall, 2);
	out.append((char *)&ts, 2);
	for(int i = 0; i < s; ++i){
		out.append(ulaw_encode(pcm[i]));
	}
	if (!m_wt || m_tx) {
		m_udp->writeDatagram(out, m_address, m_port);
	}
#ifdef DEBUGG
	fprintf(stderr, "SEND: ");
	for(int i = 0; i < out.size(); ++i){
		fprintf(stderr, "%02x ", (unsigned char)out.data()[i]);
	}
	fprintf(stderr, "\n");
	fflush(stderr);
#endif
}

void IAX::deleteLater()
{
	if (m_regtimer) {
		m_regtimer->stop();
		delete m_regtimer;
		m_regtimer = nullptr;
	}
	if (m_pingtimer) {
		m_pingtimer->stop();
		delete m_pingtimer;
		m_pingtimer = nullptr;
	}
	Mode::deleteLater();
}
