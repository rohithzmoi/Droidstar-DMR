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

// iOS QSO Replay writer: AAC-LC, 8 kHz, mono, 24 kbps in an .m4a container through ExtAudioFile.
// Runs on the replay worker thread only. The software codec is forced so backgrounding (which can
// take the hardware encoder away) never fails a clip; 8 kHz mono AAC costs well under 1% of a core.

#include "ReplayFileWriter.h"

#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <QString>
#include <atomic>

namespace {

std::atomic<bool> g_protectedDataAvailable { true };
std::atomic<bool> g_observersInstalled { false };

QString osStatusText(OSStatus st)
{
    char fourcc[5] = { 0, 0, 0, 0, 0 };
    const UInt32 v = static_cast<UInt32>(st);
    fourcc[0] = static_cast<char>((v >> 24) & 0xff);
    fourcc[1] = static_cast<char>((v >> 16) & 0xff);
    fourcc[2] = static_cast<char>((v >> 8) & 0xff);
    fourcc[3] = static_cast<char>(v & 0xff);
    bool printable = true;
    for (int i = 0; i < 4; ++i) {
        if (fourcc[i] < 32 || fourcc[i] > 126) printable = false;
    }
    return printable ? QStringLiteral("'%1' (%2)").arg(QString::fromLatin1(fourcc)).arg(static_cast<int>(st))
                     : QString::number(static_cast<int>(st));
}

class ReplayAacWriter : public IReplayFileWriter
{
public:
    ~ReplayAacWriter() override { abort(); }

    bool open(const QString &path, int sampleRate, int channels, int bitrateBps) override
    {
        abort();
        @autoreleasepool {
            NSURL *url = [NSURL fileURLWithPath:path.toNSString()];

            AudioStreamBasicDescription out = {};
            out.mSampleRate = static_cast<Float64>(sampleRate);
            out.mFormatID = kAudioFormatMPEG4AAC;
            out.mChannelsPerFrame = static_cast<UInt32>(channels);

            OSStatus st = ExtAudioFileCreateWithURL((__bridge CFURLRef)url, kAudioFileM4AType, &out, nullptr,
                                                    kAudioFileFlags_EraseFile, &m_file);
            if (st != noErr || m_file == nullptr) {
                m_file = nullptr;
                return fail(QStringLiteral("ExtAudioFileCreateWithURL %1").arg(osStatusText(st)));
            }

            // Must precede the client format: picks the software AAC encoder for this file.
            UInt32 manufacturer = kAppleSoftwareAudioCodecManufacturer;
            st = ExtAudioFileSetProperty(m_file, kExtAudioFileProperty_CodecManufacturer, sizeof(manufacturer), &manufacturer);
            if (st != noErr) {
                NSLog(@"[QsoReplay] software codec selection failed (%d); continuing with default", static_cast<int>(st));
            }

            AudioStreamBasicDescription client = {};
            client.mSampleRate = static_cast<Float64>(sampleRate);
            client.mFormatID = kAudioFormatLinearPCM;
            client.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
            client.mBitsPerChannel = 16;
            client.mChannelsPerFrame = static_cast<UInt32>(channels);
            client.mFramesPerPacket = 1;
            client.mBytesPerFrame = static_cast<UInt32>(2 * channels);
            client.mBytesPerPacket = client.mBytesPerFrame;
            st = ExtAudioFileSetProperty(m_file, kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client);
            if (st != noErr) {
                return fail(QStringLiteral("ClientDataFormat %1").arg(osStatusText(st)));
            }

            // Bit rate is best effort: an unsupported value leaves the encoder default in place.
            AudioConverterRef converter = nullptr;
            UInt32 size = sizeof(converter);
            if (ExtAudioFileGetProperty(m_file, kExtAudioFileProperty_AudioConverter, &size, &converter) == noErr && converter) {
                UInt32 br = static_cast<UInt32>(bitrateBps);
                if (AudioConverterSetProperty(converter, kAudioConverterEncodeBitRate, sizeof(br), &br) == noErr) {
                    CFArrayRef cfg = nullptr;
                    ExtAudioFileSetProperty(m_file, kExtAudioFileProperty_ConverterConfig, sizeof(cfg), &cfg);
                }
            }
            m_channels = channels;
            m_bytesPerFrame = 2 * channels;
            return true;
        }
    }

    bool write(const int16_t *pcm, size_t frames) override
    {
        if (!m_file) {
            return false;
        }
        if (!pcm || frames == 0) {
            return true;
        }
        AudioBufferList list;
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = static_cast<UInt32>(m_channels);
        list.mBuffers[0].mDataByteSize = static_cast<UInt32>(frames * static_cast<size_t>(m_bytesPerFrame));
        list.mBuffers[0].mData = const_cast<int16_t *>(pcm);
        const OSStatus st = ExtAudioFileWrite(m_file, static_cast<UInt32>(frames), &list);
        if (st != noErr) {
            return fail(QStringLiteral("ExtAudioFileWrite %1").arg(osStatusText(st)));
        }
        return true;
    }

    bool close() override
    {
        if (!m_file) {
            return false;
        }
        const OSStatus st = ExtAudioFileDispose(m_file);   // flushes the encoder and writes moov
        m_file = nullptr;
        if (st != noErr) {
            return fail(QStringLiteral("ExtAudioFileDispose %1").arg(osStatusText(st)));
        }
        return true;
    }

    void abort() override
    {
        if (m_file) {
            ExtAudioFileDispose(m_file);
            m_file = nullptr;
        }
    }

    const char *extension() const override { return "m4a"; }
    // 24 kbps AAC-LC plus ~3% container overhead.
    int nominalBytesPerSecond() const override { return 3090; }
    QString lastError() const override { return m_error; }

private:
    bool fail(const QString &what)
    {
        m_error = what;
        NSLog(@"[QsoReplay] %@", what.toNSString());
        return false;
    }

    ExtAudioFileRef m_file { nullptr };
    QString m_error;
    int m_channels { 1 };
    int m_bytesPerFrame { 2 };
};

void installProtectedDataObservers()
{
    if (g_observersInstalled.exchange(true)) {
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        UIApplication *app = [UIApplication sharedApplication];
        g_protectedDataAvailable.store(app.isProtectedDataAvailable ? true : false);
        NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
        [nc addObserverForName:UIApplicationProtectedDataWillBecomeUnavailable object:nil queue:nil
                    usingBlock:^(NSNotification *) { g_protectedDataAvailable.store(false); }];
        [nc addObserverForName:UIApplicationProtectedDataDidBecomeAvailable object:nil queue:nil
                    usingBlock:^(NSNotification *) { g_protectedDataAvailable.store(true); }];
    });
}

} // namespace

std::unique_ptr<IReplayFileWriter> createAacReplayFileWriter()
{
    return std::make_unique<ReplayAacWriter>();
}

void replayPlatformPrepareDirectory(const QString &dir)
{
    @autoreleasepool {
        NSURL *url = [NSURL fileURLWithPath:dir.toNSString() isDirectory:YES];
        NSError *error = nil;
        // Replays never leave the device: not in iCloud or Finder/iTunes backups.
        if (![url setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&error]) {
            NSLog(@"[QsoReplay] backup exclusion failed: %@", error.localizedDescription);
        }
        // CompleteUntilFirstUserAuthentication: encrypted at rest, yet writable while the device is
        // locked during background RX (NSFileProtectionComplete would fail those writes).
        NSDictionary *attrs = @{ NSFileProtectionKey : NSFileProtectionCompleteUntilFirstUserAuthentication };
        [[NSFileManager defaultManager] setAttributes:attrs ofItemAtPath:dir.toNSString() error:nil];
    }
    installProtectedDataObservers();
}

void replayPlatformProtectFile(const QString &path)
{
    @autoreleasepool {
        NSDictionary *attrs = @{ NSFileProtectionKey : NSFileProtectionCompleteUntilFirstUserAuthentication };
        NSError *error = nil;
        if (![[NSFileManager defaultManager] setAttributes:attrs ofItemAtPath:path.toNSString() error:&error]) {
            NSLog(@"[QsoReplay] file protection failed: %@", error.localizedDescription);
        }
    }
}

bool replayPlatformStorageWritable()
{
    return g_protectedDataAvailable.load();
}
