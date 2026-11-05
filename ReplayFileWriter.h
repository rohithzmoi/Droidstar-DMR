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

#ifndef REPLAYFILEWRITER_H
#define REPLAYFILEWRITER_H

#include <QString>
#include <cstddef>
#include <cstdint>
#include <memory>

// One QSO Replay clip file. Every call runs on the replay worker thread; nothing here is reached
// from the audio path. The codec sits behind this interface so the iOS AAC-LC writer (AudioToolbox)
// and the plain WAV writer (other platforms, unit tests) are interchangeable, and so Opus/CAF could
// be swapped in later without touching the engine.
class IReplayFileWriter
{
public:
    virtual ~IReplayFileWriter() = default;

    // Creates `path` (the ".part" file). Returns false and sets lastError() on any failure.
    virtual bool open(const QString &path, int sampleRate, int channels, int bitrateBps) = 0;
    // Appends interleaved int16 PCM frames. false == fatal: the caller aborts the clip.
    virtual bool write(const int16_t *pcm, size_t frames) = 0;
    // Finalizes the container. false == the file is suspect and the caller deletes it.
    virtual bool close() = 0;
    // Releases resources without finalizing; the caller removes the file.
    virtual void abort() = 0;

    virtual const char *extension() const = 0;        // "m4a" or "wav"
    virtual int nominalBytesPerSecond() const = 0;    // for the storage-hours estimate
    virtual QString lastError() const = 0;
};

std::unique_ptr<IReplayFileWriter> createWavReplayFileWriter();
#ifdef Q_OS_IOS
std::unique_ptr<IReplayFileWriter> createAacReplayFileWriter();
#endif

// Platform storage policy. iOS: excludes the replay folder from iCloud/Finder backup and applies
// NSFileProtectionCompleteUntilFirstUserAuthentication (writable while the device is locked during
// background RX, still encrypted at rest). Elsewhere these are no-ops.
void replayPlatformPrepareDirectory(const QString &dir);
void replayPlatformProtectFile(const QString &path);
// false only before the first unlock after a reboot (protected data unavailable); the engine then
// skips the clip instead of failing an open().
bool replayPlatformStorageWritable();

#endif // REPLAYFILEWRITER_H
