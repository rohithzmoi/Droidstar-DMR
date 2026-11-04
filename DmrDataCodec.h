/*
    Copyright (C) 2026 Rohith Namboothiri

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

#ifndef DMRDATACODEC_H
#define DMRDATACODEC_H

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>
#include <cstdint>
#include <cstddef>

class DmrDataCodec
{
public:
    enum class SmsFormat : int {
        MotorolaTms      = 0, // UDP port 4007 + Motorola TMS header + UTF-16BE (Default, M-SMS)
        DmrStandardLe    = 1, // UDP port 5016 + 000D000A + UTF-16LE (AnyTone "DMR Standard")
        EtsiStandardBe   = 2, // UDP port 5016 + 000D000A + UTF-16BE (ETSI TS 102 361-3)
        DefinedShortData = 3  // DPF_DEFINED_SHORT (0x0D), SAP 0x0A (Hytera / Chinese Radio)
    };

    struct DecodedSms {
        bool valid { false };
        bool isAck { false };
        bool ackSuccess { true };
        uint32_t srcId { 0 };
        uint32_t dstId { 0 };
        uint8_t slot { 2 };
        bool isGroup { false };
        QString text;
        QString formatName;
        QVector<QByteArray> autoAckDmrdFrames; // Populated when Confirmed Data requires an automatic DPF_RESPONSE
    };

    static QString formatDisplayName(SmsFormat fmt);

    // ── BrandMeister ODTP (REWIND01) Terminal Text Message Framing ──────────
    static QByteArray encodeOdtpTextPayload(uint32_t srcId,
                                            uint32_t dstId,
                                            bool isGroup,
                                            const QString &text);

    static bool decodeOdtpTextPayload(const uint8_t *payload,
                                      uint16_t length,
                                      DecodedSms &out);

    static QByteArray encodeOdtpStatusPayload(uint32_t sourceId,
                                              uint32_t destinationId,
                                              uint8_t status = 0x08U);

    static bool decodeOdtpStatusPayload(const uint8_t *payload,
                                        uint16_t length,
                                        uint32_t &outSrcId,
                                        uint32_t &outDstId,
                                        uint8_t &outStatus);

    // ── Stateless HomeBrew (DMRD) Air-Interface Burst Framing ───────────────
    static void encodeSlotTypeAndSync(uint8_t frame33[33],
                                      uint8_t colorCode,
                                      uint8_t dataType,
                                      bool bsSourced = false);

    static QByteArray buildDmrdFrame(uint8_t seqNo,
                                     uint32_t srcId,
                                     uint32_t dstId,
                                     uint32_t essid,
                                     uint8_t slot,      // 1 or 2
                                     uint8_t colorCode, // 0..15
                                     bool isGroup,
                                     uint8_t dataType,  // DT_CSBK, DT_DATA_HEADER, DT_RATE_12_DATA, etc.
                                     uint32_t streamId,
                                     const uint8_t frame33[33]);

    static QByteArray buildBptcDmrdFrame(uint8_t seqNo,
                                         uint32_t srcId,
                                         uint32_t dstId,
                                         uint32_t essid,
                                         uint8_t slot,
                                         uint8_t colorCode,
                                         bool isGroup,
                                         uint8_t dataType,
                                         uint32_t streamId,
                                         const uint8_t block12[12]);

    // Encodes a complete SMS into a sequence of 55-byte HomeBrew DMRD UDP datagrams
    static QVector<QByteArray> encodeSmsSequence(uint32_t srcId,
                                                 uint32_t dstId,
                                                 uint32_t essid,
                                                 uint8_t slot,
                                                 uint8_t colorCode,
                                                 bool isGroup,
                                                 const QString &text,
                                                 SmsFormat format = SmsFormat::MotorolaTms,
                                                 bool includeCsbkPreamble = true);

    // Builds an automatic DPF_RESPONSE + Terminator sequence for Confirmed Data RX
    static QVector<QByteArray> buildConfirmedDataAckSequence(uint32_t mySrcId,
                                                             uint32_t peerDstId,
                                                             uint32_t essid,
                                                             uint8_t slot,
                                                             uint8_t colorCode,
                                                             uint8_t sap,
                                                             uint8_t rxSeqNo);

    // ── FEC & Checksums (ETSI TS 102 361-1 / -3) ────────────────────────────
    static bool trellisDecode(const uint8_t frame33[33], uint8_t out18[18]);
    static void trellisEncode(const uint8_t in18[18], uint8_t frame33[33]);

    static uint32_t computeDmrCrc32(const uint8_t *data, size_t len);
    static bool verifyDmrCrc32(const uint8_t *data, size_t totalLen);
    static bool verifyDmrCrc9(const uint8_t block18[18], uint8_t &outDbsn);
    static uint16_t computeIpChecksum(const uint8_t *data, size_t offset, size_t len);
};

class DmrSmsAssembler
{
public:
    DmrSmsAssembler() = default;

    void reset();

    DmrDataCodec::DecodedSms processBurst(uint32_t pktSrcId,
                                          uint32_t pktDstId,
                                          uint8_t slot,
                                          bool pktIsPrivate,
                                          uint8_t dataType,
                                          const uint8_t frame33[33],
                                          uint32_t myDmrId,
                                          uint32_t myEssid,
                                          uint8_t myColorCode);

private:
    struct AssemblyState {
        uint32_t srcId { 0 };
        uint32_t dstId { 0 };
        uint8_t slot { 2 };
        bool isGroup { false };
        bool responseRequested { false };
        uint8_t dpf { 0 };
        uint8_t sap { 0 };
        uint8_t padOctets { 0 };
        uint8_t ddFormat { 0 };
        uint8_t udtFormat { 0 };
        uint8_t udtOpcode { 0 };
        uint8_t seqNo { 0 };
        int expectedBlocks { 0 };
        int receivedBlocks { 0 };
        uint64_t seenDbsnLo { 0 };
        uint64_t seenDbsnHi { 0 };
        QByteArray buffer;
        qint64 lastActivityMs { 0 };
    };

    QHash<uint64_t, AssemblyState> m_states;
    uint8_t m_lastDupBurst[33] {};
    uint32_t m_lastDupSrcId { 0 };
    qint64 m_lastDupTimeMs { 0 };

    void purgeStale(qint64 nowMs);
    static uint64_t makeKey(uint8_t slot, uint32_t srcId)
    {
        return (static_cast<uint64_t>(slot) << 32) | static_cast<uint64_t>(srcId);
    }

    static bool finalizeMessage(const AssemblyState &st,
                                uint32_t myDmrId,
                                uint32_t myEssid,
                                uint8_t myColorCode,
                                DmrDataCodec::DecodedSms &out);
};

#endif // DMRDATACODEC_H
