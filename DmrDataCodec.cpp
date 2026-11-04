/*
    Copyright (C) 2026 Rohith Namboothiri

    DMR packet-data and text-message codec.  This file intentionally has no
    dependency on Mode, AudioEngine, or UI state so data calls cannot disturb
    the voice path.
*/

#include "DmrDataCodec.h"

#include "CRCenc.h"
#include "DMRDefines.h"
#include "cbptc19696.h"
#include "cgolay2087.h"
#include "crs129.h"

#include <QDateTime>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstring>

namespace {

constexpr uint32_t CRC32_POLY = 0x04C11DB7U;
constexpr uint16_t CRC9_POLY  = 0x0059U;
constexpr uint16_t CRC9_MASK  = 0x00F0U;

constexpr std::array<unsigned int, 98> TRELLIS_INTERLEAVE = {
    0,1,8,9,16,17,24,25,32,33,40,41,48,49,56,57,64,65,72,73,80,81,88,89,96,97,
    2,3,10,11,18,19,26,27,34,35,42,43,50,51,58,59,66,67,74,75,82,83,90,91,
    4,5,12,13,20,21,28,29,36,37,44,45,52,53,60,61,68,69,76,77,84,85,92,93,
    6,7,14,15,22,23,30,31,38,39,46,47,54,55,62,63,70,71,78,79,86,87,94,95
};

constexpr std::array<uint8_t, 64> TRELLIS_ENCODE = {
    0,8,4,12,2,10,6,14, 4,12,2,10,6,14,0,8,
    1,9,5,13,3,11,7,15, 5,13,3,11,7,15,1,9,
    3,11,7,15,1,9,5,13, 7,15,1,9,5,13,3,11,
    2,10,6,14,0,8,4,12, 6,14,0,8,4,12,2,10
};

constexpr std::array<std::array<int8_t, 2>, 16> POINT_DIBITS = {{
    {{+1,-1}}, {{-1,-1}}, {{+3,-3}}, {{-3,-3}},
    {{-3,-1}}, {{+3,-1}}, {{-1,-3}}, {{+1,-3}},
    {{-3,+3}}, {{+3,+3}}, {{-1,+1}}, {{+1,+1}},
    {{+1,+3}}, {{-1,+3}}, {{+3,+1}}, {{-3,+1}}
}};

inline bool readBit(const uint8_t *p, unsigned int bit)
{
    return (p[bit >> 3U] & (0x80U >> (bit & 7U))) != 0U;
}

inline void writeBit(uint8_t *p, unsigned int bit, bool value)
{
    const uint8_t mask = static_cast<uint8_t>(0x80U >> (bit & 7U));
    if (value)
        p[bit >> 3U] |= mask;
    else
        p[bit >> 3U] &= static_cast<uint8_t>(~mask);
}

inline uint32_t readLe32(const uint8_t *p)
{
    return qFromLittleEndian<uint32_t>(p);
}

inline uint16_t readLe16(const uint8_t *p)
{
    return qFromLittleEndian<uint16_t>(p);
}

inline void appendLe16(QByteArray &out, uint16_t value)
{
    const uint16_t le = qToLittleEndian(value);
    out.append(reinterpret_cast<const char *>(&le), 2);
}

inline void appendLe32(QByteArray &out, uint32_t value)
{
    const uint32_t le = qToLittleEndian(value);
    out.append(reinterpret_cast<const char *>(&le), 4);
}

QByteArray encodeUtf16(const QString &text, bool littleEndian, bool appendNull = false)
{
    QByteArray out;
    out.reserve((text.size() + (appendNull ? 1 : 0)) * 2);
    for (QChar ch : text) {
        const uint16_t u = ch.unicode();
        if (littleEndian) {
            out.append(static_cast<char>(u & 0xFFU));
            out.append(static_cast<char>((u >> 8U) & 0xFFU));
        } else {
            out.append(static_cast<char>((u >> 8U) & 0xFFU));
            out.append(static_cast<char>(u & 0xFFU));
        }
    }
    if (appendNull) {
        out.append('\0');
        out.append('\0');
    }
    return out;
}

QString decodeUtf16(const uint8_t *data, int length, bool littleEndian)
{
    QString out;
    if (!data || length < 2)
        return out;
    out.reserve(length / 2);
    for (int i = 0; i + 1 < length; i += 2) {
        const uint16_t u = littleEndian
            ? static_cast<uint16_t>(data[i] | (static_cast<uint16_t>(data[i + 1]) << 8U))
            : static_cast<uint16_t>((static_cast<uint16_t>(data[i]) << 8U) | data[i + 1]);
        if (u == 0U)
            break;
        if (u == 0xFEFFU || u == 0xFFFEU)
            continue;
        if (u == 0x000DU || u == 0x000AU || u == 0x040DU) {
            if (!out.isEmpty() && !out.endsWith(QLatin1Char('\n')))
                out.append(QLatin1Char('\n'));
        } else {
            out.append(QChar(u));
        }
    }
    return out.trimmed();
}

bool likelyLittleEndianUtf16(const uint8_t *data, int length)
{
    int leScore = 0;
    int beScore = 0;
    for (int i = 0; i + 1 < std::min(length, 24); i += 2) {
        if (data[i] != 0U && data[i + 1] == 0U) ++leScore;
        if (data[i] == 0U && data[i + 1] != 0U) ++beScore;
    }
    return leScore > beScore;
}

QByteArray buildIpUdpPdu(uint32_t srcId, uint32_t dstId, bool isGroup,
                         uint16_t port, const QByteArray &appPayload, uint16_t ipId)
{
    QByteArray pdu(28 + appPayload.size(), '\0');
    auto *p = reinterpret_cast<uint8_t *>(pdu.data());
    const uint16_t totalLen = static_cast<uint16_t>(pdu.size());
    const uint16_t udpLen = static_cast<uint16_t>(8 + appPayload.size());

    p[0] = 0x45U;
    p[2] = static_cast<uint8_t>(totalLen >> 8U);
    p[3] = static_cast<uint8_t>(totalLen);
    p[4] = static_cast<uint8_t>(ipId >> 8U);
    p[5] = static_cast<uint8_t>(ipId);
    p[8] = 0x01U;
    p[9] = 0x11U;
    p[12] = 0x0CU;
    p[13] = static_cast<uint8_t>(srcId >> 16U);
    p[14] = static_cast<uint8_t>(srcId >> 8U);
    p[15] = static_cast<uint8_t>(srcId);
    p[16] = isGroup ? 0xE1U : 0x0CU;
    p[17] = static_cast<uint8_t>(dstId >> 16U);
    p[18] = static_cast<uint8_t>(dstId >> 8U);
    p[19] = static_cast<uint8_t>(dstId);
    p[20] = static_cast<uint8_t>(port >> 8U);
    p[21] = static_cast<uint8_t>(port);
    p[22] = static_cast<uint8_t>(port >> 8U);
    p[23] = static_cast<uint8_t>(port);
    p[24] = static_cast<uint8_t>(udpLen >> 8U);
    p[25] = static_cast<uint8_t>(udpLen);
    if (!appPayload.isEmpty())
        std::memcpy(p + 28, appPayload.constData(), static_cast<size_t>(appPayload.size()));

    const uint16_t ipCsum = DmrDataCodec::computeIpChecksum(p, 0, 20);
    p[10] = static_cast<uint8_t>(ipCsum >> 8U);
    p[11] = static_cast<uint8_t>(ipCsum);

    // UDP checksum over pseudo header + UDP datagram.  A zero result is encoded as FFFF.
    QByteArray pseudo;
    pseudo.reserve(12 + udpLen + 1);
    pseudo.append(reinterpret_cast<const char *>(p + 12), 8);
    pseudo.append('\0');
    pseudo.append(static_cast<char>(0x11));
    pseudo.append(static_cast<char>(udpLen >> 8U));
    pseudo.append(static_cast<char>(udpLen));
    pseudo.append(reinterpret_cast<const char *>(p + 20), udpLen);
    if (pseudo.size() & 1)
        pseudo.append('\0');
    const uint16_t udpCsum = DmrDataCodec::computeIpChecksum(
        reinterpret_cast<const uint8_t *>(pseudo.constData()), 0, static_cast<size_t>(pseudo.size()));
    p[26] = static_cast<uint8_t>((udpCsum ? udpCsum : 0xFFFFU) >> 8U);
    p[27] = static_cast<uint8_t>(udpCsum ? udpCsum : 0xFFFFU);
    return pdu;
}

void addMaskedCrc16(uint8_t block[12], uint8_t mask)
{
    CCRC::addCCITT162(block, 12U);
    block[10] ^= mask;
    block[11] ^= mask;
}

bool checkMaskedCrc16(uint8_t block[12], uint8_t mask)
{
    block[10] ^= mask;
    block[11] ^= mask;
    return CCRC::checkCCITT162(block, 12U);
}

uint16_t crc9Bits(const uint8_t *data, size_t length)
{
    uint16_t crc = 0U;
    for (size_t n = 0; n < length; ++n) {
        const uint8_t c = data[n];
        for (unsigned int i = 0; i < 8U; ++i) {
            const bool top = (crc & 0x100U) != 0U;
            crc = static_cast<uint16_t>((crc << 1U) | ((c >> (7U - i)) & 1U));
            if (top)
                crc ^= CRC9_POLY;
            crc &= 0x1FFU;
        }
    }
    for (unsigned int i = 0; i < 9U; ++i) {
        const bool top = (crc & 0x100U) != 0U;
        crc = static_cast<uint16_t>(crc << 1U);
        if (top)
            crc ^= CRC9_POLY;
        crc &= 0x1FFU;
    }
    return static_cast<uint16_t>((crc ^ 0x1FFU) & 0x1FFU);
}

bool verifyConfirmedBlockCrc9(const uint8_t *block, size_t blockSize, uint8_t &outDbsn)
{
    if (!block || blockSize < 3U || blockSize > 24U)
        return false;
    outDbsn = static_cast<uint8_t>((block[0] >> 1U) & 0x7FU);
    const uint16_t sent = static_cast<uint16_t>(
        ((((static_cast<uint16_t>(block[0]) & 1U) << 8U) | block[1])) ^ CRC9_MASK);
    uint8_t data[23] {};
    std::memcpy(data, block + 2U, blockSize - 2U);
    data[blockSize - 2U] = static_cast<uint8_t>(outDbsn << 1U);
    uint8_t shifted[23] {};
    for (int i = static_cast<int>(blockSize) - 2; i >= 0; --i) {
        const uint8_t rem = i > 0 ? static_cast<uint8_t>(data[i - 1] & 1U) : 0U;
        shifted[i] = static_cast<uint8_t>((data[i] >> 1U) | (rem << 7U));
    }
    return crc9Bits(shifted, blockSize - 1U) == sent;
}

uint32_t crc32Byte(uint32_t crc, uint8_t byte)
{
    crc ^= static_cast<uint32_t>(byte) << 24U;
    for (unsigned int bit = 0; bit < 8U; ++bit)
        crc = (crc & 0x80000000U) ? ((crc << 1U) ^ CRC32_POLY) : (crc << 1U);
    return crc;
}

QByteArray decodeIso7(const uint8_t *data, int byteLen, int outputChars)
{
    QByteArray out;
    out.reserve(outputChars);
    unsigned int bitPos = 0U;
    for (int i = 0; i < outputChars && bitPos + 7U <= static_cast<unsigned int>(byteLen * 8); ++i) {
        uint8_t c = 0U;
        for (unsigned int b = 0; b < 7U; ++b, ++bitPos) {
            c <<= 1U;
            c |= readBit(data, bitPos) ? 1U : 0U;
        }
        if (c == 0U)
            break;
        out.append(static_cast<char>(c));
    }
    return out;
}

} // namespace

QString DmrDataCodec::formatDisplayName(SmsFormat fmt)
{
    switch (fmt) {
    case SmsFormat::MotorolaTms:      return QStringLiteral("M-SMS");
    case SmsFormat::DmrStandardLe:    return QStringLiteral("DMR Standard LE");
    case SmsFormat::EtsiStandardBe:   return QStringLiteral("ETSI UTF-16BE");
    case SmsFormat::DefinedShortData: return QStringLiteral("Short Data");
    }
    return QStringLiteral("DMR SMS");
}

QByteArray DmrDataCodec::encodeOdtpTextPayload(uint32_t srcId, uint32_t dstId,
                                               bool isGroup, const QString &text)
{
    QByteArray encoded = encodeUtf16(text, true, false);
    if (encoded.size() > 0xFFFF)
        encoded.truncate(0xFFFE);
    QByteArray out;
    out.reserve(16 + encoded.size());
    appendLe32(out, 0U);
    appendLe32(out, srcId);
    appendLe32(out, dstId);
    appendLe16(out, isGroup ? 0x0080U : 0x0000U);
    appendLe16(out, static_cast<uint16_t>(encoded.size()));
    out.append(encoded);
    return out;
}

bool DmrDataCodec::decodeOdtpTextPayload(const uint8_t *payload, uint16_t length,
                                         DecodedSms &out)
{
    if (!payload || length < 16U)
        return false;
    const uint16_t textLen = readLe16(payload + 14U);
    if (textLen > length - 16U || (textLen & 1U))
        return false;
    out = DecodedSms{};
    out.valid = true;
    out.srcId = readLe32(payload + 4U);
    out.dstId = readLe32(payload + 8U);
    out.isGroup = (readLe16(payload + 12U) & 0x0080U) != 0U;
    out.text = decodeUtf16(payload + 16U, textLen, true);
    out.formatName = QStringLiteral("ODTP");
    return !out.text.isEmpty();
}

QByteArray DmrDataCodec::encodeOdtpStatusPayload(uint32_t sourceId,
                                                 uint32_t destinationId,
                                                 uint8_t status)
{
    QByteArray out;
    out.reserve(13);
    appendLe32(out, 0U);
    appendLe32(out, sourceId);
    appendLe32(out, destinationId);
    out.append(static_cast<char>(status));
    return out;
}

bool DmrDataCodec::decodeOdtpStatusPayload(const uint8_t *payload, uint16_t length,
                                           uint32_t &outSrcId, uint32_t &outDstId,
                                           uint8_t &outStatus)
{
    if (!payload || length < 13U)
        return false;
    outSrcId = readLe32(payload + 4U);
    outDstId = readLe32(payload + 8U);
    outStatus = payload[12U];
    return true;
}

void DmrDataCodec::encodeSlotTypeAndSync(uint8_t frame33[33], uint8_t colorCode,
                                         uint8_t dataType, bool bsSourced)
{
    uint8_t slotType[3] = {
        static_cast<uint8_t>(((colorCode & 0x0FU) << 4U) | (dataType & 0x0FU)),
        0U, 0U
    };
    CGolay2087::encode(slotType);
    frame33[12] = static_cast<uint8_t>((frame33[12] & 0xC0U) | ((slotType[0] >> 2U) & 0x3FU));
    frame33[13] = static_cast<uint8_t>((frame33[13] & 0x0FU) |
                                      ((slotType[0] << 6U) & 0xC0U) |
                                      ((slotType[1] >> 2U) & 0x30U));
    frame33[19] = static_cast<uint8_t>((frame33[19] & 0xF0U) | ((slotType[1] >> 2U) & 0x0FU));
    frame33[20] = static_cast<uint8_t>((frame33[20] & 0x03U) |
                                      ((slotType[1] << 6U) & 0xC0U) |
                                      ((slotType[2] >> 2U) & 0x3CU));
    const uint8_t *sync = bsSourced ? BS_SOURCED_DATA_SYNC : MS_SOURCED_DATA_SYNC;
    for (unsigned int i = 0; i < 7U; ++i)
        frame33[i + 13U] = static_cast<uint8_t>((frame33[i + 13U] & ~SYNC_MASK[i]) | sync[i]);
}

QByteArray DmrDataCodec::buildDmrdFrame(uint8_t seqNo, uint32_t srcId,
                                        uint32_t dstId, uint32_t essid,
                                        uint8_t slot, uint8_t colorCode,
                                        bool isGroup, uint8_t dataType,
                                        uint32_t streamId,
                                        const uint8_t frame33[33])
{
    Q_UNUSED(colorCode)
    QByteArray out(55, '\0');
    auto *p = reinterpret_cast<uint8_t *>(out.data());
    std::memcpy(p, "DMRD", 4U);
    p[4] = seqNo;
    p[5] = static_cast<uint8_t>(srcId >> 16U);
    p[6] = static_cast<uint8_t>(srcId >> 8U);
    p[7] = static_cast<uint8_t>(srcId);
    p[8] = static_cast<uint8_t>(dstId >> 16U);
    p[9] = static_cast<uint8_t>(dstId >> 8U);
    p[10] = static_cast<uint8_t>(dstId);
    p[11] = static_cast<uint8_t>(essid >> 24U);
    p[12] = static_cast<uint8_t>(essid >> 16U);
    p[13] = static_cast<uint8_t>(essid >> 8U);
    p[14] = static_cast<uint8_t>(essid);
    p[15] = static_cast<uint8_t>((slot == 2U ? 0x80U : 0x00U) |
                                 (isGroup ? 0x00U : 0x40U) |
                                 0x20U | (dataType & 0x0FU));
    // HomeBrew treats Stream ID as an opaque four-byte value.  Use network order
    // so logs and packet captures show the expected numeric value.
    p[16] = static_cast<uint8_t>(streamId >> 24U);
    p[17] = static_cast<uint8_t>(streamId >> 16U);
    p[18] = static_cast<uint8_t>(streamId >> 8U);
    p[19] = static_cast<uint8_t>(streamId);
    std::memcpy(p + 20U, frame33, 33U);
    p[53] = 0U;
    p[54] = 0U;
    return out;
}

QByteArray DmrDataCodec::buildBptcDmrdFrame(uint8_t seqNo, uint32_t srcId,
                                            uint32_t dstId, uint32_t essid,
                                            uint8_t slot, uint8_t colorCode,
                                            bool isGroup, uint8_t dataType,
                                            uint32_t streamId,
                                            const uint8_t block12[12])
{
    uint8_t frame[33] {};
    CBPTC19696 bptc;
    bptc.encode(block12, frame);
    encodeSlotTypeAndSync(frame, colorCode, dataType, false);
    return buildDmrdFrame(seqNo, srcId, dstId, essid, slot, colorCode,
                          isGroup, dataType, streamId, frame);
}

QVector<QByteArray> DmrDataCodec::encodeSmsSequence(uint32_t srcId, uint32_t dstId,
                                                    uint32_t essid, uint8_t slot,
                                                    uint8_t colorCode, bool isGroup,
                                                    const QString &text,
                                                    SmsFormat format,
                                                    bool includeCsbkPreamble)
{
    QVector<QByteArray> result;
    if (srcId == 0U || dstId == 0U || text.isEmpty())
        return result;

    static uint16_t appSequence = 1U;
    static uint32_t streamSequence = 0x44534D00U;
    const uint16_t appSeq = appSequence++;
    const uint32_t streamId = ++streamSequence;

    QByteArray payload;
    uint8_t sap = 0x04U;
    if (format == SmsFormat::MotorolaTms) {
        QByteArray tms;
        const QByteArray encoded = encodeUtf16(text.left(500), false, false);
        const uint16_t tmsBodyLength = static_cast<uint16_t>(8 + encoded.size());
        tms.append(static_cast<char>(tmsBodyLength >> 8U));
        tms.append(static_cast<char>(tmsBodyLength));
        tms.append(static_cast<char>(0xA0U));
        tms.append('\0');
        tms.append(static_cast<char>(0x80U | (appSeq & 0x07U)));
        tms.append(static_cast<char>(0x04U));
        tms.append(static_cast<char>(0x0DU));
        tms.append('\0');
        tms.append(static_cast<char>(0x0AU));
        tms.append(encoded);
        tms.append('\0');
        payload = buildIpUdpPdu(srcId, dstId, isGroup, 4007U, tms, appSeq);
    } else if (format == SmsFormat::DmrStandardLe ||
               format == SmsFormat::EtsiStandardBe) {
        QByteArray sms("\x00\x0D\x00\x0A", 4);
        sms.append(encodeUtf16(text.left(500),
                              format == SmsFormat::DmrStandardLe, false));
        payload = buildIpUdpPdu(srcId, dstId, isGroup, 5016U, sms, appSeq);
    } else {
        sap = 0x0AU;
        payload = encodeUtf16(text.left(500), false, true);
    }

    // Four CRC bytes live at the end of the final 12-byte data block.
    const int blocks = std::max(1, static_cast<int>((payload.size() + 4 + 11) / 12));
    if (blocks > 63)
        return {};
    const int paddedDataLength = blocks * 12 - 4;
    const int padOctets = paddedDataLength - payload.size();
    payload.append(QByteArray(padOctets, '\0'));
    const uint32_t crc32 = computeDmrCrc32(
        reinterpret_cast<const uint8_t *>(payload.constData()),
        static_cast<size_t>(payload.size()));
    payload.append(static_cast<char>(crc32 & 0xFFU));
    payload.append(static_cast<char>((crc32 >> 8U) & 0xFFU));
    payload.append(static_cast<char>((crc32 >> 16U) & 0xFFU));
    payload.append(static_cast<char>((crc32 >> 24U) & 0xFFU));

    uint8_t dmrdSeq = 0U;
    if (includeCsbkPreamble) {
        constexpr int PREAMBLES = 3;
        for (int n = 0; n < PREAMBLES; ++n) {
            uint8_t csbk[12] {};
            csbk[0] = 0xBDU; // LB=1, PF=0, CSBKO=0x3D (Preamble)
            csbk[2] = static_cast<uint8_t>(0x80U | (isGroup ? 0x40U : 0x00U));
            csbk[3] = static_cast<uint8_t>(blocks + PREAMBLES - n);
            csbk[4] = static_cast<uint8_t>(dstId >> 16U);
            csbk[5] = static_cast<uint8_t>(dstId >> 8U);
            csbk[6] = static_cast<uint8_t>(dstId);
            csbk[7] = static_cast<uint8_t>(srcId >> 16U);
            csbk[8] = static_cast<uint8_t>(srcId >> 8U);
            csbk[9] = static_cast<uint8_t>(srcId);
            addMaskedCrc16(csbk, 0xA5U);
            result.append(buildBptcDmrdFrame(dmrdSeq++, srcId, dstId, essid,
                                              slot, colorCode, isGroup,
                                              DT_CSBK, streamId, csbk));
        }
    }

    uint8_t header[12] {};
    if (format == SmsFormat::DefinedShortData) {
        header[0] = static_cast<uint8_t>((isGroup ? 0x80U : 0x00U) |
                                         (blocks & 0x30U) | DPF_DEFINED_SHORT);
        header[1] = static_cast<uint8_t>((sap << 4U) | (blocks & 0x0FU));
        header[8] = static_cast<uint8_t>((20U << 2U) | 0x01U); // DD Format UTF-16BE, Full Message
        header[9] = static_cast<uint8_t>(padOctets * 8);
    } else {
        header[0] = static_cast<uint8_t>((isGroup ? 0x80U : 0x00U) |
                                         ((padOctets & 0x10) ? 0x10U : 0U) |
                                         DPF_UNCONFIRMED_DATA);
        header[1] = static_cast<uint8_t>((sap << 4U) | (padOctets & 0x0FU));
        header[8] = static_cast<uint8_t>(0x80U | (blocks & 0x7FU));
        header[9] = 0U; // FSN 0, single fragment
    }
    header[2] = static_cast<uint8_t>(dstId >> 16U);
    header[3] = static_cast<uint8_t>(dstId >> 8U);
    header[4] = static_cast<uint8_t>(dstId);
    header[5] = static_cast<uint8_t>(srcId >> 16U);
    header[6] = static_cast<uint8_t>(srcId >> 8U);
    header[7] = static_cast<uint8_t>(srcId);
    addMaskedCrc16(header, 0xCCU);
    result.append(buildBptcDmrdFrame(dmrdSeq++, srcId, dstId, essid,
                                      slot, colorCode, isGroup,
                                      DT_DATA_HEADER, streamId, header));

    for (int block = 0; block < blocks; ++block) {
        result.append(buildBptcDmrdFrame(dmrdSeq++, srcId, dstId, essid,
                                          slot, colorCode, isGroup,
                                          DT_RATE_12_DATA, streamId,
                                          reinterpret_cast<const uint8_t *>(payload.constData() + block * 12)));
    }
    return result;
}

QVector<QByteArray> DmrDataCodec::buildConfirmedDataAckSequence(
    uint32_t mySrcId, uint32_t peerDstId, uint32_t essid,
    uint8_t slot, uint8_t colorCode, uint8_t sap, uint8_t rxSeqNo)
{
    QVector<QByteArray> out;
    const uint32_t streamId = 0x41434B00U ^ mySrcId ^ peerDstId ^
                              static_cast<uint32_t>(QDateTime::currentMSecsSinceEpoch());
    uint8_t header[12] {};
    header[0] = DPF_RESPONSE;
    header[1] = static_cast<uint8_t>((sap & 0x0FU) << 4U);
    header[2] = static_cast<uint8_t>(peerDstId >> 16U);
    header[3] = static_cast<uint8_t>(peerDstId >> 8U);
    header[4] = static_cast<uint8_t>(peerDstId);
    header[5] = static_cast<uint8_t>(mySrcId >> 16U);
    header[6] = static_cast<uint8_t>(mySrcId >> 8U);
    header[7] = static_cast<uint8_t>(mySrcId);
    header[8] = 1U;
    header[9] = static_cast<uint8_t>(0x08U | (rxSeqNo & 0x07U)); // Class 0, Type 1: ACK
    addMaskedCrc16(header, 0xCCU);
    out.append(buildBptcDmrdFrame(0U, mySrcId, peerDstId, essid, slot,
                                   colorCode, false, DT_DATA_HEADER,
                                   streamId, header));

    uint8_t response[12];
    std::memset(response, 0xFF, 8U); // All block bitmap bits received
    std::memset(response + 8U, 0, 4U);
    const uint32_t crc = computeDmrCrc32(response, 8U);
    response[8] = static_cast<uint8_t>(crc & 0xFFU);
    response[9] = static_cast<uint8_t>((crc >> 8U) & 0xFFU);
    response[10] = static_cast<uint8_t>((crc >> 16U) & 0xFFU);
    response[11] = static_cast<uint8_t>((crc >> 24U) & 0xFFU);
    out.append(buildBptcDmrdFrame(1U, mySrcId, peerDstId, essid, slot,
                                   colorCode, false, DT_RATE_12_DATA,
                                   streamId, response));

    // A standards-compliant terminator with private-call Full LC.
    uint8_t lc[12] {};
    lc[0] = static_cast<uint8_t>(FLCO_USER_USER);
    lc[3] = static_cast<uint8_t>(peerDstId >> 16U);
    lc[4] = static_cast<uint8_t>(peerDstId >> 8U);
    lc[5] = static_cast<uint8_t>(peerDstId);
    lc[6] = static_cast<uint8_t>(mySrcId >> 16U);
    lc[7] = static_cast<uint8_t>(mySrcId >> 8U);
    lc[8] = static_cast<uint8_t>(mySrcId);
    uint8_t parity[4] {};
    CRS129::encode(lc, 9U, parity);
    lc[9] = static_cast<uint8_t>(parity[2] ^ TERMINATOR_WITH_LC_CRC_MASK[0]);
    lc[10] = static_cast<uint8_t>(parity[1] ^ TERMINATOR_WITH_LC_CRC_MASK[1]);
    lc[11] = static_cast<uint8_t>(parity[0] ^ TERMINATOR_WITH_LC_CRC_MASK[2]);
    out.append(buildBptcDmrdFrame(2U, mySrcId, peerDstId, essid, slot,
                                   colorCode, false, DT_TERMINATOR_WITH_LC,
                                   streamId, lc));
    return out;
}

bool DmrDataCodec::trellisDecode(const uint8_t frame33[33], uint8_t out18[18])
{
    int8_t dibits[98] {};
    for (unsigned int i = 0; i < 98U; ++i) {
        unsigned int n1 = i * 2U;
        unsigned int n2 = n1 + 1U;
        if (n1 >= 98U) n1 += 68U;
        if (n2 >= 98U) n2 += 68U;
        const bool b1 = readBit(frame33, n1);
        const bool b2 = readBit(frame33, n2);
        const int8_t dibit = !b1 ? (b2 ? +3 : +1) : (b2 ? -3 : -1);
        dibits[TRELLIS_INTERLEAVE[i]] = dibit;
    }

    uint8_t points[49] {};
    for (unsigned int i = 0; i < 49U; ++i) {
        bool found = false;
        for (uint8_t point = 0; point < 16U; ++point) {
            if (POINT_DIBITS[point][0] == dibits[i * 2U] &&
                POINT_DIBITS[point][1] == dibits[i * 2U + 1U]) {
                points[i] = point;
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }

    auto decodePoints = [](const uint8_t *pts, uint8_t *tribits) -> int {
        uint8_t state = 0U;
        for (int i = 0; i < 49; ++i) {
            int found = -1;
            for (uint8_t t = 0; t < 8U; ++t) {
                if (pts[i] == TRELLIS_ENCODE[state * 8U + t]) {
                    found = t;
                    break;
                }
            }
            if (found < 0)
                return i;
            tribits[i] = static_cast<uint8_t>(found);
            state = tribits[i];
        }
        return tribits[48] == 0U ? 999 : 48;
    };

    uint8_t tribits[49] {};
    int fail = decodePoints(points, tribits);
    if (fail != 999) {
        // Limited trellis correction: choose the candidate that advances the
        // decoder furthest, repeating up to 20 symbols as MMDVMHost does.
        for (int tries = 0; tries < 20 && fail != 999; ++tries) {
            int bestFail = fail;
            uint8_t bestPoint = points[fail];
            for (uint8_t candidate = 0; candidate < 16U; ++candidate) {
                const uint8_t saved = points[fail];
                points[fail] = candidate;
                uint8_t trial[49] {};
                const int nextFail = decodePoints(points, trial);
                points[fail] = saved;
                if (nextFail == 999) {
                    points[fail] = candidate;
                    std::memcpy(tribits, trial, sizeof(tribits));
                    fail = 999;
                    break;
                }
                if (nextFail > bestFail) {
                    bestFail = nextFail;
                    bestPoint = candidate;
                }
            }
            if (fail != 999) {
                points[fail] = bestPoint;
                fail = decodePoints(points, tribits);
            }
        }
    }
    if (fail != 999)
        return false;

    std::memset(out18, 0, 18U);
    for (unsigned int i = 0; i < 48U; ++i) {
        for (unsigned int b = 0; b < 3U; ++b)
            writeBit(out18, i * 3U + b, (tribits[i] & (4U >> b)) != 0U);
    }
    return true;
}

void DmrDataCodec::trellisEncode(const uint8_t in18[18], uint8_t frame33[33])
{
    uint8_t tribits[49] {};
    for (unsigned int i = 0; i < 48U; ++i) {
        uint8_t value = 0U;
        for (unsigned int b = 0; b < 3U; ++b)
            value = static_cast<uint8_t>((value << 1U) | (readBit(in18, i * 3U + b) ? 1U : 0U));
        tribits[i] = value;
    }
    tribits[48] = 0U;

    uint8_t points[49] {};
    uint8_t state = 0U;
    for (unsigned int i = 0; i < 49U; ++i) {
        points[i] = TRELLIS_ENCODE[state * 8U + tribits[i]];
        state = tribits[i];
    }
    int8_t dibits[98] {};
    for (unsigned int i = 0; i < 49U; ++i) {
        dibits[i * 2U] = POINT_DIBITS[points[i]][0];
        dibits[i * 2U + 1U] = POINT_DIBITS[points[i]][1];
    }
    for (unsigned int i = 0; i < 98U; ++i) {
        const int8_t d = dibits[TRELLIS_INTERLEAVE[i]];
        const bool b1 = (d == -1 || d == -3);
        const bool b2 = (d == +3 || d == -3);
        unsigned int n1 = i * 2U;
        unsigned int n2 = n1 + 1U;
        if (n1 >= 98U) n1 += 68U;
        if (n2 >= 98U) n2 += 68U;
        writeBit(frame33, n1, b1);
        writeBit(frame33, n2, b2);
    }
}

uint32_t DmrDataCodec::computeDmrCrc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0U;
    size_t i = 0U;
    for (; i + 1U < len; i += 2U) {
        crc = crc32Byte(crc, data[i + 1U]);
        crc = crc32Byte(crc, data[i]);
    }
    if (i < len)
        crc = crc32Byte(crc, data[i]);
    return crc;
}

bool DmrDataCodec::verifyDmrCrc32(const uint8_t *data, size_t totalLen)
{
    if (!data || totalLen < 4U)
        return false;
    const size_t payloadLen = totalLen - 4U;
    const uint32_t sent = static_cast<uint32_t>(data[payloadLen]) |
                          (static_cast<uint32_t>(data[payloadLen + 1U]) << 8U) |
                          (static_cast<uint32_t>(data[payloadLen + 2U]) << 16U) |
                          (static_cast<uint32_t>(data[payloadLen + 3U]) << 24U);
    return computeDmrCrc32(data, payloadLen) == sent;
}

bool DmrDataCodec::verifyDmrCrc9(const uint8_t block18[18], uint8_t &outDbsn)
{
    return verifyConfirmedBlockCrc9(block18, 18U, outDbsn);
}

uint16_t DmrDataCodec::computeIpChecksum(const uint8_t *data, size_t offset, size_t len)
{
    uint32_t sum = 0U;
    size_t i = 0U;
    for (; i + 1U < len; i += 2U)
        sum += (static_cast<uint32_t>(data[offset + i]) << 8U) | data[offset + i + 1U];
    if (i < len)
        sum += static_cast<uint32_t>(data[offset + i]) << 8U;
    while (sum >> 16U)
        sum = (sum & 0xFFFFU) + (sum >> 16U);
    return static_cast<uint16_t>(~sum);
}

void DmrSmsAssembler::reset()
{
    m_states.clear();
    std::memset(m_lastDupBurst, 0, sizeof(m_lastDupBurst));
    m_lastDupSrcId = 0U;
    m_lastDupTimeMs = 0;
}

void DmrSmsAssembler::purgeStale(qint64 nowMs)
{
    for (auto it = m_states.begin(); it != m_states.end();) {
        if (nowMs - it->lastActivityMs > 10000)
            it = m_states.erase(it);
        else
            ++it;
    }
    while (m_states.size() >= 64) {
        auto oldest = m_states.begin();
        for (auto it = m_states.begin(); it != m_states.end(); ++it) {
            if (it->lastActivityMs < oldest->lastActivityMs)
                oldest = it;
        }
        m_states.erase(oldest);
    }
}

DmrDataCodec::DecodedSms DmrSmsAssembler::processBurst(
    uint32_t pktSrcId, uint32_t pktDstId, uint8_t slot, bool pktIsPrivate,
    uint8_t dataType, const uint8_t frame33[33], uint32_t myDmrId,
    uint32_t myEssid, uint8_t myColorCode)
{
    DmrDataCodec::DecodedSms out;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    purgeStale(now);

    if (!frame33)
        return out;
    if (dataType == DT_CSBK)
        return out; // Preambles provide RF wake-up only; addressing is in the data header.

    const uint64_t key = makeKey(slot, pktSrcId);
    if (dataType == DT_DATA_HEADER) {
        auto existing = m_states.find(key);
        if (existing != m_states.end() && existing->receivedBlocks > 0 &&
            pktSrcId == m_lastDupSrcId && (now - m_lastDupTimeMs) < 500 &&
            std::memcmp(frame33, m_lastDupBurst, 33U) == 0) {
            return out;
        }

        uint8_t header[12] {};
        CBPTC19696 bptc;
        bptc.decode(frame33, header);
        uint8_t checked[12];
        std::memcpy(checked, header, sizeof(checked));
        if (!checkMaskedCrc16(checked, 0xCCU))
            return out;

        std::memcpy(m_lastDupBurst, frame33, 33U);
        m_lastDupSrcId = pktSrcId;
        m_lastDupTimeMs = now;

        const uint8_t dpf = header[0] & 0x0FU;
        if (dpf == DPF_RESPONSE) {
            const uint8_t respClass = (header[9] >> 6U) & 0x03U;
            const uint8_t respType  = (header[9] >> 3U) & 0x07U;
            out.valid = true;
            out.isAck = true;
            out.ackSuccess = (respClass == 0U && respType == 1U);
            out.srcId = (static_cast<uint32_t>(header[5]) << 16U) |
                        (static_cast<uint32_t>(header[6]) << 8U) | header[7];
            out.dstId = (static_cast<uint32_t>(header[2]) << 16U) |
                        (static_cast<uint32_t>(header[3]) << 8U) | header[4];
            if (out.srcId == 0U) out.srcId = pktSrcId;
            if (out.dstId == 0U) out.dstId = pktDstId;
            out.slot = slot;
            out.formatName = QStringLiteral("DMR ACK");
            return out;
        }

        AssemblyState st;
        st.srcId = (static_cast<uint32_t>(header[5]) << 16U) |
                   (static_cast<uint32_t>(header[6]) << 8U) | header[7];
        st.dstId = (static_cast<uint32_t>(header[2]) << 16U) |
                   (static_cast<uint32_t>(header[3]) << 8U) | header[4];
        if (st.srcId == 0U) st.srcId = pktSrcId;
        if (st.dstId == 0U) st.dstId = pktDstId;
        st.slot = slot;
        st.isGroup = (header[0] & 0x80U) != 0U || !pktIsPrivate;
        st.responseRequested = (header[0] & 0x40U) != 0U || dpf == DPF_CONFIRMED_DATA;
        st.dpf = dpf;
        st.sap = static_cast<uint8_t>((header[1] >> 4U) & 0x0FU);
        st.lastActivityMs = now;

        if (dpf == DPF_UNCONFIRMED_DATA || dpf == DPF_CONFIRMED_DATA) {
            st.padOctets = static_cast<uint8_t>((((header[0] >> 4U) & 1U) << 4U) |
                                                (header[1] & 0x0FU));
            st.expectedBlocks = header[8] & 0x7FU;
            st.seqNo = dpf == DPF_CONFIRMED_DATA
                ? static_cast<uint8_t>((header[9] >> 4U) & 0x07U)
                : static_cast<uint8_t>(header[9] & 0x0FU);
        } else if (dpf == DPF_DEFINED_SHORT || dpf == DPF_DEFINED_RAW) {
            st.expectedBlocks = (header[0] & 0x30U) | (header[1] & 0x0FU);
            st.ddFormat = static_cast<uint8_t>(header[8] >> 2U);
            st.padOctets = static_cast<uint8_t>((header[9] + 7U) / 8U);
        } else if (dpf == DPF_UDT) {
            st.expectedBlocks = (header[8] & 0x03U) + 1;
            st.udtFormat = header[1] & 0x0FU;
            st.udtOpcode = header[9] & 0x3FU;
            st.padOctets = static_cast<uint8_t>((header[8] >> 3U) / 2U);
        } else {
            return out;
        }
        if (st.expectedBlocks <= 0 || st.expectedBlocks > 127)
            return out;
        st.buffer.reserve(st.expectedBlocks * 24);
        m_states.insert(key, st);
        return out;
    }

    auto it = m_states.find(key);
    if (it == m_states.end())
        return out;
    AssemblyState &st = it.value();
    st.lastActivityMs = now;

    auto markAndCheckDuplicateDbsn = [&st](uint8_t dbsn) -> bool {
        if (dbsn < 64U) {
            const uint64_t bit = (uint64_t(1) << dbsn);
            if ((st.seenDbsnLo & bit) != 0U)
                return true;
            st.seenDbsnLo |= bit;
        } else if (dbsn < 128U) {
            const uint64_t bit = (uint64_t(1) << (dbsn - 64U));
            if ((st.seenDbsnHi & bit) != 0U)
                return true;
            st.seenDbsnHi |= bit;
        }
        return false;
    };

    if (dataType == DT_RATE_12_DATA) {
        uint8_t block[12] {};
        CBPTC19696 bptc;
        bptc.decode(frame33, block);
        if (st.dpf == DPF_CONFIRMED_DATA) {
            // Confirmed Rate 1/2 carries two overhead bytes and 10 payload bytes.
            uint8_t dbsn = 0U;
            (void)verifyConfirmedBlockCrc9(block, 12U, dbsn);
            if (markAndCheckDuplicateDbsn(dbsn))
                return out;
            st.buffer.append(reinterpret_cast<const char *>(block + 2U), 10);
        } else {
            st.buffer.append(reinterpret_cast<const char *>(block), 12);
        }
    } else if (dataType == DT_RATE_34_DATA) {
        uint8_t block[18] {};
        if (!DmrDataCodec::trellisDecode(frame33, block))
            return out;
        uint8_t dbsn = 0U;
        if (st.dpf == DPF_CONFIRMED_DATA) {
            (void)DmrDataCodec::verifyDmrCrc9(block, dbsn);
            if (markAndCheckDuplicateDbsn(dbsn))
                return out;
            st.buffer.append(reinterpret_cast<const char *>(block + 2U), 16);
        } else {
            st.buffer.append(reinterpret_cast<const char *>(block), 18);
        }
    } else if (dataType == DT_RATE_1_DATA) {
        // Rate 1 data occupies the 98+98 payload bits around the slot type/sync.
        // Reconstruct the 24 bytes in bit order.
        uint8_t block[24] {};
        unsigned int outBit = 0U;
        for (unsigned int inBit = 0U; inBit < 264U && outBit < 192U; ++inBit) {
            if (inBit >= 98U && inBit < 166U)
                continue;
            writeBit(block, outBit++, readBit(frame33, inBit));
        }
        if (st.dpf == DPF_CONFIRMED_DATA) {
            uint8_t dbsn = 0U;
            (void)verifyConfirmedBlockCrc9(block, 24U, dbsn);
            if (markAndCheckDuplicateDbsn(dbsn))
                return out;
            st.buffer.append(reinterpret_cast<const char *>(block + 2U), 22);
        } else {
            st.buffer.append(reinterpret_cast<const char *>(block), 24);
        }
    } else {
        return out;
    }

    ++st.receivedBlocks;
    if (st.receivedBlocks < st.expectedBlocks)
        return out;

    const AssemblyState completed = st;
    m_states.erase(it);
    if (finalizeMessage(completed, myDmrId, myEssid, myColorCode, out) &&
        !out.isAck && completed.responseRequested && !completed.isGroup &&
        (completed.dstId == myDmrId || completed.dstId == myEssid)) {
        out.autoAckDmrdFrames = DmrDataCodec::buildConfirmedDataAckSequence(
            myDmrId, completed.srcId, myEssid, completed.slot,
            myColorCode, completed.sap, completed.seqNo);
    }
    return out;
}

bool DmrSmsAssembler::finalizeMessage(const AssemblyState &st,
                                      uint32_t myDmrId, uint32_t myEssid,
                                      uint8_t myColorCode,
                                      DmrDataCodec::DecodedSms &out)
{
    Q_UNUSED(myDmrId)
    Q_UNUSED(myEssid)
    Q_UNUSED(myColorCode)
    if (st.buffer.isEmpty())
        return false;

    QByteArray data = st.buffer;
    out = DmrDataCodec::DecodedSms{};
    out.srcId = st.srcId;
    out.dstId = st.dstId;
    out.slot = st.slot;
    out.isGroup = st.isGroup;

    if (st.dpf == DPF_UDT) {
        if (data.size() < 2 || !CCRC::checkCCITT162(
                reinterpret_cast<const uint8_t *>(data.constData()),
                static_cast<uint32_t>(data.size())))
            return false;
        int textLen = data.size() - 2 - st.padOctets;
        if (textLen <= 0)
            return false;
        const auto *raw = reinterpret_cast<const uint8_t *>(data.constData());
        if (st.udtFormat == 0x07U) {
            out.text = decodeUtf16(raw, textLen, false);
            out.formatName = QStringLiteral("UDT UTF-16");
        } else if (st.udtFormat == 0x03U) {
            const QByteArray iso = decodeIso7(raw, textLen, (textLen * 8) / 7);
            out.text = QString::fromLatin1(iso).trimmed();
            out.formatName = QStringLiteral("UDT ISO-7");
        } else if (st.udtFormat == 0x04U) {
            out.text = QString::fromUtf8(data.constData(), textLen).trimmed();
            out.formatName = QStringLiteral("UDT UTF-8");
        } else {
            return false; // Includes NMEA location format 0x05.
        }
        out.valid = !out.text.isEmpty();
        return out.valid;
    }

    const bool hasCrc = DmrDataCodec::verifyDmrCrc32(
        reinterpret_cast<const uint8_t *>(data.constData()),
        static_cast<size_t>(data.size()));
    if (st.dpf == DPF_UNCONFIRMED_DATA || st.dpf == DPF_CONFIRMED_DATA) {
        if (!hasCrc)
            return false;
        int pduLen = data.size() - 4 - st.padOctets;
        if (pduLen < 0)
            return false;
        data.truncate(pduLen);
        if (st.sap != 0x04U && st.sap != 0x03U)
            return false;
        const auto *p = reinterpret_cast<const uint8_t *>(data.constData());

        if (st.sap == 0x03U) {
            // ETSI compressed UDP/IP: locate optional explicit ports and payload.
            if (data.size() < 5)
                return false;
            const uint8_t srcPortIdx = p[3] & 0x7FU;
            const uint8_t dstPortIdx = p[4] & 0x7FU;
            int offset = 5;
            uint16_t srcPort = srcPortIdx == 1U ? 5016U : srcPortIdx;
            uint16_t dstPort = dstPortIdx == 1U ? 5016U : dstPortIdx;
            if (srcPortIdx == 0U && data.size() >= offset + 2) {
                srcPort = static_cast<uint16_t>((p[offset] << 8U) | p[offset + 1]);
                offset += 2;
            }
            if (dstPortIdx == 0U && data.size() >= offset + 2) {
                dstPort = static_cast<uint16_t>((p[offset] << 8U) | p[offset + 1]);
                offset += 2;
            }
            if (offset >= data.size())
                return false;
            const int len = data.size() - offset;
            if (srcPort == 5016U || dstPort == 5016U) {
                int textOffset = offset;
                if (data.size() >= offset + 4 &&
                    p[offset] == 0x00U && p[offset + 1] == 0x0DU &&
                    p[offset + 2] == 0x00U && p[offset + 3] == 0x0AU) {
                    textOffset += 4;
                }
                const bool little = likelyLittleEndianUtf16(p + textOffset, data.size() - textOffset);
                out.text = decodeUtf16(p + textOffset, data.size() - textOffset, little);
                out.formatName = QStringLiteral("ETSI Compressed");
            } else if (srcPort >= 0x00C0U || dstPort >= 0x00C0U || srcPort == 98U || dstPort == 98U) {
                // Manufacturer-specific compressed port (Motorola TMS).
                if (len >= 5) {
                    const uint16_t tmsLen = static_cast<uint16_t>((p[offset] << 8U) | p[offset + 1]);
                    if (tmsLen <= 4U && (p[offset + 2] & 0xF0U) == 0xB0U) {
                        out.valid = true;
                        out.isAck = true;
                        out.ackSuccess = true;
                        out.formatName = QStringLiteral("M-SMS ACK");
                        return true;
                    }
                }
                int textOffset = offset;
                for (int i = offset; i + 2 < data.size(); ++i) {
                    if (p[i] == 0x0DU && p[i + 1] == 0x00U && p[i + 2] == 0x0AU) {
                        textOffset = i + 3;
                        if (textOffset + 2 < data.size() &&
                            p[textOffset] == 0x00U && p[textOffset + 1] == 0x00U &&
                            p[textOffset + 2] != 0x00U) {
                            ++textOffset;
                        }
                        break;
                    }
                }
                const bool little = likelyLittleEndianUtf16(p + textOffset, data.size() - textOffset);
                out.text = decodeUtf16(p + textOffset, data.size() - textOffset, little);
                out.formatName = QStringLiteral("M-SMS Compressed");
            }
        } else {
            if (data.size() < 28 || (p[0] >> 4U) != 4U || p[9] != 0x11U)
                return false;
            const int ipHeaderLen = (p[0] & 0x0FU) * 4;
            if (ipHeaderLen < 20 || data.size() < ipHeaderLen + 8)
                return false;
            const uint16_t port = static_cast<uint16_t>(
                (p[ipHeaderLen + 2] << 8U) | p[ipHeaderLen + 3]);
            const int appOffset = ipHeaderLen + 8;
            const bool isMotoTmsOn5016 = (port == 5016U && data.size() >= appOffset + 9 &&
                                          p[appOffset + 5] == 0x04U && p[appOffset + 6] == 0x0DU &&
                                          p[appOffset + 7] == 0x00U && p[appOffset + 8] == 0x0AU);
            if (port == 4007U || isMotoTmsOn5016) {
                if (data.size() >= appOffset + 5) {
                    const uint16_t tmsLen = static_cast<uint16_t>((p[appOffset] << 8U) | p[appOffset + 1]);
                    if (tmsLen <= 4U && (p[appOffset + 2] & 0xF0U) == 0xB0U) {
                        out.valid = true;
                        out.isAck = true;
                        out.ackSuccess = true;
                        out.formatName = QStringLiteral("M-SMS ACK");
                        return true;
                    }
                }
                if (data.size() < appOffset + 9)
                    return false;
                const uint16_t tmsLen = static_cast<uint16_t>((p[appOffset] << 8U) | p[appOffset + 1]);
                int textOffset = appOffset + 9;
                for (int i = appOffset + 4; i + 2 < std::min<int>(data.size(), appOffset + 12); ++i) {
                    if (p[i] == 0x0DU && p[i + 1] == 0x00U && p[i + 2] == 0x0AU) {
                        textOffset = i + 3;
                        break;
                    }
                }
                if (textOffset + 2 < data.size() &&
                    p[textOffset] == 0x00U && p[textOffset + 1] == 0x00U &&
                    p[textOffset + 2] != 0x00U) {
                    ++textOffset;
                }
                const int endOffset = (tmsLen >= 7U && appOffset + 2 + static_cast<int>(tmsLen) <= data.size())
                                      ? (appOffset + 2 + static_cast<int>(tmsLen))
                                      : data.size();
                if (endOffset <= textOffset)
                    return false;
                const int textLen = endOffset - textOffset;
                const bool little = likelyLittleEndianUtf16(p + textOffset, textLen);
                out.text = decodeUtf16(p + textOffset, textLen, little);
                out.formatName = QStringLiteral("M-SMS");
            } else if (port == 5016U) {
                int textOffset = appOffset;
                if (data.size() >= appOffset + 4 &&
                    p[appOffset] == 0x00U && p[appOffset + 1] == 0x0DU &&
                    p[appOffset + 2] == 0x00U && p[appOffset + 3] == 0x0AU)
                    textOffset += 4;
                const uint16_t udpLen = static_cast<uint16_t>((p[ipHeaderLen + 4] << 8U) | p[ipHeaderLen + 5]);
                const int endOffset = (udpLen >= 12U && ipHeaderLen + static_cast<int>(udpLen) <= data.size())
                                      ? (ipHeaderLen + static_cast<int>(udpLen))
                                      : data.size();
                if (endOffset <= textOffset)
                    return false;
                const int textLen = endOffset - textOffset;
                const bool little = likelyLittleEndianUtf16(p + textOffset, textLen);
                out.text = decodeUtf16(p + textOffset, textLen, little);
                out.formatName = little ? QStringLiteral("DMR Standard LE")
                                        : QStringLiteral("ETSI UTF-16BE");
            } else {
                return false; // ARS, LRRP, GPS, or unknown application.
            }
        }
    } else if (st.dpf == DPF_DEFINED_SHORT || st.dpf == DPF_DEFINED_RAW) {
        int len = data.size() - st.padOctets;
        if (hasCrc && len > 4)
            len -= 4;
        if (len <= 0)
            return false;
        const auto *raw = reinterpret_cast<const uint8_t *>(data.constData());
        int offset = 0;
        // Many Hytera H-SMS frames prepend two non-zero control bytes (< 0x20).
        if (len >= 4 && raw[0] != 0x00U && raw[0] < 0x20U && raw[1] < 0x20U) {
            offset = 2;
        }
        if (st.ddFormat == 19U || st.ddFormat == 20U) {
            out.text = decodeUtf16(raw + offset, len - offset, false);
            out.formatName = QStringLiteral("Short Data UTF-16BE");
        } else if (st.ddFormat == 21U) {
            out.text = decodeUtf16(raw + offset, len - offset, true);
            out.formatName = QStringLiteral("Short Data UTF-16LE");
        } else if (st.ddFormat == 18U) {
            out.text = QString::fromUtf8(data.constData() + offset, len - offset).trimmed();
            out.formatName = QStringLiteral("Short Data UTF-8");
        } else if (st.ddFormat == 2U) {
            const QByteArray iso = decodeIso7(raw + offset, len - offset, ((len - offset) * 8) / 7);
            out.text = QString::fromLatin1(iso).trimmed();
            out.formatName = QStringLiteral("Short Data ISO-7");
        } else if (st.ddFormat >= 3U && st.ddFormat <= 17U) {
            out.text = QString::fromLatin1(data.constData() + offset, len - offset).trimmed();
            out.formatName = QStringLiteral("Short Data ISO-8");
        } else {
            // Practical fallback used by several Chinese-radio firmware variants.
            const bool little = likelyLittleEndianUtf16(raw + offset, len - offset);
            out.text = decodeUtf16(raw + offset, len - offset, little);
            out.formatName = QStringLiteral("Short Data");
        }
    }

    out.valid = !out.text.isEmpty();
    return out.valid;
}
