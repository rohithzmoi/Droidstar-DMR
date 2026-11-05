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

#include "ReplayFileWriter.h"

#include <QFile>
#include <QtEndian>

// 16-bit PCM RIFF/WAV writer: non-iOS builds and the offline engine tests. The header is written
// with placeholder sizes and patched on close().
class ReplayWavWriter : public IReplayFileWriter
{
public:
    bool open(const QString &path, int sampleRate, int channels, int /*bitrateBps*/) override
    {
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_error = QStringLiteral("open failed: %1").arg(m_file.errorString());
            return false;
        }
        m_rate = sampleRate;
        m_channels = channels;
        m_dataBytes = 0;
        return writeHeader(0);
    }

    bool write(const int16_t *pcm, size_t frames) override
    {
        if (!m_file.isOpen() || pcm == nullptr || frames == 0) {
            return m_file.isOpen();
        }
        const qint64 bytes = static_cast<qint64>(frames) * m_channels * 2;
        const qint64 written = m_file.write(reinterpret_cast<const char *>(pcm), bytes);
        if (written != bytes) {
            m_error = QStringLiteral("write failed: %1").arg(m_file.errorString());
            return false;
        }
        m_dataBytes += bytes;
        return true;
    }

    bool close() override
    {
        if (!m_file.isOpen()) {
            return false;
        }
        bool ok = m_file.seek(0) && writeHeader(m_dataBytes);
        ok = m_file.flush() && ok;
        m_file.close();
        if (!ok) {
            m_error = QStringLiteral("finalize failed: %1").arg(m_file.errorString());
        }
        return ok;
    }

    void abort() override
    {
        if (m_file.isOpen()) {
            m_file.close();
        }
    }

    const char *extension() const override { return "wav"; }
    int nominalBytesPerSecond() const override { return m_rate * m_channels * 2; }
    QString lastError() const override { return m_error; }

private:
    bool writeHeader(qint64 dataBytes)
    {
        unsigned char h[44];
        auto put32 = [&h](int off, quint32 v) { qToLittleEndian<quint32>(v, h + off); };
        auto put16 = [&h](int off, quint16 v) { qToLittleEndian<quint16>(v, h + off); };
        const quint32 data = static_cast<quint32>(dataBytes);
        std::memcpy(h, "RIFF", 4);
        put32(4, 36U + data);
        std::memcpy(h + 8, "WAVE", 4);
        std::memcpy(h + 12, "fmt ", 4);
        put32(16, 16U);
        put16(20, 1U);                                             // PCM
        put16(22, static_cast<quint16>(m_channels));
        put32(24, static_cast<quint32>(m_rate));
        put32(28, static_cast<quint32>(m_rate * m_channels * 2));
        put16(32, static_cast<quint16>(m_channels * 2));
        put16(34, 16U);
        std::memcpy(h + 36, "data", 4);
        put32(40, data);
        if (m_file.write(reinterpret_cast<const char *>(h), 44) != 44) {
            m_error = QStringLiteral("header write failed: %1").arg(m_file.errorString());
            return false;
        }
        return true;
    }

    QFile m_file;
    QString m_error;
    int m_rate { 8000 };
    int m_channels { 1 };
    qint64 m_dataBytes { 0 };
};

std::unique_ptr<IReplayFileWriter> createWavReplayFileWriter()
{
    return std::make_unique<ReplayWavWriter>();
}

#ifndef Q_OS_IOS
void replayPlatformPrepareDirectory(const QString &) {}
void replayPlatformProtectFile(const QString &) {}
bool replayPlatformStorageWritable() { return true; }
#endif
