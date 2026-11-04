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

#include <iostream>
#include <cstring>
#include <algorithm>
#include <utility>
#include <QSet>
#include "dmr.h"
#include "cgolay2087.h"
#include "crs129.h"
#include "SHA256.h"
#include "CRCenc.h"
#include "MMDVMDefines.h"
#ifdef USE_MD380_VOCODER
#include <md380_vocoder.h>
#endif

const uint32_t ENCODING_TABLE_1676[] =
    {0x0000U, 0x0273U, 0x04E5U, 0x0696U, 0x09C9U, 0x0BBAU, 0x0D2CU, 0x0F5FU, 0x11E2U, 0x1391U, 0x1507U, 0x1774U,
     0x182BU, 0x1A58U, 0x1CCEU, 0x1EBDU, 0x21B7U, 0x23C4U, 0x2552U, 0x2721U, 0x287EU, 0x2A0DU, 0x2C9BU, 0x2EE8U,
     0x3055U, 0x3226U, 0x34B0U, 0x36C3U, 0x399CU, 0x3BEFU, 0x3D79U, 0x3F0AU, 0x411EU, 0x436DU, 0x45FBU, 0x4788U,
     0x48D7U, 0x4AA4U, 0x4C32U, 0x4E41U, 0x50FCU, 0x528FU, 0x5419U, 0x566AU, 0x5935U, 0x5B46U, 0x5DD0U, 0x5FA3U,
     0x60A9U, 0x62DAU, 0x644CU, 0x663FU, 0x6960U, 0x6B13U, 0x6D85U, 0x6FF6U, 0x714BU, 0x7338U, 0x75AEU, 0x77DDU,
     0x7882U, 0x7AF1U, 0x7C67U, 0x7E14U, 0x804FU, 0x823CU, 0x84AAU, 0x86D9U, 0x8986U, 0x8BF5U, 0x8D63U, 0x8F10U,
     0x91ADU, 0x93DEU, 0x9548U, 0x973BU, 0x9864U, 0x9A17U, 0x9C81U, 0x9EF2U, 0xA1F8U, 0xA38BU, 0xA51DU, 0xA76EU,
     0xA831U, 0xAA42U, 0xACD4U, 0xAEA7U, 0xB01AU, 0xB269U, 0xB4FFU, 0xB68CU, 0xB9D3U, 0xBBA0U, 0xBD36U, 0xBF45U,
     0xC151U, 0xC322U, 0xC5B4U, 0xC7C7U, 0xC898U, 0xCAEBU, 0xCC7DU, 0xCE0EU, 0xD0B3U, 0xD2C0U, 0xD456U, 0xD625U,
     0xD97AU, 0xDB09U, 0xDD9FU, 0xDFECU, 0xE0E6U, 0xE295U, 0xE403U, 0xE670U, 0xE92FU, 0xEB5CU, 0xEDCAU, 0xEFB9U,
     0xF104U, 0xF377U, 0xF5E1U, 0xF792U, 0xF8CDU, 0xFABEU, 0xFC28U, 0xFE5BU};

static inline void writeLE16(uint8_t *dst, uint16_t val)
{
    dst[0] = static_cast<uint8_t>(val & 0xFFU);
    dst[1] = static_cast<uint8_t>((val >> 8) & 0xFFU);
}

static inline void writeLE32(uint8_t *dst, uint32_t val)
{
    dst[0] = static_cast<uint8_t>(val & 0xFFU);
    dst[1] = static_cast<uint8_t>((val >> 8) & 0xFFU);
    dst[2] = static_cast<uint8_t>((val >> 16) & 0xFFU);
    dst[3] = static_cast<uint8_t>((val >> 24) & 0xFFU);
}

static inline uint16_t readLE16(const uint8_t *src)
{
    return static_cast<uint16_t>(src[0]) |
           (static_cast<uint16_t>(src[1]) << 8);
}

static inline uint32_t readLE32(const uint8_t *src)
{
    return static_cast<uint32_t>(src[0]) |
           (static_cast<uint32_t>(src[1]) << 8) |
           (static_cast<uint32_t>(src[2]) << 16) |
           (static_cast<uint32_t>(src[3]) << 24);
}

DMR::DMR() :
    m_essid(0),
    m_txsrcid(0),
    m_txdstid(0),
    m_linkedGroupTg(0),
    m_unlinkedStaleTg(0),
    m_linkGeneration(0),
    m_linkTimer(nullptr),
    m_autoLinkState(AUTOLINK_IDLE),
    m_linkTargetTg(0),
    m_linkActiveDst(0),
    m_linkStreamId(0),
    m_linkDmrCnt(0),
    m_linkFramesRemaining(0),
    m_wait4000StartMs(0),
    m_last4000PktMs(0),
    m_saw4000Rx(false),
    m_4000EotReceived(false),
    m_silenceAmbeReady(false),
    m_txstreamid(0),
    m_txslot(2),
    m_txcc(1),
    packet_size(0),
    m_defsrcid(0),
    m_dataType(0),
    m_dmrcnt(0),
    m_flco(FLCO_GROUP),
    m_odtp(false),
    m_isTgif(false),
    m_tgifTriedDefaultPass(false),
    m_tgifNam(nullptr),
    m_hbFallbackPort(REWIND_DEFAULT_HBP_PORT),
    m_odtpAuthAttempts(0),
    m_odtpConnectRetries(0),
    m_odtpConfigured(false),
    m_odtpSubscribedTg(0),
    m_odtpSubscribedPrivateSelf(false),
    m_odtpRxSrcId(0),
    m_odtpRxDstId(0),
    m_odtpRxIsPrivate(false),
    m_odtpRxFrameCnt(0)
{
    m_mode = "DMR";
    m_modeinfo.cc = 1;
    m_attenuation = 5;
    m_odtpSeq[0] = 0U;
    m_odtpSeq[1] = 0U;
    ::memset(m_ambe, 0, sizeof(m_ambe));
    ::memset(m_silenceAmbe27, 0, sizeof(m_silenceAmbe27));
    ::memset(m_dmrFrame, 0, sizeof(m_dmrFrame));
    ::memset(m_raw, 0, sizeof(m_raw));
    ::memset(m_data, 0, sizeof(m_data));
#ifdef USE_MD380_VOCODER
    md380_init();
#endif
}

void DMR::set_odtp_mode(bool enabled, int fallbackHbPort)
{
    m_odtp = enabled;
    m_hbFallbackPort = (fallbackHbPort > 0) ? fallbackHbPort : static_cast<int>(REWIND_DEFAULT_HBP_PORT);
}

DMR::~DMR()
{
    if (m_smsTxTimer) {
        m_smsTxTimer->stop();
        delete m_smsTxTimer;
        m_smsTxTimer = nullptr;
    }
    if (m_linkTimer) {
        m_linkTimer->stop();
        delete m_linkTimer;
        m_linkTimer = nullptr;
    }
}

void DMR::stop_rx_stream()
{
    const bool wasActive = (m_modeinfo.stream_state == STREAM_NEW ||
                            m_modeinfo.stream_state == STREAMING ||
                            (m_rxtimer && m_rxtimer->isActive()));
    if (m_rxtimer) {
        m_rxtimer->stop();
    }
    if (m_audio) {
        m_audio->stop_playback();
    }
    m_rxcodecq.clear();
    m_rxmodemq.clear();
    m_rxwatchdog = 0;
    m_modeinfo.streamid = 0;
    m_modeinfo.srcid = 0;
    m_modeinfo.dstid = 0;
    m_modeinfo.src.clear();
    m_modeinfo.frame_number = 0;
    m_modeinfo.stream_state = STREAM_IDLE;
    if (wasActive) {
        emit update_output_level(0);
        emit update(m_modeinfo);
    }
}

void DMR::prepare_silence_ambe()
{
    if (m_silenceAmbeReady) {
        return;
    }
    ::memcpy(m_silenceAmbe27, DMR_SILENCE_DATA, 13U);
    m_silenceAmbe27[13U] = (DMR_SILENCE_DATA[13U] & 0xF0U) | (DMR_SILENCE_DATA[19U] & 0x0FU);
    ::memcpy(m_silenceAmbe27 + 14U, DMR_SILENCE_DATA + 20U, 13U);

    if (m_modeinfo.sw_vocoder_loaded) {
        int16_t zeroPcm[160];
        ::memset(zeroPcm, 0, sizeof(zeroPcm));
        uint8_t ambe[72];
        for (int f = 0; f < 3; ++f) {
            ::memset(ambe, 0, sizeof(ambe));
#ifdef USE_MD380_VOCODER
            md380_encode_fec(ambe, zeroPcm);
#else
            if (m_mbevocoder) {
                m_mbevocoder->encode_2450x1150(zeroPcm, ambe);
            }
#endif
            ::memcpy(m_silenceAmbe27 + (f * 9), ambe, 9U);
        }
        m_silenceAmbeReady = true;
    }
}

void DMR::abort_auto_link_stream()
{
    if (!m_udp || m_modeinfo.status != CONNECTED_RW || m_linkDmrCnt == 0U || m_linkActiveDst == 0U) {
        m_linkDmrCnt = 0U;
        m_linkFramesRemaining = 0;
        return;
    }

    const uint32_t savedSrcId = m_txsrcid;
    const uint32_t savedDstId = m_txdstid;
    const FLCO savedFlco = m_flco;
    const uint32_t savedStreamId = m_txstreamid;
    const uint32_t savedDmrCnt = m_dmrcnt;
    const uint8_t savedDataType = m_dataType;
    const Mode::MODEINFO savedInfo = m_modeinfo;

    m_txsrcid = m_dmrid;
    m_txdstid = m_linkActiveDst;
    m_flco = FLCO_GROUP;
    m_txstreamid = m_linkStreamId;
    m_dmrcnt = m_linkDmrCnt;

    encode_header(DT_TERMINATOR_WITH_LC);
    build_frame();
    m_udp->writeDatagram(reinterpret_cast<const char *>(m_dmrFrame), 55, m_address, m_modeinfo.port);
    m_udp->writeDatagram(reinterpret_cast<const char *>(m_dmrFrame), 55, m_address, m_modeinfo.port);

    m_txsrcid = savedSrcId;
    m_txdstid = savedDstId;
    m_flco = savedFlco;
    m_txstreamid = savedStreamId;
    m_dmrcnt = savedDmrCnt;
    m_dataType = savedDataType;
    m_modeinfo = savedInfo;

    m_linkDmrCnt = 0U;
    m_linkFramesRemaining = 0;
}

bool DMR::send_auto_link_step(uint32_t dstid)
{
    if (!m_udp || m_modeinfo.status != CONNECTED_RW || m_tx || m_dmrid == 0U || dstid == 0U) {
        m_linkDmrCnt = 0U;
        m_linkFramesRemaining = 0;
        return true;
    }

    prepare_silence_ambe();

    const uint32_t savedSrcId = m_txsrcid;
    const uint32_t savedDstId = m_txdstid;
    const FLCO savedFlco = m_flco;
    const uint32_t savedStreamId = m_txstreamid;
    const uint32_t savedDmrCnt = m_dmrcnt;
    const uint8_t savedDataType = m_dataType;
    const Mode::MODEINFO savedInfo = m_modeinfo;

    m_txsrcid = m_dmrid;
    m_txdstid = dstid;
    m_flco = FLCO_GROUP;
    bool finished = false;

    if (m_linkDmrCnt == 0U) {
        m_linkActiveDst = dstid;
        m_linkStreamId = (static_cast<uint32_t>(::rand()) | 1U);
        m_txstreamid = m_linkStreamId;
        m_dmrcnt = 0U;

        // 1. Send Voice LC Header (3x, matching real PTT send_frame())
        encode_header(DT_VOICE_LC_HEADER);
        build_frame();
        m_udp->writeDatagram(reinterpret_cast<const char *>(m_dmrFrame), 55, m_address, m_modeinfo.port);
        m_udp->writeDatagram(reinterpret_cast<const char *>(m_dmrFrame), 55, m_address, m_modeinfo.port);
        m_udp->writeDatagram(reinterpret_cast<const char *>(m_dmrFrame), 55, m_address, m_modeinfo.port);
        ++m_dmrcnt;

        // 2. Send first voice sync frame (n_dmr = 0)
        ::memcpy(m_dmrFrame + 20U, m_silenceAmbe27, 13U);
        m_dmrFrame[33U] = m_silenceAmbe27[13U] & 0xF0U;
        m_dmrFrame[39U] = m_silenceAmbe27[13U] & 0x0FU;
        ::memcpy(m_dmrFrame + 40U, &m_silenceAmbe27[14U], 13U);
        encode_data();
        build_frame();
        m_udp->writeDatagram(reinterpret_cast<const char *>(m_dmrFrame), 55, m_address, m_modeinfo.port);
        ++m_dmrcnt;

        m_linkDmrCnt = m_dmrcnt;
        if (m_linkFramesRemaining > 0) {
            --m_linkFramesRemaining;
        }
    } else if (m_linkFramesRemaining > 0) {
        m_txstreamid = m_linkStreamId;
        m_dmrcnt = m_linkDmrCnt;

        ::memcpy(m_dmrFrame + 20U, m_silenceAmbe27, 13U);
        m_dmrFrame[33U] = m_silenceAmbe27[13U] & 0xF0U;
        m_dmrFrame[39U] = m_silenceAmbe27[13U] & 0x0FU;
        ::memcpy(m_dmrFrame + 40U, &m_silenceAmbe27[14U], 13U);
        encode_data();
        build_frame();
        m_udp->writeDatagram(reinterpret_cast<const char *>(m_dmrFrame), 55, m_address, m_modeinfo.port);
        ++m_dmrcnt;

        m_linkDmrCnt = m_dmrcnt;
        --m_linkFramesRemaining;
    } else {
        m_txstreamid = m_linkStreamId;
        m_dmrcnt = m_linkDmrCnt;

        encode_header(DT_TERMINATOR_WITH_LC);
        build_frame();
        m_udp->writeDatagram(reinterpret_cast<const char *>(m_dmrFrame), 55, m_address, m_modeinfo.port);
        m_udp->writeDatagram(reinterpret_cast<const char *>(m_dmrFrame), 55, m_address, m_modeinfo.port);

        m_linkDmrCnt = 0U;
        m_linkFramesRemaining = 0;
        finished = true;
    }

    m_txsrcid = savedSrcId;
    m_txdstid = savedDstId;
    m_flco = savedFlco;
    m_txstreamid = savedStreamId;
    m_dmrcnt = savedDmrCnt;
    m_dataType = savedDataType;
    m_modeinfo = savedInfo;
    return finished;
}

void DMR::onAutoLinkTick()
{
    if (!m_udp || m_modeinfo.status != CONNECTED_RW || m_tx || m_flco != FLCO_GROUP) {
        abort_auto_link_stream();
        if (m_linkTimer) m_linkTimer->stop();
        m_autoLinkState = AUTOLINK_IDLE;
        return;
    }

    if (m_autoLinkState == AUTOLINK_TX_UNLINK_4000) {
        if (send_auto_link_step(4000U)) {
            m_linkedGroupTg = 0U;
            emit update_log("DMR: Sent TG 4000 unlink");

            if (m_linkTargetTg == 0U || m_linkTargetTg == 4000U) {
                if (m_linkTimer) m_linkTimer->stop();
                m_autoLinkState = AUTOLINK_IDLE;
                return;
            }

            m_autoLinkState = AUTOLINK_WAIT_4000_CLEAR;
            m_wait4000StartMs = QDateTime::currentMSecsSinceEpoch();
            m_last4000PktMs = 0;
            m_saw4000Rx = false;
            m_4000EotReceived = false;
        }
        return;
    }

    if (m_autoLinkState == AUTOLINK_WAIT_4000_CLEAR) {
        if (m_linkTargetTg == 0U || m_linkTargetTg == 4000U) {
            if (m_linkTimer) m_linkTimer->stop();
            m_autoLinkState = AUTOLINK_IDLE;
            return;
        }
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const bool eotSettled = m_4000EotReceived && (nowMs - m_last4000PktMs) >= 110;
        const bool rxQuietAfter4000 = m_saw4000Rx && (nowMs - m_last4000PktMs) >= 240;
        const bool noPromptTimeout = !m_saw4000Rx && (nowMs - m_wait4000StartMs) >= 380;
        const bool maxWaitReached = (nowMs - m_wait4000StartMs) >= 1700;

        if (eotSettled || rxQuietAfter4000 || noPromptTimeout || maxWaitReached) {
            m_autoLinkState = AUTOLINK_TX_TARGET;
            m_linkActiveDst = m_linkTargetTg;
            m_linkDmrCnt = 0U;
            m_linkFramesRemaining = 8;
            send_auto_link_step(m_linkTargetTg);
        }
        return;
    }

    if (m_autoLinkState == AUTOLINK_TX_TARGET) {
        if (m_linkTargetTg == 0U || m_linkTargetTg == 4000U) {
            abort_auto_link_stream();
            if (m_linkTimer) m_linkTimer->stop();
            m_autoLinkState = AUTOLINK_IDLE;
            return;
        }
        if (send_auto_link_step(m_linkTargetTg)) {
            m_linkedGroupTg = m_linkTargetTg;
            if (m_linkTimer) m_linkTimer->stop();
            m_autoLinkState = AUTOLINK_IDLE;
            emit update_log(QString("DMR: Linked to TG %1").arg(m_linkTargetTg));
        }
        return;
    }

    if (m_linkTimer) m_linkTimer->stop();
    m_autoLinkState = AUTOLINK_IDLE;
}

bool DMR::send_quick_call(uint32_t dstid, FLCO flco)
{
    if (!m_udp || m_modeinfo.status != CONNECTED_RW || m_tx || m_dmrid == 0U || dstid == 0U) {
        return false;
    }
    if (flco == FLCO_GROUP) {
        m_txdstid = dstid;
        schedule_group_link(true);
        return true;
    }
    return false;
}

void DMR::schedule_group_link(bool force)
{
    if (!m_udp || m_modeinfo.status != CONNECTED_RW || m_tx || m_dmrid == 0U || m_flco != FLCO_GROUP) {
        return;
    }

    if (m_odtp) {
        update_odtp_subscription(force);
        return;
    }

    const uint32_t target = m_txdstid;
    if (target == 0U || target == 9990U || target == 31000U) {
        abort_auto_link_stream();
        if (m_linkTimer) m_linkTimer->stop();
        m_autoLinkState = AUTOLINK_IDLE;
        return;
    }

    // If an auto-link sequence is already running for this exact target, let it finish.
    if (m_autoLinkState != AUTOLINK_IDLE && m_linkTargetTg == target) {
        return;
    }

    const bool hearingTarget = (m_modeinfo.stream_state == STREAM_NEW || m_modeinfo.stream_state == STREAMING);
    if (m_autoLinkState == AUTOLINK_IDLE && m_linkedGroupTg == target && target != 4000U && (!force || hearingTarget)) {
        return;
    }

    ++m_linkGeneration;

    if (!m_linkTimer) {
        m_linkTimer = new QTimer(this);
        m_linkTimer->setTimerType(Qt::PreciseTimer);
        connect(m_linkTimer, SIGNAL(timeout()), this, SLOT(onAutoLinkTick()));
    }

    // If we are already unlinking with 4000 or waiting for 4000 EOT, just retarget!
    if ((m_autoLinkState == AUTOLINK_TX_UNLINK_4000 || m_autoLinkState == AUTOLINK_WAIT_4000_CLEAR) && target != 4000U) {
        m_linkTargetTg = target;
        if (!m_linkTimer->isActive()) {
            m_linkTimer->start(57);
        }
        return;
    }

    // If we were mid-stream linking a different target TG, close that stream cleanly first.
    if (m_autoLinkState == AUTOLINK_TX_TARGET && m_linkActiveDst != 0U && m_linkActiveDst != target) {
        const uint32_t prevActive = m_linkActiveDst;
        abort_auto_link_stream();
        if (prevActive != 4000U) {
            m_linkedGroupTg = prevActive;
        }
    }

    m_linkTargetTg = target;

    if (target == 4000U) {
        m_autoLinkState = AUTOLINK_TX_UNLINK_4000;
        m_linkActiveDst = 4000U;
        m_linkDmrCnt = 0U;
        m_linkFramesRemaining = 6;
        onAutoLinkTick();
        if (m_autoLinkState != AUTOLINK_IDLE && !m_linkTimer->isActive()) {
            m_linkTimer->start(57);
        }
        return;
    }

    // TGIF Network parks directly on the newly keyed Talkgroup and never
    const bool needUnlink = !m_isTgif && (m_linkedGroupTg != 0U && m_linkedGroupTg != 4000U && m_linkedGroupTg != target);
    if (needUnlink) {
        m_autoLinkState = AUTOLINK_TX_UNLINK_4000;
        m_linkActiveDst = 4000U;
        m_linkDmrCnt = 0U;
        m_linkFramesRemaining = 6;
    } else {
        m_autoLinkState = AUTOLINK_TX_TARGET;
        m_linkActiveDst = target;
        m_linkDmrCnt = 0U;
        m_linkFramesRemaining = 8;
    }

    onAutoLinkTick();
    if (m_autoLinkState != AUTOLINK_IDLE && !m_linkTimer->isActive()) {
        m_linkTimer->start(57);
    }
}

void DMR::request_tgif_talkgroup_switch(uint32_t targetTg, bool forceFallback)
{
    if (!m_udp || m_modeinfo.status != CONNECTED_RW || m_tx || m_dmrid == 0U) {
        return;
    }

    if (m_autoLinkState != AUTOLINK_IDLE) {
        abort_auto_link_stream();
        if (m_linkTimer) m_linkTimer->stop();
        m_autoLinkState = AUTOLINK_IDLE;
    }

    if (targetTg == 0U) {
        return;
    }

    const uint32_t gen = ++m_linkGeneration;
    const uint32_t repeaterId = (m_essid > 0U) ? m_essid : m_dmrid;
    const uint32_t slotIdx = (m_txslot == 1U) ? 0U : 1U;
    const QString apiHost = m_modeinfo.host.contains(QStringLiteral("tgif.network"), Qt::CaseInsensitive)
                            ? m_modeinfo.host
                            : QStringLiteral("tgif.network");

    // When tuning to TGIF Parrot (9990 or 31000), do not auto-key silence into Parrot;
    if (targetTg == 9990U || targetTg == 31000U) {
        if (m_linkedGroupTg != 0U && m_linkedGroupTg != 4000U) {
            if (!m_tgifNam) {
                m_tgifNam = new QNetworkAccessManager(this);
            }
            const QUrl url(QStringLiteral("http://%1:5040/api/sessions/update/%2/%3/4000")
                           .arg(apiHost).arg(repeaterId).arg(slotIdx));
            QNetworkRequest req(url);
            req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DroidStar/2026"));
            req.setTransferTimeout(1500);
            QNetworkReply *reply = m_tgifNam->get(req);
            connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
            m_linkedGroupTg = 0U;
        }
        return;
    }

    if (!m_tgifNam) {
        m_tgifNam = new QNetworkAccessManager(this);
    }

    const QUrl url(QStringLiteral("http://%1:5040/api/sessions/update/%2/%3/%4")
                   .arg(apiHost).arg(repeaterId).arg(slotIdx).arg(targetTg));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DroidStar/2026"));
    req.setTransferTimeout(1500);

    QNetworkReply *reply = m_tgifNam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, gen, targetTg, forceFallback]() {
        reply->deleteLater();
        if (gen != m_linkGeneration || !m_udp || m_modeinfo.status != CONNECTED_RW || m_tx) {
            return;
        }

        const QByteArray body = (reply->error() == QNetworkReply::NoError)
                                ? reply->readAll().trimmed()
                                : QByteArray();
        if (body == "200") {
            if (targetTg == 4000U) {
                m_linkedGroupTg = 0U;
                emit update_log("DMR (TGIF): Unlinked (TG 4000)");
            } else {
                m_linkedGroupTg = targetTg;
                emit update_log(QString("DMR (TGIF): Switched to TG %1").arg(targetTg));
            }
            return;
        }

        // Fallback to silent in-band DMR link burst if port 5040 is
        if (m_flco == FLCO_GROUP && m_txdstid == targetTg) {
            schedule_group_link(true || forceFallback);
        }
    });
}

void DMR::relink_talkgroup()
{
    if (!m_udp || m_modeinfo.status != CONNECTED_RW || m_tx || m_dmrid == 0U || m_txdstid == 0U) {
        return;
    }
    stop_rx_stream();
    if (m_odtp) {
        update_odtp_subscription(true);
        return;
    }
    if (m_isTgif) {
        request_tgif_talkgroup_switch(m_flco == FLCO_GROUP ? m_txdstid : 4000U, true);
        return;
    }
    if (m_flco == FLCO_GROUP) {
        schedule_group_link(true);
    }
}

void DMR::dmr_tgid_changed(int id)
{
    const uint32_t newId = (id > 0) ? static_cast<uint32_t>(id) : 0U;
    const uint32_t oldId = m_txdstid;
    m_txdstid = newId;

    if (newId != oldId) {
        m_unlinkedStaleTg = 0U;
        if (!m_tx) {
            stop_rx_stream();
            m_odtpRxSrcId = 0U;
            m_odtpRxDstId = 0U;
            m_odtpRxIsPrivate = false;
            m_odtpRxSrcCall.clear();
            m_odtpRxFrameCnt = 0U;
        }
        if (m_modeinfo.status == CONNECTED_RW && !m_tx) {
            if (m_odtp) {
                update_odtp_subscription(false);
            } else if (m_isTgif) {
                if (m_flco == FLCO_GROUP && m_txdstid > 0U) {
                    request_tgif_talkgroup_switch(m_txdstid, false);
                }
            } else if (m_flco == FLCO_GROUP && m_txdstid > 0U) {
                schedule_group_link(false);
            }
        }
    }
}

void DMR::dmrpc_state_changed(int p)
{
    const FLCO newFlco = p ? FLCO_USER_USER : FLCO_GROUP;
    if (m_flco == newFlco) {
        return;
    }
    m_flco = newFlco;
    if (!m_tx) {
        stop_rx_stream();
        m_odtpRxSrcId = 0U;
        m_odtpRxDstId = 0U;
        m_odtpRxIsPrivate = false;
        m_odtpRxSrcCall.clear();
        m_odtpRxFrameCnt = 0U;
    }
    if (m_modeinfo.status == CONNECTED_RW && !m_tx) {
        if (m_odtp) {
            update_odtp_subscription(true);
        } else if (m_isTgif) {
            request_tgif_talkgroup_switch(m_flco == FLCO_GROUP ? m_txdstid : 4000U, true);
        } else if (m_flco == FLCO_GROUP && m_txdstid > 0U) {
            schedule_group_link(false);
        }
    }
}

bool DMR::shouldAcceptRxPacket(uint32_t srcid, uint32_t dstid, bool isPrivate) const
{
    // If no destination filter is configured (0), accept all incoming traffic
    if (m_txdstid == 0U) {
        return true;
    }

    // If user explicitly dialed 4000 (Unlink), only play 4000 announcements
    if (m_txdstid == 4000U) {
        return (dstid == 4000U || srcid == 4000U);
    }

    // When tuned to a normal TG or Private ID (m_txdstid !=
    if (srcid == 4000U || dstid == 4000U) {
        return false;
    }

    if (isPrivate) {
        // Accept Private Calls addressed to our DMR ID / ESSID, or from/to our target
        return (dstid == m_dmrid || dstid == m_essid ||
                srcid == m_txdstid || dstid == m_txdstid ||
                srcid == 9990U || srcid == 31000U || srcid == 5000U);
    }

    // Incoming packet is a Group Call:
    if (m_flco == FLCO_USER_USER) {
        // In Private Call mode, only accept group packets if they match
        return (dstid == m_txdstid || srcid == m_txdstid ||
                dstid == m_dmrid || dstid == m_essid ||
                srcid == 9990U || srcid == 31000U);
    }

    // In Group Call mode, accept Group Calls matching the currently selected
    if (dstid == m_txdstid || dstid == m_dmrid || dstid == m_essid) {
        return true;
    }
    if ((m_txdstid == 9990U || m_txdstid == 31000U) &&
        (srcid == 9990U || srcid == 31000U || dstid == 9990U || dstid == 31000U)) {
        return true;
    }
    if (m_isTgif && m_txdstid == 777U) {
        return true;
    }
    if (dstid == 9U && (m_txdstid == 9U || (m_txdstid > 4000U && m_txdstid <= 5000U))) {
        return true;
    }

    return false;
}

bool DMR::shouldAcceptRxDataPacket(uint32_t srcid, uint32_t dstid, bool isPrivate) const
{
    if (srcid == 0U || dstid == 0U) {
        return false;
    }
    if (isPrivate) {
        // Data calls are asynchronous and must not be filtered by the selected voice TG.
        return dstid == m_dmrid || dstid == m_essid ||
               (m_flco == FLCO_USER_USER && (srcid == m_txdstid || dstid == m_txdstid));
    }
    return dstid == m_txdstid || dstid == m_linkedGroupTg ||
           dstid == m_dmrid || dstid == m_essid;
}

void DMR::send_sms(int msgId, uint32_t dstId, const QString &text, bool isGroup, int format)
{
    if (!m_udp || m_modeinfo.status != CONNECTED_RW || m_dmrid == 0U ||
        dstId == 0U || text.trimmed().isEmpty()) {
        emit sms_send_status(msgId, 3, QStringLiteral("DMR is not connected or message is empty"));
        return;
    }

    if (m_odtp) {
        const QByteArray payload = DmrDataCodec::encodeOdtpTextPayload(
            m_dmrid, dstId, isGroup, text.trimmed());
        if (payload.isEmpty()) {
            emit sms_send_status(msgId, 3, QStringLiteral("Unable to encode ODTP text"));
            return;
        }
        send_odtp_packet(REWIND_TYPE_MESSAGE_TEXT, REWIND_FLAG_NONE,
                         payload.constData(), static_cast<uint16_t>(payload.size()));
        emit sms_send_status(msgId, 1, QStringLiteral("ODTP"));
        return;
    }

    const bool includeCsbkPreamble = (format & 0x100) == 0;
    const int boundedFormat = std::clamp(format & 0xFF, 0, 3);
    const auto frames = DmrDataCodec::encodeSmsSequence(
        m_dmrid, dstId, m_essid, m_txslot, m_txcc, isGroup,
        text.trimmed(), static_cast<DmrDataCodec::SmsFormat>(boundedFormat),
        includeCsbkPreamble);
    if (frames.isEmpty()) {
        emit sms_send_status(msgId, 3, QStringLiteral("Unable to encode DMR data message"));
        return;
    }
    for (int i = 0; i < frames.size(); ++i) {
        m_smsTxQueue.enqueue(PendingSmsBurst{
            msgId, i == frames.size() - 1, frames.at(i)
        });
    }
    if (!m_smsTxTimer) {
        m_smsTxTimer = new QTimer(this);
        m_smsTxTimer->setTimerType(Qt::PreciseTimer);
        m_smsTxTimer->setInterval(static_cast<int>(DMR_SLOT_TIME));
        connect(m_smsTxTimer, &QTimer::timeout, this, &DMR::process_sms_tx_queue);
    }
    if (!m_smsTxTimer->isActive()) {
        m_smsTxTimer->start();
    }
}

void DMR::process_sms_tx_queue()
{
    if (!m_udp || m_modeinfo.status != CONNECTED_RW) {
        QSet<int> failed;
        while (!m_smsTxQueue.isEmpty()) {
            const PendingSmsBurst item = m_smsTxQueue.dequeue();
            if (item.msgId >= 0) failed.insert(item.msgId);
        }
        if (m_smsTxTimer) m_smsTxTimer->stop();
        for (int id : std::as_const(failed)) {
            emit sms_send_status(id, 3, QStringLiteral("DMR disconnected during send"));
        }
        return;
    }

    // Voice PTT, the scanner's auto-TX punch, and the HomeBrew auto-link
    // state machine always have priority over packet data.
    if (m_tx || m_autoTxPunch.load(std::memory_order_relaxed) ||
        m_autoLinkState != AUTOLINK_IDLE ||
        (m_txtimer && m_txtimer->isActive()) ||
        m_modeinfo.stream_state == STREAM_NEW ||
        m_modeinfo.stream_state == STREAMING) {
        return;
    }
    if (m_smsTxQueue.isEmpty()) {
        if (m_smsTxTimer) m_smsTxTimer->stop();
        return;
    }

    const PendingSmsBurst item = m_smsTxQueue.dequeue();
    if (item.dmrdFrame.size() == 55) {
        m_udp->writeDatagram(item.dmrdFrame, m_address, m_modeinfo.port);
    }
    if (item.isLastBurst && item.msgId >= 0) {
        emit sms_send_status(item.msgId, 1, QStringLiteral("HomeBrew DMRD"));
    }
    if (m_smsTxQueue.isEmpty() && m_smsTxTimer) {
        m_smsTxTimer->stop();
    }
}

void DMR::set_dmr_params(uint8_t essid, QString password, QString lat, QString lon, QString location, QString desc, QString freq, QString url, QString swid, QString pkid, QString options)
{
    if (essid){
        m_essid = m_dmrid * 100 + (essid-1);
    }
    else{
        m_essid = m_dmrid;
    }

    m_password = password;
    m_lat = lat;
    m_lon = lon;
    m_location = location;
    m_desc = desc;
    m_freq = freq;
    m_url = url;
    m_swid = swid;
    m_pkid = pkid;
    m_options = options;
}

void DMR::send_odtp_packet(uint16_t type, uint16_t flags, const void *payload, uint16_t length, bool advanceSeq)
{
    if (!m_udp) {
        return;
    }

    QByteArray pkt;
    pkt.resize(static_cast<int>(REWIND_HEADER_LENGTH) + static_cast<int>(length));
    uint8_t *raw = reinterpret_cast<uint8_t *>(pkt.data());

    ::memcpy(raw, REWIND_PROTOCOL_SIGN, REWIND_SIGN_LENGTH);
    writeLE16(raw + 8U, type);
    writeLE16(raw + 10U, flags);

    const int seqIdx = (flags & REWIND_FLAG_REAL_TIME_1) ? 1 : 0;
    const uint32_t seqNum = m_odtpSeq[seqIdx];
    writeLE32(raw + 12U, seqNum);
    if (advanceSeq) {
        ++m_odtpSeq[seqIdx];
    }

    writeLE16(raw + 16U, length);
    if (length > 0U && payload != nullptr) {
        ::memcpy(raw + REWIND_HEADER_LENGTH, payload, length);
    }

    m_udp->writeDatagram(pkt, m_address, m_modeinfo.port);

    if (m_debug) {
        QDebug debug = qDebug();
        debug.noquote();
        QString s = "ODTP SEND:";
        for (int i = 0; i < pkt.size(); ++i) {
            s += " " + QString("%1").arg(static_cast<uint8_t>(pkt.constData()[i]), 2, 16, QChar('0'));
        }
        debug << s;
    }
}

void DMR::send_odtp_keepalive()
{
    if (!m_udp) {
        return;
    }

    const QByteArray desc = QStringLiteral("DroidStar 2026").toUtf8();
    QByteArray verPayload;
    verPayload.resize(5 + desc.size());
    uint8_t *p = reinterpret_cast<uint8_t *>(verPayload.data());

    // ODTP requires the bare 7-digit DMR ID (never the 9-digit hotspot ESSID)
    writeLE32(p, m_dmrid);
    p[4] = REWIND_SERVICE_OPEN_TERMINAL;
    if (!desc.isEmpty()) {
        ::memcpy(p + 5, desc.constData(), static_cast<size_t>(desc.size()));
    }

    send_odtp_packet(REWIND_TYPE_KEEP_ALIVE,
                     REWIND_FLAG_NONE,
                     verPayload.constData(),
                     static_cast<uint16_t>(verPayload.size()));
}

void DMR::update_odtp_subscription(bool force)
{
    if (!m_odtp || !m_udp || m_modeinfo.status != CONNECTED_RW) {
        return;
    }

    // In Group Call mode, subscribe to the selected TG (except 0
    const uint32_t desiredGroupTg =
        (m_flco == FLCO_GROUP && m_txdstid > 0U && m_txdstid != 4000U && m_txdstid != 9990U)
            ? m_txdstid
            : 0U;

    if (!force && m_odtpSubscribedTg == desiredGroupTg && m_odtpSubscribedPrivateSelf) {
        return;
    }

    const uint32_t prevTg = m_odtpSubscribedTg;

    // 1. Cancel all previous active subscriptions on the BrandMeister master (0-byte
    send_odtp_packet(REWIND_TYPE_CANCELLING, REWIND_FLAG_NONE, nullptr, 0U);
    m_odtpSubscribedTg = 0U;
    m_odtpSubscribedPrivateSelf = false;
    m_linkedGroupTg = 0U;

    // 2. Always subscribe to Private Voice Calls addressed to our own 7-digit DMR ID
    if (m_dmrid > 0U) {
        uint8_t subPriv[8];
        writeLE32(subPriv + 0U, REWIND_SESSION_TYPE_PRIVATE_VOICE);
        writeLE32(subPriv + 4U, m_dmrid);
        send_odtp_packet(REWIND_TYPE_SUBSCRIPTION, REWIND_FLAG_NONE, subPriv, sizeof(subPriv));
        m_odtpSubscribedPrivateSelf = true;
    }

    // 3. Subscribe to the target Group Talkgroup if active
    if (desiredGroupTg > 0U) {
        uint8_t subGrp[8];
        writeLE32(subGrp + 0U, REWIND_SESSION_TYPE_GROUP_VOICE);
        writeLE32(subGrp + 4U, desiredGroupTg);
        send_odtp_packet(REWIND_TYPE_SUBSCRIPTION, REWIND_FLAG_NONE, subGrp, sizeof(subGrp));
        m_odtpSubscribedTg = desiredGroupTg;
        m_linkedGroupTg = desiredGroupTg;
        emit update_log(QString("DMR (ODTP): Subscribed to TG %1").arg(desiredGroupTg));
    } else if (m_txdstid == 4000U && prevTg != 0U) {
        emit update_log(QString("DMR (ODTP): Unsubscribed from TG %1").arg(prevTg));
    }
}

void DMR::handle_odtp_rx_header(uint32_t pktSrcId, uint32_t pktDstId, bool pktIsPrivate, const QString &srcCall, uint32_t seqNum)
{
    if (m_tx) {
        return;
    }
    if (!shouldAcceptRxPacket(pktSrcId, pktDstId, pktIsPrivate)) {
        return;
    }

    if (!pktIsPrivate && pktDstId > 0U && pktDstId != 4000U && pktDstId != 9U) {
        m_linkedGroupTg = pktDstId;
        m_unlinkedStaleTg = 0U;
    }

    const bool alreadyActive = (m_modeinfo.stream_state == STREAM_NEW || m_modeinfo.stream_state == STREAMING) &&
                               (m_rxtimer && m_rxtimer->isActive());
    const bool sameStream = alreadyActive &&
                            (m_modeinfo.srcid == 0U || pktSrcId == 0U || m_modeinfo.srcid == pktSrcId) &&
                            (m_modeinfo.dstid == 0U || pktDstId == 0U || m_modeinfo.dstid == pktDstId);

    m_rxwatchdog = 0;
    const uint32_t prevSrcId = m_modeinfo.srcid;
    const QString prevSrcCall = m_modeinfo.src;

    if (pktSrcId > 0U) {
        m_odtpRxSrcId = pktSrcId;
        m_modeinfo.srcid = pktSrcId;
    }
    if (pktDstId > 0U) {
        m_odtpRxDstId = pktDstId;
        m_modeinfo.dstid = pktDstId;
    }
    m_odtpRxIsPrivate = pktIsPrivate;
    if (!srcCall.isEmpty()) {
        m_odtpRxSrcCall = srcCall;
        m_modeinfo.src = srcCall;
    }
    m_modeinfo.gwid = m_dmrid;
    m_modeinfo.slot = 2U | (pktIsPrivate ? 0x40U : 0x00U);

    if (!sameStream) {
        m_rxcodecq.clear();
        rxJitterReset();
        if (m_audio) {
            m_audio->start_playback();
        }
        if (m_rxtimer && !m_rxtimer->isActive()) {
            m_rxtimer->start(m_rxtimerint);
        }
        m_modeinfo.stream_state = STREAM_NEW;
        m_modeinfo.ts = QDateTime::currentMSecsSinceEpoch();
        m_modeinfo.streamid = (seqNum != 0U) ? seqNum : (static_cast<uint32_t>(::rand()) | 1U);
        m_odtpRxFrameCnt = 0U;
        m_modeinfo.frame_number = 0U;
        qDebug() << "New DMR (ODTP) stream from" << m_modeinfo.srcid << "(" << m_modeinfo.src << ") to" << m_modeinfo.dstid
                 << (pktIsPrivate ? "(Private)" : "(Group)");
        emit update(m_modeinfo);
    } else if (m_modeinfo.srcid != prevSrcId || m_modeinfo.src != prevSrcCall) {
        emit update(m_modeinfo);
    }
}

void DMR::process_odtp_udp(const QByteArray &buf)
{
    if (buf.size() < static_cast<int>(REWIND_HEADER_LENGTH)) {
        return;
    }

    const uint8_t *raw = reinterpret_cast<const uint8_t *>(buf.constData());
    const uint16_t pktType = readLE16(raw + 8U);
    const uint32_t seqNum  = readLE32(raw + 12U);
    const uint16_t payloadLen = readLE16(raw + 16U);

    if (buf.size() < static_cast<int>(REWIND_HEADER_LENGTH) + static_cast<int>(payloadLen)) {
        return;
    }
    const uint8_t *payload = raw + REWIND_HEADER_LENGTH;

    if (pktType != REWIND_TYPE_DMR_AUDIO_FRAME &&
        pktType != REWIND_TYPE_DMR_VOICE_HEADER &&
        pktType != REWIND_TYPE_SUPER_HEADER &&
        pktType != REWIND_TYPE_DMR_EMBEDDED_DATA &&
        pktType != REWIND_TYPE_MESSAGE_TEXT &&
        pktType != REWIND_TYPE_MESSAGE_STATUS &&
        (m_modeinfo.stream_state == STREAM_LOST || m_modeinfo.stream_state == STREAM_END)) {
        if (!m_rxtimer || !m_rxtimer->isActive()) {
            m_modeinfo.stream_state = STREAM_IDLE;
        }
    }

    switch (pktType) {
    case REWIND_TYPE_CHALLENGE: {
        if (m_password.isEmpty()) {
            emit update_log("DMR (ODTP): login rejected. BrandMeister hotspot password is empty.");
            m_modeinfo.status = DISCONNECTED;
            emit update(m_modeinfo);
            return;
        }
        ++m_odtpAuthAttempts;
        if (m_odtpAuthAttempts > 4) {
            emit update_log("DMR (ODTP): authentication failed. Check BrandMeister hotspot security password.");
            m_modeinfo.status = DISCONNECTED;
            emit update(m_modeinfo);
            return;
        }
        if (m_modeinfo.status != CONNECTED_RW) {
            m_modeinfo.status = DMR_AUTH;
        }
        m_odtpConfigured = false;
        m_odtpSubscribedTg = 0U;
        m_odtpSubscribedPrivateSelf = false;

        QByteArray authIn;
        authIn.append(reinterpret_cast<const char *>(payload), static_cast<int>(payloadLen));
        authIn.append(m_password.toUtf8());
        const QByteArray digest = QCryptographicHash::hash(authIn, QCryptographicHash::Sha256);
        if (digest.size() == 32) {
            send_odtp_packet(REWIND_TYPE_AUTHENTICATION, REWIND_FLAG_NONE, digest.constData(), 32U);
        } else {
            uint8_t shaOut[32U];
            CSHA256 sha256;
            sha256.buffer(reinterpret_cast<const uint8_t *>(authIn.constData()),
                          static_cast<uint32_t>(authIn.size()),
                          shaOut);
            send_odtp_packet(REWIND_TYPE_AUTHENTICATION, REWIND_FLAG_NONE, shaOut, 32U);
        }
        return;
    }

    case REWIND_TYPE_KEEP_ALIVE: {
        m_odtpAuthAttempts = 0;
        m_odtpConnectRetries = 0;
        m_modeinfo.count++;

        const bool wasConnected = (m_modeinfo.status == CONNECTED_RW);
        if (!wasConnected) {
            setup_connection();
        }
        if (!m_odtpConfigured) {
            uint8_t conf[4U];
            writeLE32(conf, REWIND_OPTION_SUPER_HEADER);
            send_odtp_packet(REWIND_TYPE_CONFIGURATION, REWIND_FLAG_NONE, conf, sizeof(conf));
            m_odtpConfigured = true;
            update_odtp_subscription(true);
        }
        emit update(m_modeinfo);
        return;
    }

    case REWIND_TYPE_CONFIGURATION:
    case REWIND_TYPE_SUBSCRIPTION:
    case REWIND_TYPE_CANCELLING:
        return;

    case REWIND_TYPE_REDIRECTION: {
        if (payloadLen >= 8U) {
            const uint16_t family = readLE16(payload);
            const uint16_t redirPort = readLE16(payload + 2U);
            if (family == 2U && redirPort > 0U) {
                const quint32 ipv4 = (static_cast<quint32>(payload[4]) << 24) |
                                     (static_cast<quint32>(payload[5]) << 16) |
                                     (static_cast<quint32>(payload[6]) << 8) |
                                     static_cast<quint32>(payload[7]);
                const QHostAddress newAddr(ipv4);
                if (!newAddr.isNull()) {
                    emit update_log(QString("DMR (ODTP): redirected to %1:%2").arg(newAddr.toString()).arg(redirPort));
                    m_address = newAddr;
                    m_modeinfo.port = redirPort;
                    m_odtpConfigured = false;
                    send_odtp_keepalive();
                }
            }
        }
        return;
    }

    case REWIND_TYPE_CLOSE: {
        emit update_log("DMR (ODTP): master closed connection.");
        m_modeinfo.status = CLOSED;
        emit update(m_modeinfo);
        return;
    }

    case REWIND_TYPE_REPORT:
    case REWIND_TYPE_FAILURE_CODE: {
        if (payloadLen > 0U) {
            const QString msg = QString::fromUtf8(reinterpret_cast<const char *>(payload), static_cast<int>(payloadLen)).trimmed();
            if (!msg.isEmpty()) {
                emit update_log("DMR (ODTP): " + msg);
            }
        }
        return;
    }

    case REWIND_TYPE_SUPER_HEADER: {
        if (m_modeinfo.status != CONNECTED_RW || m_tx || payloadLen < 32U) {
            return;
        }
        const uint32_t sessionType = readLE32(payload + 0U);
        if (sessionType != REWIND_SESSION_TYPE_PRIVATE_VOICE &&
            sessionType != REWIND_SESSION_TYPE_GROUP_VOICE) {
            return;
        }
        const uint32_t pktSrcId    = readLE32(payload + 4U);
        const uint32_t pktDstId    = readLE32(payload + 8U);
        const bool pktIsPrivate    = (sessionType == REWIND_SESSION_TYPE_PRIVATE_VOICE);

        const char *rawSrcCall = reinterpret_cast<const char *>(payload + 12U);
        size_t callLen = 0;
        while (callLen < REWIND_CALL_LENGTH && rawSrcCall[callLen] != '\0') {
            ++callLen;
        }
        const QString srcCall = QString::fromLatin1(rawSrcCall, static_cast<qsizetype>(callLen)).trimmed();

        handle_odtp_rx_header(pktSrcId, pktDstId, pktIsPrivate, srcCall, seqNum);
        return;
    }

    case REWIND_TYPE_DMR_VOICE_HEADER: {
        if (m_modeinfo.status != CONNECTED_RW || m_tx || payloadLen < 9U) {
            return;
        }
        const uint8_t flco = payload[0U] & 0x3FU;
        const bool pktIsPrivate = (flco == FLCO_USER_USER);
        const uint32_t pktDstId = (static_cast<uint32_t>(payload[3U]) << 16) |
                                  (static_cast<uint32_t>(payload[4U]) << 8) |
                                  static_cast<uint32_t>(payload[5U]);
        const uint32_t pktSrcId = (static_cast<uint32_t>(payload[6U]) << 16) |
                                  (static_cast<uint32_t>(payload[7U]) << 8) |
                                  static_cast<uint32_t>(payload[8U]);

        handle_odtp_rx_header(pktSrcId, pktDstId, pktIsPrivate, m_odtpRxSrcCall, seqNum);
        return;
    }

    case REWIND_TYPE_DMR_EMBEDDED_DATA: {
        if (m_modeinfo.status != CONNECTED_RW || m_tx || payloadLen < 9U) {
            return;
        }
        const uint8_t flco = payload[0U] & 0x3FU;
        if (flco == FLCO_GROUP || flco == FLCO_USER_USER) {
            const bool pktIsPrivate = (flco == FLCO_USER_USER);
            const uint32_t pktDstId = (static_cast<uint32_t>(payload[3U]) << 16) |
                                      (static_cast<uint32_t>(payload[4U]) << 8) |
                                      static_cast<uint32_t>(payload[5U]);
            const uint32_t pktSrcId = (static_cast<uint32_t>(payload[6U]) << 16) |
                                      (static_cast<uint32_t>(payload[7U]) << 8) |
                                      static_cast<uint32_t>(payload[8U]);
            if (pktSrcId > 0U && shouldAcceptRxPacket(pktSrcId, pktDstId, pktIsPrivate)) {
                m_odtpRxSrcId = pktSrcId;
                m_odtpRxDstId = pktDstId;
                m_odtpRxIsPrivate = pktIsPrivate;
                if (m_modeinfo.srcid == 0U) {
                    m_modeinfo.srcid = pktSrcId;
                    m_modeinfo.dstid = pktDstId;
                    emit update(m_modeinfo);
                }
            }
        }
        return;
    }

    case REWIND_TYPE_DMR_AUDIO_FRAME: {
        if (m_modeinfo.status != CONNECTED_RW || m_tx || payloadLen < 27U) {
            return;
        }
        const uint32_t pktSrcId = m_odtpRxSrcId;
        const uint32_t pktDstId = (m_odtpRxDstId != 0U) ? m_odtpRxDstId : m_txdstid;
        const bool pktIsPrivate = m_odtpRxIsPrivate;

        if (pktDstId > 0U && !shouldAcceptRxPacket(pktSrcId, pktDstId, pktIsPrivate)) {
            return;
        }

        if (!m_rxtimer->isActive() ||
            m_modeinfo.stream_state == STREAM_LOST ||
            m_modeinfo.stream_state == STREAM_END ||
            m_modeinfo.stream_state == STREAM_IDLE) {
            m_rxcodecq.clear();
            rxJitterReset();
            m_audio->start_playback();
            if (!m_rxtimer->isActive()) {
                m_rxtimer->start(m_rxtimerint);
            }
            m_modeinfo.stream_state = STREAM_NEW;
            m_modeinfo.ts = QDateTime::currentMSecsSinceEpoch();
            if (m_modeinfo.streamid == 0U) {
                m_modeinfo.streamid = (seqNum != 0U) ? seqNum : (static_cast<uint32_t>(::rand()) | 1U);
            }
            m_odtpRxFrameCnt = 0U;
        } else {
            m_modeinfo.stream_state = STREAMING;
        }
        m_rxwatchdog = 0;

        m_modeinfo.srcid = pktSrcId;
        m_modeinfo.dstid = pktDstId;
        m_modeinfo.gwid  = m_dmrid;
        if (!m_odtpRxSrcCall.isEmpty()) {
            m_modeinfo.src = m_odtpRxSrcCall;
        }
        m_modeinfo.frame_number = ++m_odtpRxFrameCnt;
        m_modeinfo.slot = 2U | (pktIsPrivate ? 0x40U : 0x00U);

        // Prevent unbounded jitter queue latency if network bursts packets
        while (m_rxcodecq.size() > (9 * 24)) {
            for (int k = 0; k < 9 && !m_rxcodecq.isEmpty(); ++k) {
                m_rxcodecq.dequeue();
            }
        }

        // Feed the 3 Mode-33 9-byte AMBE frames (27 bytes total) directly into the vocoder queue
        for (int i = 0; i < 27; ++i) {
            m_rxcodecq.append(payload[i]);
        }

        if (m_modeinfo.stream_state == STREAM_NEW || (m_odtpRxFrameCnt % 6U) == 1U) {
            emit update(m_modeinfo);
        }
        return;
    }

    case REWIND_TYPE_DMR_TERMINATOR_WITH_LC: {
        if (m_modeinfo.status != CONNECTED_RW || m_tx) {
            return;
        }
        m_rxwatchdog = 0;
        qDebug() << "DMR (ODTP) RX EOT";
        m_modeinfo.stream_state = STREAM_END;
        m_modeinfo.ts = QDateTime::currentMSecsSinceEpoch();
        m_modeinfo.streamid = 0;
        m_odtpRxSrcId = 0U;
        m_odtpRxDstId = 0U;
        m_odtpRxIsPrivate = false;
        m_odtpRxSrcCall.clear();
        m_odtpRxFrameCnt = 0U;
        emit update(m_modeinfo);
        return;
    }

    case REWIND_TYPE_MESSAGE_TEXT: {
        DmrDataCodec::DecodedSms sms;
        if (!DmrDataCodec::decodeOdtpTextPayload(payload, payloadLen, sms)) {
            emit update_log("DMR SMS: malformed ODTP text message ignored");
            return;
        }
        const bool addressedToUs = sms.dstId == m_dmrid || sms.dstId == m_essid;
        const bool subscribedGroup = sms.isGroup &&
            (sms.dstId == m_txdstid || sms.dstId == m_odtpSubscribedTg);
        if (!addressedToUs && !subscribedGroup) {
            return;
        }
        if (!sms.isGroup) {
            const QByteArray ack = DmrDataCodec::encodeOdtpStatusPayload(
                m_dmrid, sms.srcId, 0x08U);
            send_odtp_packet(REWIND_TYPE_MESSAGE_STATUS, REWIND_FLAG_NONE,
                             ack.constData(), static_cast<uint16_t>(ack.size()));
        }
        emit update_log(QString("DMR SMS RX from %1 to %2 (ODTP): %3")
                        .arg(sms.srcId).arg(sms.dstId).arg(sms.text));
        emit sms_received(sms.srcId, sms.dstId, sms.text, sms.isGroup,
                          QStringLiteral("ODTP"));
        return;
    }

    case REWIND_TYPE_MESSAGE_STATUS: {
        uint32_t statusSrc = 0U;
        uint32_t statusDst = 0U;
        uint8_t status = 0U;
        if (DmrDataCodec::decodeOdtpStatusPayload(
                payload, payloadLen, statusSrc, statusDst, status) &&
            (statusDst == m_dmrid || statusDst == m_essid ||
             statusSrc == m_dmrid || statusSrc == m_essid)) {
            const uint32_t peerId = (statusDst == m_dmrid || statusDst == m_essid)
                                    ? statusSrc
                                    : statusDst;
            const bool ok = ((status & 0xC0U) == 0U) && (((status >> 3U) & 0x07U) == 1U);
            emit sms_ack_received(peerId, ok);
        }
        return;
    }

    default:
        return;
    }
}

void DMR::process_udp()
{
    QByteArray buf;
    QByteArray in;
    QByteArray out;
    QHostAddress sender;
    quint16 senderPort;
    CSHA256 sha256;
    char buffer[400U];

    buf.resize(m_udp->pendingDatagramSize());
    m_udp->readDatagram(buf.data(), buf.size(), &sender, &senderPort);

    if (buf.isEmpty()) {
        return;
    }

    if(m_debug){
        QDebug debug = qDebug();
        debug.noquote();
        QString s = "RECV:";
        for(int i = 0; i < buf.size(); ++i){
            s += " " + QString("%1").arg((uint8_t)buf.data()[i], 2, 16, QChar('0'));
        }
        debug << s;
    }

    if ((buf.size() >= static_cast<int>(REWIND_HEADER_LENGTH)) &&
        (::memcmp(buf.constData(), REWIND_PROTOCOL_SIGN, REWIND_SIGN_LENGTH) == 0)) {
        m_odtp = true;
        process_odtp_udp(buf);
        return;
    }

    if ((buf.size() >= 6) && (::memcmp(buf.data() + 3, "NAK", 3U) == 0)) {
        if (m_modeinfo.status == CONNECTED_RW) {
            // Master lost session state (e.g. NAT rebind or BM/TGIF master restart)
            emit update_log("DMR: session expired (MSTNAK), re-authenticating...");
            m_modeinfo.status = CONNECTING;
            out.clear();
            out.append('R');
            out.append('P');
            out.append('T');
            out.append('L');
            out.append((m_essid >> 24) & 0xff);
            out.append((m_essid >> 16) & 0xff);
            out.append((m_essid >> 8) & 0xff);
            out.append((m_essid >> 0) & 0xff);
            m_udp->writeDatagram(out, m_address, m_modeinfo.port);
            return;
        } else if (m_isTgif && !m_tgifTriedDefaultPass && m_password != QStringLiteral("passw0rd")) {
            // If user configured a custom TGIF password in Settings, but their TGIF account has
            m_tgifTriedDefaultPass = true;
            m_password = QStringLiteral("passw0rd");
            m_modeinfo.status = CONNECTING;
            emit update_log("DMR (TGIF): Hotspot security password rejected, retrying with default TGIF password...");
            out.clear();
            out.append('R');
            out.append('P');
            out.append('T');
            out.append('L');
            out.append((m_essid >> 24) & 0xff);
            out.append((m_essid >> 16) & 0xff);
            out.append((m_essid >> 8) & 0xff);
            out.append((m_essid >> 0) & 0xff);
            m_udp->writeDatagram(out, m_address, m_modeinfo.port);
            return;
        } else {
            // NAK during login/config typically indicates auth/config rejection.
            if (m_password.isEmpty()) {
                emit update_log("DMR: login rejected (NAK). Password is empty.");
            } else if (m_isTgif) {
                emit update_log("DMR (TGIF): login rejected (NAK). Check your TGIF Hotspot Security password in Settings.");
            } else {
                emit update_log("DMR: login/config rejected (NAK). Check password and DMR profile fields.");
            }
            m_modeinfo.status = DISCONNECTED;
        }
    }
    if ((buf.size() >= 5) && (::memcmp(buf.data(), "MSTCL", 5U) == 0)) {
        emit update_log("DMR: master closed connection (MSTCL).");
        m_modeinfo.status = CLOSED;
    }
    if ((m_modeinfo.status != CONNECTED_RW) && (buf.size() >= 6) && (::memcmp(buf.data(), "RPTACK", 6U) == 0)) {
        switch(m_modeinfo.status){
        case CONNECTING:
            if (buf.size() >= 10) {
                m_modeinfo.status = DMR_AUTH;
                in.append(buf.constData() + 6, 4);
                in.append(m_password.toUtf8());

                const QByteArray digest = QCryptographicHash::hash(in, QCryptographicHash::Sha256);
                out.clear();
                out.resize(40);
                out[0] = 'R';
                out[1] = 'P';
                out[2] = 'T';
                out[3] = 'K';
                out[4] = (m_essid >> 24) & 0xff;
                out[5] = (m_essid >> 16) & 0xff;
                out[6] = (m_essid >> 8) & 0xff;
                out[7] = (m_essid >> 0) & 0xff;
                if (digest.size() == 32) {
                    ::memcpy(out.data() + 8U, digest.constData(), 32U);
                } else {
                    sha256.buffer(reinterpret_cast<const uint8_t *>(in.constData()),
                                  static_cast<uint32_t>(in.size()),
                                  reinterpret_cast<uint8_t *>(out.data()) + 8U);
                }
            }
            break;
        case DMR_AUTH:
            out.clear();
            buffer[0] = 'R';
            buffer[1] = 'P';
            buffer[2] = 'T';
            buffer[3] = 'C';
            buffer[4] = (m_essid >> 24) & 0xff;
            buffer[5] = (m_essid >> 16) & 0xff;
            buffer[6] = (m_essid >> 8) & 0xff;
            buffer[7] = (m_essid >> 0) & 0xff;

            m_modeinfo.status = DMR_CONF;
            char latitude[20U];
            char longitude[20U];

            snprintf(latitude, sizeof(latitude), "%08f", m_lat.toFloat());
            snprintf(longitude, sizeof(longitude), "%09f", m_lon.toFloat());

            char *p;
            if((p = strchr(latitude, ',')) != NULL){
                *p = '.';
            }
            if((p = strchr(longitude, ',')) != NULL){
                *p = '.';
            }
            ::snprintf(buffer + 8U, sizeof(buffer) - 8U,
                       "%-8.8s%09u%09u%02u%02u%8.8s%9.9s%03d%-20.20s%-19.19s%c%-124.124s%-40.40s%-40.40s",
                       m_modeinfo.callsign.toStdString().c_str(),
                       m_freq.toUInt(), m_freq.toUInt(), 1, 1, latitude, longitude, 0,
                       m_location.toStdString().c_str(), m_desc.toStdString().c_str(), '4',
                       m_url.toStdString().c_str(), m_swid.toStdString().c_str(), m_pkid.toStdString().c_str());
            out.append(buffer, 302);
            break;
        case DMR_CONF:
            setup_connection();
            if(m_options.size()){
                out.clear();
                out.append('R');
                out.append('P');
                out.append('T');
                out.append('O');
                out.append((m_essid >> 24) & 0xff);
                out.append((m_essid >> 16) & 0xff);
                out.append((m_essid >> 8) & 0xff);
                out.append((m_essid >> 0) & 0xff);
                out.append(m_options.toUtf8());
            }
            break;
        case DMR_OPTS:
            //setup_connection();
            break;
        default:
            break;
        }
        if (!out.isEmpty()) {
            m_udp->writeDatagram(out, m_address, m_modeinfo.port);
        }
    }
    if((buf.size() == 11) && (::memcmp(buf.data(), "MSTPONG", 7U) == 0)){
        m_modeinfo.count++;
    }
    if((buf.size() != 55) && ( (m_modeinfo.stream_state == STREAM_LOST) || (m_modeinfo.stream_state == STREAM_END) )){
        if (!m_rxtimer || !m_rxtimer->isActive()) {
            m_modeinfo.stream_state = STREAM_IDLE;
        }
    }
    if((buf.size() == 55) &&
        (::memcmp(buf.data(), "DMRD", 4U) == 0) &&
        (m_modeinfo.status == CONNECTED_RW))
    {
        // Ignore incoming RX packets while transmitting (simplex hotspot) so they never
        if (m_tx) {
            return;
        }

        const uint32_t pktSrcId = (uint32_t)(((uint8_t)buf.data()[5] << 16) | (((uint8_t)buf.data()[6] << 8) & 0xff00) | ((uint8_t)buf.data()[7] & 0xff));
        const uint32_t pktDstId = (uint32_t)(((uint8_t)buf.data()[8] << 16) | (((uint8_t)buf.data()[9] << 8) & 0xff00) | ((uint8_t)buf.data()[10] & 0xff));
        const uint32_t pktGwId  = (uint32_t)(((uint8_t)buf.data()[11] << 24) | (((uint8_t)buf.data()[12] << 16) & 0xff0000) | (((uint8_t)buf.data()[13] << 8) & 0xff00) | ((uint8_t)buf.data()[14] & 0xff));
        const uint32_t pktStreamId = (uint32_t)(((uint8_t)buf.data()[16] << 24) | (((uint8_t)buf.data()[17] << 16) & 0xff0000) | (((uint8_t)buf.data()[18] << 8) & 0xff00) | ((uint8_t)buf.data()[19] & 0xff));
        const uint8_t pktFlags = (uint8_t)buf.data()[15];
        const bool pktIsPrivate = (pktFlags & 0x40U) != 0U;
        const bool pktIsDataSync = (pktFlags & 0x20U) != 0U;

        if (pktSrcId == 4000U || pktDstId == 4000U) {
            m_saw4000Rx = true;
            m_last4000PktMs = QDateTime::currentMSecsSinceEpoch();
            if (pktIsDataSync && (pktFlags & DT_MASK) == DT_TERMINATOR_WITH_LC) {
                m_4000EotReceived = true;
            }
        }

        // Packet-data calls have their own addressing and lifecycle. Process
        // them before the selected voice-TG filter and return without touching
        // m_modeinfo, AudioEngine, transcription, QSO logging, or link state.
        if (pktIsDataSync) {
            const uint8_t dataType = pktFlags & DT_MASK;
            if (dataType == DT_CSBK || dataType == DT_DATA_HEADER ||
                dataType == DT_RATE_12_DATA || dataType == DT_RATE_34_DATA ||
                dataType == DT_RATE_1_DATA) {
                if (shouldAcceptRxDataPacket(pktSrcId, pktDstId, pktIsPrivate)) {
                    const uint8_t rxSlot = (pktFlags & 0x80U) ? 2U : 1U;
                    const auto result = m_smsAssembler.processBurst(
                        pktSrcId, pktDstId, rxSlot, pktIsPrivate, dataType,
                        reinterpret_cast<const uint8_t *>(buf.constData() + 20),
                        m_dmrid, m_essid, m_txcc);

                    if (!result.autoAckDmrdFrames.isEmpty()) {
                        for (const QByteArray &frame : result.autoAckDmrdFrames) {
                            m_smsTxQueue.enqueue(PendingSmsBurst{-1, false, frame});
                        }
                        if (!m_smsTxTimer) {
                            m_smsTxTimer = new QTimer(this);
                            m_smsTxTimer->setTimerType(Qt::PreciseTimer);
                            m_smsTxTimer->setInterval(static_cast<int>(DMR_SLOT_TIME));
                            connect(m_smsTxTimer, &QTimer::timeout,
                                    this, &DMR::process_sms_tx_queue);
                        }
                        if (!m_smsTxTimer->isActive()) m_smsTxTimer->start();
                    }
                    if (result.valid) {
                        if (result.isAck) {
                            emit sms_ack_received(result.srcId, result.ackSuccess);
                        } else {
                            emit update_log(QString("DMR SMS RX from %1 to %2 (%3): %4")
                                            .arg(result.srcId).arg(result.dstId)
                                            .arg(result.formatName, result.text));
                            emit sms_received(result.srcId, result.dstId, result.text,
                                              result.isGroup, result.formatName);
                        }
                    }
                }
                return;
            }
        }

        // Filter out RX packets from previous/unselected Talkgroups
        if (!shouldAcceptRxPacket(pktSrcId, pktDstId, pktIsPrivate)) {
            return;
        }

        if (!pktIsPrivate && pktDstId > 0U && pktDstId != 4000U && pktDstId != 9U &&
            pktDstId != m_dmrid && pktDstId != m_essid) {
            m_linkedGroupTg = pktDstId;
            m_unlinkedStaleTg = 0U;
        }

        if (pktIsDataSync) {
            const uint8_t dataType = pktFlags & DT_MASK;
            uint8_t t = 0;

            if (dataType == DT_TERMINATOR_WITH_LC) {
                // A packet-data terminator is not a voice EOT. If there is no
                // active voice stream, consume it without emitting STREAM_END.
                if (m_modeinfo.stream_state == STREAM_IDLE &&
                    m_modeinfo.streamid == 0U) {
                    return;
                }
                if (m_modeinfo.streamid != 0U && pktStreamId != m_modeinfo.streamid) {
                    return;
                }
                m_rxwatchdog = 0;
                qDebug() << "DMR RX EOT";
                m_modeinfo.stream_state = STREAM_END;
                m_modeinfo.ts = QDateTime::currentMSecsSinceEpoch();
                m_modeinfo.streamid = 0;
                t = 0x42;
                emit update(m_modeinfo);
            } else if (dataType == DT_VOICE_LC_HEADER) {
                m_rxwatchdog = 0;
                if (!m_tx) {
                    m_rxcodecq.clear();
                    rxJitterReset();
                    m_audio->start_playback();
                    if (!m_rxtimer->isActive()) {
                        m_rxtimer->start(m_rxtimerint);
                    }
                }
                m_modeinfo.stream_state = STREAM_NEW;
                m_modeinfo.ts = QDateTime::currentMSecsSinceEpoch();
                m_modeinfo.srcid = pktSrcId;
                m_modeinfo.dstid = pktDstId;
                m_modeinfo.gwid = pktGwId;
                m_modeinfo.streamid = pktStreamId;
                m_modeinfo.frame_number = (uint8_t)buf.data()[4];
                m_modeinfo.slot = ((pktFlags & 0x80U) ? 2U : 1U) | (pktFlags & 0x40U);
                t = 0x41;
                qDebug() << "New DMR stream from " << m_modeinfo.srcid << " to " << m_modeinfo.dstid
                         << (pktIsPrivate ? "(Private)" : "(Group)");
                emit update(m_modeinfo);
            } else {
                // Ignore CSBK (0x03), Data Header (0x06), Rate Data (0x07/0x08/0x0A), Idle (0x09)
                // so they don't falsely trigger STREAM_NEW or STREAM_END
                return;
            }

            if (m_modem && t != 0) {
                m_rxmodemq.append(MMDVM_FRAME_START);
                m_rxmodemq.append(0x25);
                m_rxmodemq.append(MMDVM_DMR_DATA2);
                m_rxmodemq.append(t);

                for (int i = 0; i < 33; ++i) {
                    m_rxmodemq.append(buf.data()[20 + i]);
                }
            }
            return;
        } else {
            // Voice burst (Audio Sync or Voice frame)
            if (!m_tx && (!m_rxtimer->isActive() ||
                          (m_modeinfo.stream_state == STREAM_LOST) ||
                          (m_modeinfo.stream_state == STREAM_END) ||
                          (m_modeinfo.stream_state == STREAM_IDLE) ||
                          (m_modeinfo.streamid != 0U && m_modeinfo.streamid != pktStreamId))) {
                m_rxcodecq.clear();
                rxJitterReset();
                m_audio->start_playback();
                if (!m_rxtimer->isActive()) {
                    m_rxtimer->start(m_rxtimerint);
                }
                m_modeinfo.stream_state = STREAM_NEW;
                m_modeinfo.ts = QDateTime::currentMSecsSinceEpoch();
            } else {
                m_modeinfo.stream_state = STREAMING;
            }
            m_rxwatchdog = 0;

            uint8_t dmrframe[33];
            uint8_t dmr3ambe[27];
            uint8_t dmrsync[7];
            // get the 33 bytes ambe
            memcpy(dmrframe, &(buf.data()[20]), 33);
            // extract the 3 ambe frames
            memcpy(dmr3ambe, dmrframe, 14);
            dmr3ambe[13] &= 0xF0;
            dmr3ambe[13] |= (dmrframe[19] & 0x0F);
            memcpy(&dmr3ambe[14], &dmrframe[20], 13);
            // extract sync
            dmrsync[0] = dmrframe[13] & 0x0F;
            ::memcpy(&dmrsync[1], &dmrframe[14], 5);
            dmrsync[6] = dmrframe[19] & 0xF0;
            m_modeinfo.srcid = pktSrcId;
            m_modeinfo.dstid = pktDstId;
            m_modeinfo.gwid = pktGwId;
            m_modeinfo.streamid = pktStreamId;
            m_modeinfo.frame_number = (uint8_t)buf.data()[4];
            m_modeinfo.slot = ((pktFlags & 0x80U) ? 2U : 1U) | (pktFlags & 0x40U);

            if (m_modem) {
                uint8_t t = (pktFlags & 0x0f);
                if (!t) t = 0x20;

                m_rxmodemq.append(MMDVM_FRAME_START);
                m_rxmodemq.append(0x25);
                m_rxmodemq.append(MMDVM_DMR_DATA2);
                m_rxmodemq.append(t);

                for (int i = 0; i < 33; ++i) {
                    m_rxmodemq.append(buf.data()[20 + i]);
                }
            }

            // Prevent unbounded jitter queue latency if network bursts packets
            while (m_rxcodecq.size() > (9 * 24)) {
                for (int k = 0; k < 9 && !m_rxcodecq.isEmpty(); ++k) {
                    m_rxcodecq.dequeue();
                }
            }

            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 9; ++j) {
                    m_rxcodecq.append(dmr3ambe[j + (9 * i)]);
                }
            }
            // Throttle cross-thread UI updates during continuous voice streaming (emit on
            if (m_modeinfo.stream_state == STREAM_NEW || (pktFlags & 0x10U) != 0U) {
                emit update(m_modeinfo);
            }
            return;
        }
    }
    emit update(m_modeinfo);

    if(m_debug && out.size() > 0){
        QDebug debug = qDebug();
        debug.noquote();
        QString s = "SEND:";
        for(int i = 0; i < out.size(); ++i){
            s += " " + QString("%1").arg((uint8_t)out.data()[i], 2, 16, QChar('0'));
        }
        debug << s;
    }
}

void DMR::setup_connection()
{
    m_modeinfo.status = CONNECTED_RW;
    if (!m_mbevocoder) {
        m_modeinfo.sw_vocoder_loaded = load_vocoder_plugin();
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
    if (!m_ping_timer) {
        m_ping_timer = new QTimer(this);
        connect(m_ping_timer, SIGNAL(timeout()), this, SLOT(send_ping()));
    }
    if (!m_linkTimer) {
        m_linkTimer = new QTimer(this);
        m_linkTimer->setTimerType(Qt::PreciseTimer);
        connect(m_linkTimer, SIGNAL(timeout()), this, SLOT(onAutoLinkTick()));
    }
    m_ping_timer->start(5000);
    if (!m_audio) {
        m_audio = new AudioEngine(m_audioin, m_audioout);
        m_audio->init();
        m_audio->set_agc(m_pendingAgc);
        m_audio->set_input_volume(m_pendingInVol / m_attenuation);
        m_audio->set_output_volume(m_pendingOutVol);
    }
    prepare_silence_ambe();
    m_linkedGroupTg = 0U;
    m_autoLinkState = AUTOLINK_IDLE;

    if (!m_odtp) {
        const uint32_t gen = ++m_linkGeneration;
        QTimer::singleShot(180, this, [this, gen]() {
            if (gen != m_linkGeneration || !m_udp || m_modeinfo.status != CONNECTED_RW || m_tx) {
                return;
            }
            if (m_flco == FLCO_GROUP && m_txdstid > 0U && m_txdstid != 4000U &&
                m_txdstid != 9990U && m_txdstid != 31000U) {
                if (m_isTgif) {
                    request_tgif_talkgroup_switch(m_txdstid, true);
                } else {
                    schedule_group_link(true);
                }
            }
        });
    }
}

void DMR::hostname_lookup(QHostInfo i)
{
    if (!i.addresses().isEmpty()) {
        m_address = i.addresses().first();
        m_udp = new QUdpSocket(this);
        connect(m_udp, SIGNAL(readyRead()), this, SLOT(process_udp()));

        if (m_odtp) {
            m_odtpSeq[0] = 0U;
            m_odtpSeq[1] = 0U;
            m_odtpAuthAttempts = 0;
            m_odtpConnectRetries = 0;
            m_odtpConfigured = false;
            m_odtpSubscribedTg = 0U;
            m_odtpSubscribedPrivateSelf = false;
            m_odtpRxSrcId = 0U;
            m_odtpRxDstId = 0U;
            m_odtpRxIsPrivate = false;
            m_odtpRxSrcCall.clear();
            m_odtpRxFrameCnt = 0U;

            send_odtp_keepalive();

            if (!m_ping_timer) {
                m_ping_timer = new QTimer(this);
                connect(m_ping_timer, SIGNAL(timeout()), this, SLOT(send_ping()));
            }
            m_ping_timer->start(2000);
            return;
        }

        QByteArray out;
        out.append('R');
        out.append('P');
        out.append('T');
        out.append('L');
        out.append((m_essid >> 24) & 0xff);
        out.append((m_essid >> 16) & 0xff);
        out.append((m_essid >> 8) & 0xff);
        out.append((m_essid >> 0) & 0xff);
        m_udp->writeDatagram(out, m_address, m_modeinfo.port);

        if (!m_ping_timer) {
            m_ping_timer = new QTimer(this);
            connect(m_ping_timer, SIGNAL(timeout()), this, SLOT(send_ping()));
        }
        m_ping_timer->start(3000);

        if(m_debug){
            QDebug debug = qDebug();
            debug.noquote();
            QString s = "CONN:";
            for(int i = 0; i < out.size(); ++i){
                s += " " + QString("%1").arg((uint8_t)out.data()[i], 2, 16, QChar('0'));
            }
            debug << s;
        }
    } else {
        emit update_log("DMR: DNS lookup failed for host " + m_modeinfo.host);
        m_modeinfo.status = DISCONNECTED;
        emit update(m_modeinfo);
    }
}

void DMR::send_ping()
{
    if (!m_udp) return;
    if (m_odtp) {
        if (m_modeinfo.status == CONNECTING || m_modeinfo.status == DMR_AUTH) {
            ++m_odtpConnectRetries;
            if (m_odtpConnectRetries <= 3 || m_odtpAuthAttempts > 0) {
                send_odtp_keepalive();
                return;
            }
            // Port 54006 did not respond at all after 3 keep-alive attempts; fall back to Homebrew
            m_odtp = false;
            m_modeinfo.port = m_hbFallbackPort;
            m_modeinfo.status = CONNECTING;
            emit update_log(QString("DMR: ODTP port 54006 unreachable, falling back to Homebrew (port %1)...").arg(m_modeinfo.port));
            if (m_ping_timer) {
                m_ping_timer->setInterval(3000);
            }
        } else {
            send_odtp_keepalive();
            return;
        }
    }

    QByteArray out;
    if (m_modeinfo.status == CONNECTING) {
        // Retry RPTL if initial login UDP packet was dropped
        out.append('R');
        out.append('P');
        out.append('T');
        out.append('L');
        out.append((m_essid >> 24) & 0xff);
        out.append((m_essid >> 16) & 0xff);
        out.append((m_essid >> 8) & 0xff);
        out.append((m_essid >> 0) & 0xff);
        m_udp->writeDatagram(out, m_address, m_modeinfo.port);
        return;
    }
    char tag[] = { 'R','P','T','P','I','N','G' };
    out.append(tag, 7);
    out.append((m_essid >> 24) & 0xff);
    out.append((m_essid >> 16) & 0xff);
    out.append((m_essid >> 8) & 0xff);
    out.append((m_essid >> 0) & 0xff);
    m_udp->writeDatagram(out, m_address, m_modeinfo.port);

    if(m_debug){
        QDebug debug = qDebug();
        debug.noquote();
        QString s = "PING:";
        for(int i = 0; i < out.size(); ++i){
            s += " " + QString("%1").arg((uint8_t)out.data()[i], 2, 16, QChar('0'));
        }
        debug << s;
    }
}

void DMR::send_disconnect()
{
    if (!m_udp) return;
    if (m_smsTxTimer) {
        m_smsTxTimer->stop();
    }
    QSet<int> cancelledSms;
    while (!m_smsTxQueue.isEmpty()) {
        const PendingSmsBurst item = m_smsTxQueue.dequeue();
        if (item.msgId >= 0) cancelledSms.insert(item.msgId);
    }
    for (int id : std::as_const(cancelledSms)) {
        emit sms_send_status(id, 3, QStringLiteral("Disconnected before DMR data send completed"));
    }
    m_smsAssembler.reset();
    if (m_linkTimer) {
        m_linkTimer->stop();
    }
    if (m_odtp) {
        if (m_modeinfo.status == CONNECTED_RW) {
            send_odtp_packet(REWIND_TYPE_CANCELLING, REWIND_FLAG_NONE, nullptr, 0U);
        }
        send_odtp_packet(REWIND_TYPE_CLOSE, REWIND_FLAG_NONE, nullptr, 0U);
        m_odtpConfigured = false;
        m_odtpSubscribedTg = 0U;
        m_odtpSubscribedPrivateSelf = false;
        m_linkedGroupTg = 0U;
        return;
    }
    abort_auto_link_stream();
    m_autoLinkState = AUTOLINK_IDLE;
    m_linkedGroupTg = 0U;
    QByteArray out;
    out.append('R');
    out.append('P');
    out.append('T');
    out.append('C');
    out.append('L');
    out.append((m_essid >> 24) & 0xff);
    out.append((m_essid >> 16) & 0xff);
    out.append((m_essid >> 8) & 0xff);
    out.append((m_essid >> 0) & 0xff);
    m_udp->writeDatagram(out, m_address, m_modeinfo.port);

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

void DMR::process_modem_data(QByteArray d)
{
    QByteArray txdata;
    uint8_t lcData[12U];

    uint8_t *p_frame = (uint8_t *)(d.data());
    m_dataType = p_frame[3U] & 0x0f;

    if ((p_frame[3U] & DMR_SYNC_DATA) == DMR_SYNC_DATA){
        if((m_dataType == DT_VOICE_LC_HEADER) && (m_modeinfo.stream_state == STREAM_IDLE)){
            m_modeinfo.stream_state = TRANSMITTING_MODEM;
        }
        else if(m_dataType == DT_TERMINATOR_WITH_LC){
            m_modeinfo.stream_state = STREAM_IDLE;
        }

        m_dmrcnt = 0;
        m_bptc.decode(p_frame + 4, lcData);
        m_txdstid = lcData[3U] << 16 | lcData[4U] << 8 | lcData[5U];
        m_txsrcid = lcData[6U] << 16 | lcData[7U] << 8 | lcData[8U];
        m_flco = FLCO(lcData[0U] & 0x3FU);
        build_frame();
        ::memcpy(m_dmrFrame + 20U, p_frame + 4, 33U);
        txdata.append((char *)m_dmrFrame, 55);
        m_udp->writeDatagram(txdata, m_address, m_modeinfo.port);
    }
    else {
        m_dataType = (m_dmrcnt % 6U) ? DT_VOICE : DT_VOICE_SYNC;
        build_frame();
        ::memcpy(m_dmrFrame + 20U, p_frame + 4, 33U);
        txdata.append((char *)m_dmrFrame, 55);
        m_udp->writeDatagram(txdata, m_address, m_modeinfo.port);
        ++m_dmrcnt;
    }

    if(m_debug){
        QDebug debug = qDebug();
        debug.noquote();
        QString s = "SEND:";
        for(int i = 0; i < txdata.size(); ++i){
            s += " " + QString("%1").arg((uint8_t)txdata.data()[i], 2, 16, QChar('0'));
        }
        debug << s;
    }
}

void DMR::transmit()
{
    if (m_autoLinkState != AUTOLINK_IDLE) {
        abort_auto_link_stream();
        if (m_linkTimer) m_linkTimer->stop();
        m_autoLinkState = AUTOLINK_IDLE;
    }

    // When stop_tx() sets m_tx = false, immediately send DT_TERMINATOR_WITH_LC on the
    if (!m_tx) {
        send_frame();
        return;
    }

    uint8_t ambe[72] = {0}; // never key uninitialised stack bytes on air if the vocoder is not loaded
    int16_t pcm[160];

#ifdef USE_FLITE
    if(m_ttsid > 0){
        for(int i = 0; i < 160; ++i){
            if(m_ttscnt >= tts_audio->num_samples/2){
                pcm[i] = 0;
            }
            else{
                pcm[i] = tts_audio->samples[m_ttscnt*2] / 8;
                m_ttscnt++;
            }
        }
    }
#endif
    if(m_ttsid == 0){
        if (m_autoTxPunch.load(std::memory_order_relaxed)) {
            ::memset(pcm, 0, sizeof(pcm));
        } else if (m_audio && m_audio->read(pcm, 160)) {
        } else {
            return;
        }
    }

    if(m_hwtx){
#if !defined(Q_OS_IOS)
        m_ambedev->encode(pcm);
#endif
    }
    else{
        if(m_modeinfo.sw_vocoder_loaded){
#ifdef USE_MD380_VOCODER
            md380_encode_fec(ambe, pcm);
#else
            m_mbevocoder->encode_2450x1150(pcm, ambe);
#endif
        }
        for(int i = 0; i < 9; ++i){
            m_txcodecq.append(ambe[i]);
        }
    }

    if(m_tx && (m_txcodecq.size() >= 27)){
        for(int i = 0; i < 27; ++i){
            m_ambe[i] = m_txcodecq.dequeue();
        }
        send_frame();
    }
}

void DMR::send_frame()
{
    QByteArray txdata;

    m_txsrcid = m_dmrid;
    if (m_odtp) {
        if (m_tx) {
            if (m_txdstid == 4000U) {
                update_odtp_subscription(true);
                m_tx = false;
            }
        }
        if (m_tx) {
            const bool isPrivCall = (m_flco == FLCO_USER_USER) || (m_txdstid == 9990U);
            m_modeinfo.stream_state = TRANSMITTING;
            m_modeinfo.slot = m_txslot | (isPrivCall ? 0x40 : 0x00);
            m_modeinfo.srcid = m_dmrid;
            m_modeinfo.dstid = m_txdstid;
            m_modeinfo.gwid = m_dmrid;

            if (!m_dmrcnt) {
                if (!isPrivCall && m_txdstid > 0U && m_odtpSubscribedTg != m_txdstid) {
                    update_odtp_subscription(false);
                }

                uint8_t lcData[12U];
                uint8_t parity[4U];
                ::memset(lcData, 0, sizeof(lcData));
                lcData[0U] = isPrivCall ? static_cast<uint8_t>(FLCO_USER_USER) : static_cast<uint8_t>(FLCO_GROUP);
                lcData[1U] = 0U;
                lcData[2U] = 0U;
                lcData[3U] = (m_txdstid >> 16) & 0xFFU;
                lcData[4U] = (m_txdstid >> 8)  & 0xFFU;
                lcData[5U] = (m_txdstid >> 0)  & 0xFFU;
                lcData[6U] = (m_dmrid >> 16)   & 0xFFU;
                lcData[7U] = (m_dmrid >> 8)    & 0xFFU;
                lcData[8U] = (m_dmrid >> 0)    & 0xFFU;
                CRS129::encode(lcData, 9U, parity);
                lcData[9U]  = parity[2U] ^ VOICE_LC_HEADER_CRC_MASK[0U];
                lcData[10U] = parity[1U] ^ VOICE_LC_HEADER_CRC_MASK[1U];
                lcData[11U] = parity[0U] ^ VOICE_LC_HEADER_CRC_MASK[2U];

                // Send 3x Voice Header with the same real-time sequence number per ODTP spec
                send_odtp_packet(REWIND_TYPE_DMR_VOICE_HEADER, REWIND_FLAG_REAL_TIME_1, lcData, sizeof(lcData), false);
                send_odtp_packet(REWIND_TYPE_DMR_VOICE_HEADER, REWIND_FLAG_REAL_TIME_1, lcData, sizeof(lcData), false);
                send_odtp_packet(REWIND_TYPE_DMR_VOICE_HEADER, REWIND_FLAG_REAL_TIME_1, lcData, sizeof(lcData), true);
                ++m_dmrcnt;
            }

            // Send 3x 9-byte Mode-33 AMBE frames (27 bytes total)
            send_odtp_packet(REWIND_TYPE_DMR_AUDIO_FRAME, REWIND_FLAG_REAL_TIME_1, m_ambe, 27U, true);
            m_modeinfo.frame_number = m_dmrcnt;
            ++m_dmrcnt;
        } else {
            if (m_dmrcnt > 0U) {
                send_odtp_packet(REWIND_TYPE_DMR_TERMINATOR_WITH_LC, REWIND_FLAG_REAL_TIME_1, nullptr, 0U, false);
                send_odtp_packet(REWIND_TYPE_DMR_TERMINATOR_WITH_LC, REWIND_FLAG_REAL_TIME_1, nullptr, 0U, true);
            }
            m_dmrcnt = 0;
            m_ttscnt = 0;
            if (m_txtimer) {
                m_txtimer->stop();
            }

            if (m_ttsid == 0 && m_audio && !m_autoTxPunch.load(std::memory_order_relaxed)) {
                m_audio->stop_capture();
            }
            m_autoTxPunch.store(false, std::memory_order_relaxed);

            m_modeinfo.streamid = 0;
            m_modeinfo.srcid = 0;
            m_modeinfo.dstid = 0;
            m_modeinfo.src.clear();
            m_modeinfo.frame_number = 0;
            m_modeinfo.stream_state = STREAM_IDLE;

            update_odtp_subscription(false);
        }

        emit update_output_level(m_audio ? (m_audio->level() * 8) : 0);
        emit update(m_modeinfo);
        return;
    }

    if(m_tx){
        m_modeinfo.stream_state = TRANSMITTING;
        m_modeinfo.slot = m_txslot | ((m_flco == FLCO_USER_USER) ? 0x40 : 0x00);

        if(!m_dmrcnt){
            if (m_flco == FLCO_GROUP) {
                if (m_txdstid == 4000U) {
                    m_linkedGroupTg = 0U;
                } else if (m_txdstid > 0U) {
                    m_linkedGroupTg = m_txdstid;
                }
            }

            m_txstreamid = static_cast<uint32_t>(::rand());
            encode_header(DT_VOICE_LC_HEADER);
            build_frame();
            txdata.append((char *)m_dmrFrame, 55);
            // Send Voice LC Header 3 times per MMDVM/DMR standard for reliable call setup
            m_udp->writeDatagram(txdata, m_address, m_modeinfo.port);
            m_udp->writeDatagram(txdata, m_address, m_modeinfo.port);
            m_udp->writeDatagram(txdata, m_address, m_modeinfo.port);
            ++m_dmrcnt;
            txdata.clear();
        }

        ::memcpy(m_dmrFrame + 20U, m_ambe, 13U);
        m_dmrFrame[33U] = m_ambe[13U] & 0xF0U;
        m_dmrFrame[39U] = m_ambe[13U] & 0x0FU;
        ::memcpy(m_dmrFrame + 40U, &m_ambe[14U], 13U);
        encode_data();

        build_frame();
        txdata.append((char *)m_dmrFrame, 55);
        m_udp->writeDatagram(txdata, m_address, m_modeinfo.port);
        ++m_dmrcnt;
    }
    else{
        encode_header(DT_TERMINATOR_WITH_LC);
        build_frame();
        m_dmrcnt = 0;
        m_ttscnt = 0;
        txdata.append((char *)m_dmrFrame, 55);
        m_udp->writeDatagram(txdata, m_address, m_modeinfo.port);
        m_udp->writeDatagram(txdata, m_address, m_modeinfo.port);
        m_txtimer->stop();

        if(m_ttsid == 0 && m_audio && !m_autoTxPunch.load(std::memory_order_relaxed)){
            m_audio->stop_capture();
        }
        m_autoTxPunch.store(false, std::memory_order_relaxed);

        m_modeinfo.streamid = 0;
        m_modeinfo.srcid = 0;
        m_modeinfo.dstid = 0;
        m_modeinfo.frame_number = 0;
        m_modeinfo.stream_state = STREAM_IDLE;
    }
    emit update_output_level(m_audio ? (m_audio->level() * 8) : 0);
    emit update(m_modeinfo);

    if(m_debug){
        QDebug debug = qDebug();
        debug.noquote();
        QString s = "SEND:";
        for(int i = 0; i < txdata.size(); ++i){
            s += " " + QString("%1").arg((uint8_t)txdata.data()[i], 2, 16, QChar('0'));
        }
        debug << s;
    }
}

uint8_t * DMR::get_eot()
{
    encode_header(DT_TERMINATOR_WITH_LC);
    build_frame();
    m_dmrcnt = 0;
    return m_dmrFrame;
}

void DMR::build_frame()
{
    //qDebug() << "DMR: slot:cc:flco == " << m_txslot << ":" << m_txcc << ":" << m_flco;
    m_dmrFrame[0U]  = 'D';
    m_dmrFrame[1U]  = 'M';
    m_dmrFrame[2U]  = 'R';
    m_dmrFrame[3U]  = 'D';

    m_dmrFrame[5U]  = m_txsrcid >> 16;
    m_dmrFrame[6U]  = m_txsrcid >> 8;
    m_dmrFrame[7U]  = m_txsrcid >> 0;
    m_dmrFrame[8U]  = m_txdstid >> 16;
    m_dmrFrame[9U]  = m_txdstid >> 8;
    m_dmrFrame[10U] = m_txdstid >> 0;
    m_dmrFrame[11U]  = m_essid >> 24;
    m_dmrFrame[12U]  = m_essid >> 16;
    m_dmrFrame[13U]  = m_essid >> 8;
    m_dmrFrame[14U]  = m_essid >> 0;

    m_dmrFrame[15U] = (m_txslot == 1U) ? 0x00U : 0x80U;
    m_dmrFrame[15U] |= (m_flco == FLCO_GROUP) ? 0x00U : 0x40U;

    if (m_dataType == DT_VOICE_SYNC) {
        m_dmrFrame[15U] |= 0x10U;
    } else if (m_dataType == DT_VOICE) {
        m_dmrFrame[15U] |= ((m_dmrcnt - 1) % 6U);
    } else {
        m_dmrFrame[15U] |= (0x20U | m_dataType);
    }

    m_dmrFrame[4U] = m_dmrcnt;
    ::memcpy(m_dmrFrame + 16U, &m_txstreamid, 4U);

    m_dmrFrame[53U] = 0; //data.getBER();
    m_dmrFrame[54U] = 0; //data.getRSSI();

    m_modeinfo.srcid = m_txsrcid;
    m_modeinfo.dstid = m_txdstid;
    m_modeinfo.gwid = m_essid;
    m_modeinfo.frame_number = m_dmrcnt;
}

void DMR::encode_header(uint8_t t)
{
    addDMRDataSync(m_dmrFrame+20, 0);
    m_dataType = t;
    full_lc_encode(m_dmrFrame+20, t);
}

void DMR::encode_data()
{
    uint32_t n_dmr = (m_dmrcnt - 1) % 6U;

    if (!n_dmr) {
        m_dataType = DT_VOICE_SYNC;
        addDMRAudioSync(m_dmrFrame+20, 0);
        encode_embedded_data();
    }
    else {
        m_dataType = DT_VOICE;
        uint8_t lcss = get_embedded_data(m_dmrFrame+20, n_dmr);
        get_emb_data(m_dmrFrame+20, lcss);
    }
}

void DMR::encode16114(bool* d)
{
    d[11] = d[0] ^ d[1] ^ d[2] ^ d[3] ^ d[5] ^ d[7] ^ d[8];
    d[12] = d[1] ^ d[2] ^ d[3] ^ d[4] ^ d[6] ^ d[8] ^ d[9];
    d[13] = d[2] ^ d[3] ^ d[4] ^ d[5] ^ d[7] ^ d[9] ^ d[10];
    d[14] = d[0] ^ d[1] ^ d[2] ^ d[4] ^ d[6] ^ d[7] ^ d[10];
    d[15] = d[0] ^ d[2] ^ d[5] ^ d[6] ^ d[8] ^ d[9] ^ d[10];
}

void DMR::encode_qr1676(uint8_t* data)
{
    uint32_t value = (data[0U] >> 1) & 0x7FU;
    uint32_t cksum = ENCODING_TABLE_1676[value];

    data[0U] = cksum >> 8;
    data[1U] = cksum & 0xFFU;
}

void DMR::get_emb_data(uint8_t* data, uint8_t lcss)
{
    uint8_t DMREMB[2U];
    DMREMB[0U]  = (m_txcc << 4) & 0xF0U;
    //DMREMB[0U] |= m_PI ? 0x08U : 0x00U;
    DMREMB[0U] |= (lcss << 1) & 0x06U;
    DMREMB[1U]  = 0x00U;

    encode_qr1676(DMREMB);

    data[13U] = (data[13U] & 0xF0U) | ((DMREMB[0U] >> 4U) & 0x0FU);
    data[14U] = (data[14U] & 0x0FU) | ((DMREMB[0U] << 4U) & 0xF0U);
    data[18U] = (data[18U] & 0xF0U) | ((DMREMB[1U] >> 4U) & 0x0FU);
    data[19U] = (data[19U] & 0x0FU) | ((DMREMB[1U] << 4U) & 0xF0U);
}

uint8_t DMR::get_embedded_data(uint8_t* data, uint8_t n)
{
    if (n >= 1U && n < 5U) {
        n--;

        bool bits[40U];
        ::memset(bits, 0x00U, 40U * sizeof(bool));
        ::memcpy(bits + 4U, m_raw + n * 32U, 32U * sizeof(bool));

        uint8_t bytes[5U];
        bitsToByteBE(bits + 0U,  bytes[0U]);
        bitsToByteBE(bits + 8U,  bytes[1U]);
        bitsToByteBE(bits + 16U, bytes[2U]);
        bitsToByteBE(bits + 24U, bytes[3U]);
        bitsToByteBE(bits + 32U, bytes[4U]);

        data[14U] = (data[14U] & 0xF0U) | (bytes[0U] & 0x0FU);
        data[15U] = bytes[1U];
        data[16U] = bytes[2U];
        data[17U] = bytes[3U];
        data[18U] = (data[18U] & 0x0FU) | (bytes[4U] & 0xF0U);

        switch (n) {
        case 0U:
            return 1U;
        case 3U:
            return 2U;
        default:
            return 3U;
        }
    } else {
        data[14U] &= 0xF0U;
        data[15U]  = 0x00U;
        data[16U]  = 0x00U;
        data[17U]  = 0x00U;
        data[18U] &= 0x0FU;

        return 0U;
    }
}

void DMR::encode_embedded_data()
{
    uint32_t crc;
    lc_get_data(m_data);
    CCRC::encodeFiveBit(m_data, crc);

    bool data[128U];
    ::memset(data, 0x00U, 128U * sizeof(bool));

    data[106U] = (crc & 0x01U) == 0x01U;
    data[90U]  = (crc & 0x02U) == 0x02U;
    data[74U]  = (crc & 0x04U) == 0x04U;
    data[58U]  = (crc & 0x08U) == 0x08U;
    data[42U]  = (crc & 0x10U) == 0x10U;

    uint32_t b = 0U;
    for (uint32_t a = 0U; a < 11U; a++, b++)
        data[a] = m_data[b];
    for (uint32_t a = 16U; a < 27U; a++, b++)
        data[a] = m_data[b];
    for (uint32_t a = 32U; a < 42U; a++, b++)
        data[a] = m_data[b];
    for (uint32_t a = 48U; a < 58U; a++, b++)
        data[a] = m_data[b];
    for (uint32_t a = 64U; a < 74U; a++, b++)
        data[a] = m_data[b];
    for (uint32_t a = 80U; a < 90U; a++, b++)
        data[a] = m_data[b];
    for (uint32_t a = 96U; a < 106U; a++, b++)
        data[a] = m_data[b];

    // Hamming (16,11,4) check each row except the last one
    for (uint32_t a = 0U; a < 112U; a += 16U)
        encode16114(data + a);

    // Add the parity bits for each column
    for (uint32_t a = 0U; a < 16U; a++)
        data[a + 112U] = data[a + 0U] ^ data[a + 16U] ^ data[a + 32U] ^ data[a + 48U] ^ data[a + 64U] ^ data[a + 80U] ^ data[a + 96U];

    // The data is packed downwards in columns
    b = 0U;
    for (uint32_t a = 0U; a < 128U; a++) {
        m_raw[a] = data[b];
        b += 16U;
        if (b > 127U)
            b -= 127U;
    }
}

void DMR::bitsToByteBE(const bool* bits, uint8_t& byte)
{
    byte  = bits[0U] ? 0x80U : 0x00U;
    byte |= bits[1U] ? 0x40U : 0x00U;
    byte |= bits[2U] ? 0x20U : 0x00U;
    byte |= bits[3U] ? 0x10U : 0x00U;
    byte |= bits[4U] ? 0x08U : 0x00U;
    byte |= bits[5U] ? 0x04U : 0x00U;
    byte |= bits[6U] ? 0x02U : 0x00U;
    byte |= bits[7U] ? 0x01U : 0x00U;
}

void DMR::byteToBitsBE(uint8_t byte, bool* bits)
{
    bits[0U] = (byte & 0x80U) == 0x80U;
    bits[1U] = (byte & 0x40U) == 0x40U;
    bits[2U] = (byte & 0x20U) == 0x20U;
    bits[3U] = (byte & 0x10U) == 0x10U;
    bits[4U] = (byte & 0x08U) == 0x08U;
    bits[5U] = (byte & 0x04U) == 0x04U;
    bits[6U] = (byte & 0x02U) == 0x02U;
    bits[7U] = (byte & 0x01U) == 0x01U;
}

void DMR::lc_get_data(bool* bits)
{
    uint8_t bytes[9U];
    memset(bytes, 0, 9);
    lc_get_data(bytes);

    byteToBitsBE(bytes[0U], bits + 0U);
    byteToBitsBE(bytes[1U], bits + 8U);
    byteToBitsBE(bytes[2U], bits + 16U);
    byteToBitsBE(bytes[3U], bits + 24U);
    byteToBitsBE(bytes[4U], bits + 32U);
    byteToBitsBE(bytes[5U], bits + 40U);
    byteToBitsBE(bytes[6U], bits + 48U);
    byteToBitsBE(bytes[7U], bits + 56U);
    byteToBitsBE(bytes[8U], bits + 64U);
}

void DMR::lc_get_data(uint8_t *bytes)
{
    bool pf, r;
    uint8_t fid, options;
    bytes[0U] = (uint8_t)m_flco;

    pf = (bytes[0U] & 0x80U) == 0x80U;
    r  = (bytes[0U] & 0x40U) == 0x40U;
    //m_flco = FLCO(bytes[0U] & 0x3FU);
    fid = bytes[1U];
    options = bytes[2U];

    //bytes[0U] = (uint8_t)m_flco;

    if (pf)
        bytes[0U] |= 0x80U;

    if (r)
        bytes[0U] |= 0x40U;

    bytes[1U] = fid;
    bytes[2U] = options;
    bytes[3U] = m_txdstid >> 16;
    bytes[4U] = m_txdstid >> 8;
    bytes[5U] = m_txdstid >> 0;
    bytes[6U] = m_dmrid >> 16;
    bytes[7U] = m_dmrid >> 8;
    bytes[8U] = m_dmrid >> 0;
}

void DMR::full_lc_encode(uint8_t* data, uint8_t type)  // for header
{
    uint8_t lcData[12U];
    uint8_t parity[4U];
    ::memset(lcData, 0, sizeof(lcData));
    lc_get_data(lcData);

    CRS129::encode(lcData, 9U, parity);

    switch (type) {
        case DT_VOICE_LC_HEADER:
            lcData[9U]  = parity[2U] ^ VOICE_LC_HEADER_CRC_MASK[0U];
            lcData[10U] = parity[1U] ^ VOICE_LC_HEADER_CRC_MASK[1U];
            lcData[11U] = parity[0U] ^ VOICE_LC_HEADER_CRC_MASK[2U];
            break;

        case DT_TERMINATOR_WITH_LC:
            lcData[9U]  = parity[2U] ^ TERMINATOR_WITH_LC_CRC_MASK[0U];
            lcData[10U] = parity[1U] ^ TERMINATOR_WITH_LC_CRC_MASK[1U];
            lcData[11U] = parity[0U] ^ TERMINATOR_WITH_LC_CRC_MASK[2U];
            break;

        default:
            return;
    }
    get_slot_data(data);
    m_bptc.encode(lcData, data);
}

void DMR::get_slot_data(uint8_t* data)
{
    uint8_t DMRSlotType[3U];
    DMRSlotType[0U]  = (m_txcc << 4) & 0xF0U;
    DMRSlotType[0U] |= (m_dataType  << 0) & 0x0FU;
    DMRSlotType[1U]  = 0x00U;
    DMRSlotType[2U]  = 0x00U;

    CGolay2087::encode(DMRSlotType);

    data[12U] = (data[12U] & 0xC0U) | ((DMRSlotType[0U] >> 2) & 0x3FU);
    data[13U] = (data[13U] & 0x0FU) | ((DMRSlotType[0U] << 6) & 0xC0U) | ((DMRSlotType[1U] >> 2) & 0x30U);
    data[19U] = (data[19U] & 0xF0U) | ((DMRSlotType[1U] >> 2) & 0x0FU);
    data[20U] = (data[20U] & 0x03U) | ((DMRSlotType[1U] << 6) & 0xC0U) | ((DMRSlotType[2U] >> 2) & 0x3CU);
}


void DMR::addDMRDataSync(uint8_t* data, bool duplex)
{
    if (duplex) {
        for (uint32_t i = 0U; i < 7U; i++)
            data[i + 13U] = (data[i + 13U] & ~SYNC_MASK[i]) | BS_SOURCED_DATA_SYNC[i];
    } else {
        for (uint32_t i = 0U; i < 7U; i++)
            data[i + 13U] = (data[i + 13U] & ~SYNC_MASK[i]) | MS_SOURCED_DATA_SYNC[i];
    }
}

void DMR::addDMRAudioSync(uint8_t* data, bool duplex)
{
    if (duplex) {
        for (uint32_t i = 0U; i < 7U; i++)
            data[i + 13U] = (data[i + 13U] & ~SYNC_MASK[i]) | BS_SOURCED_AUDIO_SYNC[i];
    } else {
        for (uint32_t i = 0U; i < 7U; i++)
            data[i + 13U] = (data[i + 13U] & ~SYNC_MASK[i]) | MS_SOURCED_AUDIO_SYNC[i];
    }
}

void DMR::get_ambe()
{
#if !defined(Q_OS_IOS)
    uint8_t ambe[9];

    if(m_ambedev->get_ambe(ambe)){
        for(int i = 0; i < 9; ++i){
            m_txcodecq.append(ambe[i]);
        }
    }
#endif
}

void DMR::process_rx_data()
{
    int16_t pcm[160];
    uint8_t ambe[9];
    static uint8_t cnt = 0;

    if(m_rxwatchdog++ > 100){
        qDebug() << "DMR RX stream timeout ";
        m_rxwatchdog = 0;
        m_modeinfo.stream_state = STREAM_LOST;
        m_modeinfo.ts = QDateTime::currentMSecsSinceEpoch();
        emit update(m_modeinfo);
        m_modeinfo.streamid = 0;
    }

    if((m_rxmodemq.size() > 2) && (++cnt >= 3)){
        QByteArray out;
        int s = m_rxmodemq[1];
        if((m_rxmodemq[0] == MMDVM_FRAME_START) && (m_rxmodemq.size() >= s)){
            for(int i = 0; i < s; ++i){
                out.append(m_rxmodemq.dequeue());
            }
#if !defined(Q_OS_IOS)
            m_modem->write(out);
#endif
        }
        cnt = 0;
    }

    if((!m_tx) && rxJitterReady(9)){
        m_rxConcealRun = 0;
        for(int i = 0; i < 9; ++i){
            ambe[i] = m_rxcodecq.dequeue();
        }
        if(m_hwrx){
#if !defined(Q_OS_IOS)
            m_ambedev->decode(ambe);

            if(m_ambedev->get_audio(pcm)){
                m_audio->write(pcm, 160);
                emit update_output_level(m_audio->level());
            }
#endif
        }
        else{
            if(m_modeinfo.sw_vocoder_loaded){
#ifdef USE_MD380_VOCODER
                md380_decode_fec(ambe, pcm);
#else
                m_mbevocoder->decode_2450x1150(pcm, ambe);
#endif
            }
            else{
                memset(pcm, 0, 160 * sizeof(int16_t));
            }
            m_audio->write(pcm, 160);
            emit update_output_level(m_audio->level());
        }
    }
    else if((!m_tx) && !m_hwrx && m_modeinfo.sw_vocoder_loaded && rxShouldConceal()){
        // Late/missing network frame: keep the speaker fed with a vocoder lost-frame
#ifdef USE_MD380_VOCODER
        memset(pcm, 0, 160 * sizeof(int16_t));
#else
        m_mbevocoder->conceal_2450x1150(pcm);
#endif
        m_audio->write(pcm, 160);
        emit update_output_level(m_audio->level());
    }
    else if ( ((m_modeinfo.stream_state == STREAM_END) || (m_modeinfo.stream_state == STREAM_LOST) || (m_modeinfo.stream_state == STREAM_IDLE)) && (m_rxmodemq.size() < 50) ){
        m_rxtimer->stop();
        m_audio->stop_playback();
        m_rxwatchdog = 0;
        m_modeinfo.streamid = 0;
        m_rxcodecq.clear();
        rxJitterReset();
        qDebug() << "DMR playback stopped";
        m_modeinfo.stream_state = STREAM_IDLE;
        emit update_output_level(0);
        emit update(m_modeinfo);
        return;
    }
}
