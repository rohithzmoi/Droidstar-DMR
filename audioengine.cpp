/*
    Copyright (C) 2019-2021 Doug McLain
    Modified Copyright (C) 2024 Rohith Namboothiri
    
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

#include "audioengine.h"
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <QtMath>
#include "AudioSessionManager.h"

#if defined (Q_OS_MACOS) || defined(Q_OS_IOS)
#define MACHAK 1
#else
#define MACHAK 0
#endif

AudioEngine::AudioEngine(QString in, QString out) :
    m_outputdevice(out),
    m_inputdevice(in),
    m_out(nullptr),
    m_in(nullptr),
    m_srm(1)
{
    m_audio_out_temp_buf_p = m_audio_out_temp_buf;
    memset(m_aout_max_buf, 0, sizeof(float) * 200);
    m_aout_max_buf_p = m_aout_max_buf;
    m_aout_max_buf_idx = 0;
    m_aout_gain = 100;
    m_volume = 1.0f;
}

static inline int16_t clampToInt16(float x)
{
    x = std::clamp(x, -32768.0f, 32767.0f);
    return static_cast<int16_t>(lrintf(x));
}

AudioEngine::Biquad AudioEngine::makeLowpassBiquad(float fs, float fc, float Q)
{
    // RBJ cookbook low-pass biquad, Direct Form II (transposed) coefficients.
    // fc should be < fs/2.
    AudioEngine::Biquad bq;
    if (fs <= 0.0f || fc <= 0.0f || fc >= (fs * 0.499f)) {
        return bq; // passthrough defaults
    }

    const float w0 = 2.0f * static_cast<float>(M_PI) * (fc / fs);
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * Q);

    const float b0 = (1.0f - cosw0) * 0.5f;
    const float b1 = (1.0f - cosw0);
    const float b2 = (1.0f - cosw0) * 0.5f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    bq.z1 = 0.0f;
    bq.z2 = 0.0f;
    return bq;
}

float AudioEngine::biquadProcess(AudioEngine::Biquad &bq, float x)
{
    // Direct Form II (transposed)
    const float y = bq.b0 * x + bq.z1;
    bq.z1 = bq.b1 * x - bq.a1 * y + bq.z2;
    bq.z2 = bq.b2 * x - bq.a2 * y;
    return y;
}

AudioEngine::~AudioEngine()
{
}

QStringList AudioEngine::discover_audio_devices(uint8_t d)
{
    QStringList list;
    QList<QAudioDevice> devices;

    if(d) {  // Fetch audio outputs (playback devices)
        devices = QMediaDevices::audioOutputs();
    } else {  // Fetch audio inputs (capture devices)
        devices = QMediaDevices::audioInputs();
    }

    for (const QAudioDevice &device : devices) {
        QString description = device.description();

        // Map device descriptions to friendly names
        if (description.contains("com.apple.airpods")) {
            list.append("AirPods");
        } else if (description.contains("com.apple.avfoundation.avcapturedevice.built-in_audio")) {
            list.append("Built-in Microphone");
        } else if (description == "default") {
            list.append("System Default");
        } else if (description.contains("Bluetooth")) {
            list.append("Bluetooth Device");
        } else {
            list.append(description);
        }
    }

    // Emit the signal to notify QML about the update
   // emit audioDeviceListChanged();  // Ensure signal is emitted

    return list;
}

void AudioEngine::init()
{
    // Re-init safety: stop and release old IO objects first.
    if (m_in != nullptr) {
        stop_capture();
        delete m_in;
        m_in = nullptr;
        m_indev = nullptr;
    }
    if (m_out != nullptr) {
        stop_playback();
        delete m_out;
        m_out = nullptr;
        m_outdev = nullptr;
    }

    QAudioFormat format;
    format.setSampleRate(8000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    m_agc = true;

    QList<QAudioDevice> devices = QMediaDevices::audioOutputs();
    if(devices.size() == 0){
        qDebug() << "No audio playback hardware found";
    }
    else{
        QAudioDevice device(QMediaDevices::defaultAudioOutput());
        for (QList<QAudioDevice>::ConstIterator it = devices.constBegin(); it != devices.constEnd(); ++it ) {

            qDebug() << "Playback device name = " << (*it).description();
            qDebug() << (*it).supportedSampleFormats();
            qDebug() << (*it).preferredFormat();

            if((*it).description() == m_outputdevice){
                device = *it;
            }
        }
        if (!device.isFormatSupported(format)) {
            // Fallback: prefer device native rate; we will upsample 8 kHz -> deviceRate for output.
            QAudioFormat preferred = device.preferredFormat();
            QAudioFormat candidate = preferred;
            candidate.setChannelCount(1);
            candidate.setSampleFormat(QAudioFormat::Int16);
            if (device.isFormatSupported(candidate)) {
                format = candidate;
            } else {
                format = preferred; // last resort
            }
            qWarning() << "8 kHz mono S16 not supported for playback; falling back to device format"
                       << format;
        }

        m_playbackDeviceRate = format.sampleRate();
        m_playStreamIndex = -1;
        m_playNextOutPos = 0.0;
        m_playPrevSample = 0.0f;

        qDebug() << "Playback device:" << device.description()
                 << "SR:" << format.sampleRate()
                 << "Ch:" << format.channelCount()
                 << "Fmt:" << format.sampleFormat();

        m_out = new QAudioSink(device, format, this);
        // A slightly larger buffer reduces underruns on iOS when the UI thread is busy.
        m_out->setBufferSize(4096);
        connect(m_out, SIGNAL(stateChanged(QAudio::State)), this, SLOT(handleStateChanged(QAudio::State)));
    }

    devices = QMediaDevices::audioInputs();

    if(devices.size() == 0){
        qDebug() <<  "No audio capture hardware found";
    }
    else{
        QAudioDevice device(QMediaDevices::defaultAudioInput());
        for (QList<QAudioDevice>::ConstIterator it = devices.constBegin(); it != devices.constEnd(); ++it ) {
            if(MACHAK){
                qDebug() << "Playback device name = " << (*it).description();
                qDebug() << (*it).supportedSampleFormats();
                qDebug() << (*it).preferredFormat();
            }
            if((*it).description() == m_inputdevice){
                device = *it;
            }
        }
        if (!device.isFormatSupported(format)) {
            qWarning() << "Raw audio format not supported by capture device";
        }

        int sr = 8000;
        if (MACHAK) {
            // On iOS/macOS, prefer capturing at the device-native rate and resample properly to 8 kHz.
            sr = device.preferredFormat().sampleRate();
        }
        format.setSampleRate(sr);
        m_in = new QAudioSource(device, format, this);
        // Larger input buffer reduces choppiness when the main thread is busy (common on iOS).
        m_in->setBufferSize(4096);

        m_captureDeviceRate = m_in->format().sampleRate();
        m_srm = (m_captureDeviceRate > 0) ? (static_cast<float>(m_captureDeviceRate) / 8000.0f) : 1.0f;
        m_capStreamIndex = -1;
        m_capNextOutPos = 0.0;
        m_capPrevSample = 0.0f;
        // Low-pass just below 4 kHz Nyquist (8 kHz target) to reduce aliasing.
        m_capLowpass = makeLowpassBiquad(static_cast<float>(m_captureDeviceRate), 3400.0f, 0.707f);

        qDebug() << "Capture device:" << device.description()
                 << "SR:" << m_captureDeviceRate
                 << "resample->8k ratio:" << m_srm;
    }

    // Emit signal after initializing input and output devices
    emit audioDeviceListChanged();
}




void AudioEngine::start_capture()
{
    m_audioinq.clear();
   // setupAVAudioSession();
    //setPreferredInputDevice();
    if(m_in != nullptr){
        m_indev = m_in->start();
        if (MACHAK) {
            m_captureDeviceRate = m_in->format().sampleRate();
            m_srm = (m_captureDeviceRate > 0) ? (static_cast<float>(m_captureDeviceRate) / 8000.0f) : 1.0f;
            m_capLowpass = makeLowpassBiquad(static_cast<float>(m_captureDeviceRate), 3400.0f, 0.707f);
        }
        connect(m_indev, SIGNAL(readyRead()), SLOT(input_data_received()));
    }
}

void AudioEngine::stop_capture()
{
    if(m_in != nullptr){
        m_indev->disconnect();
        m_in->stop();
    }
}

extern "C" void setupAVAudioSession();
//extern "C" void setupPushKit();

void AudioEngine::start_playback() {
    m_outdev = m_out->start();
    qDebug() << "Playback started";
}


void AudioEngine::stop_playback()
{
    //m_outdev->reset();
    m_out->reset();
    m_out->stop();
    qDebug() << "AudioOut state Stop Playback";
    
}

void AudioEngine::input_data_received()
{
    QByteArray data = m_indev->readAll();

    if (data.size() > 0){
/*
        fprintf(stderr, "AUDIOIN: ");
        for(int i = 0; i < len; ++i){
            fprintf(stderr, "%02x ", (uint8_t)data.data()[i]);
        }
        fprintf(stderr, "\n");
        fflush(stderr);
*/
        const int byteCount = data.size();
        const int sampleCount = byteCount / 2;
        if (sampleCount <= 0) {
            return;
        }

        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data.constData());

        if (MACHAK && m_captureDeviceRate > 0 && m_captureDeviceRate != 8000) {
            // Proper downsampling: low-pass then linear resample to 8 kHz, continuous across buffers.
            const double step = static_cast<double>(m_captureDeviceRate) / 8000.0; // input samples per 1 output sample
            for (int n = 0; n < sampleCount; ++n) {
                const int i = n * 2;
                const uint16_t u = static_cast<uint16_t>(bytes[i]) | (static_cast<uint16_t>(bytes[i + 1]) << 8);
                const int16_t s = static_cast<int16_t>(u);
                float x = static_cast<float>(s);
                x = biquadProcess(m_capLowpass, x);

                const qint64 streamIndex = (m_capStreamIndex + 1);
                // Output samples that fall between (streamIndex-1) and streamIndex using linear interpolation
                while (m_capNextOutPos <= static_cast<double>(streamIndex)) {
                    const double frac = m_capNextOutPos - static_cast<double>(streamIndex - 1);
                    const float y = m_capPrevSample + static_cast<float>(frac) * (x - m_capPrevSample);
                    m_audioinq.enqueue(clampToInt16(y));
                    m_capNextOutPos += step;
                }

                m_capPrevSample = x;
                m_capStreamIndex = streamIndex;
            }
        } else {
            // Non-iOS/macOS, or device already at 8 kHz: decode samples directly.
            for (int n = 0; n < sampleCount; ++n) {
                const int i = n * 2;
                const uint16_t u = static_cast<uint16_t>(bytes[i]) | (static_cast<uint16_t>(bytes[i + 1]) << 8);
                m_audioinq.enqueue(static_cast<int16_t>(u));
            }
        }
    }
}

void AudioEngine::write(int16_t *pcm, size_t s)
{
    m_maxlevel = 0;
/*
    fprintf(stderr, "AUDIOOUT: ");
    for(int i = 0; i < s; ++i){
        fprintf(stderr, "%04x ", (uint16_t)pcm[i]);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
*/
    if(m_agc){
        process_audio(pcm, s);
    }

    const int inRate = 8000;
    if (m_outdev == nullptr) {
        return;
    }

    qsizetype bytesWritten = 0;
    qsizetype attemptedBytes = 0;
    if (m_playbackDeviceRate > 0 && m_playbackDeviceRate != inRate) {
        // Upsample 8 kHz -> device rate for smoother output when the sink doesn't accept 8 kHz.
        const double step = static_cast<double>(inRate) / static_cast<double>(m_playbackDeviceRate); // input samples per output sample (<1)
        // Conservative upper bound (+2 for interpolation safety)
        const int maxOut = static_cast<int>(std::ceil(static_cast<double>(s) * (static_cast<double>(m_playbackDeviceRate) / inRate))) + 2;
        QVector<int16_t> out;
        out.reserve(maxOut);

        for (size_t n = 0; n < s; ++n) {
            const float x = static_cast<float>(pcm[n]);
            const qint64 streamIndex = (m_playStreamIndex + 1);

            while (m_playNextOutPos <= static_cast<double>(streamIndex)) {
                const double frac = m_playNextOutPos - static_cast<double>(streamIndex - 1);
                const float y = m_playPrevSample + static_cast<float>(frac) * (x - m_playPrevSample);
                out.push_back(clampToInt16(y));
                m_playNextOutPos += step;
            }

            m_playPrevSample = x;
            m_playStreamIndex = streamIndex;
        }

        attemptedBytes = static_cast<qsizetype>(out.size() * sizeof(int16_t));
        bytesWritten = m_outdev->write(reinterpret_cast<const char *>(out.constData()), attemptedBytes);
    } else {
        attemptedBytes = static_cast<qsizetype>(sizeof(int16_t) * s);
        bytesWritten = m_outdev->write(reinterpret_cast<const char *>(pcm), attemptedBytes);
    }
    
  
    if (attemptedBytes > 0 && bytesWritten < attemptedBytes) {
        qDebug() << "AudioEngine::write underrun bytesAttempted=" << attemptedBytes
                 << "bytesWritten=" << bytesWritten
                 << "bytesFree=" << (m_out ? m_out->bytesFree() : 0)
                 << "bufferSize=" << (m_out ? m_out->bufferSize() : 0)
                 << "error=" << (m_out ? m_out->error() : QAudio::NoError);
    
    }

    for(uint32_t i = 0; i < s; ++i){
        if(pcm[i] > m_maxlevel){
            m_maxlevel = pcm[i];
        }
    }
}

uint16_t AudioEngine::read(int16_t *pcm, int s)
{
    m_maxlevel = 0;

    if(m_audioinq.size() >= s){
        for(int i = 0; i < s; ++i){
            pcm[i] = m_audioinq.dequeue();
            if(pcm[i] > m_maxlevel){
                m_maxlevel = pcm[i];
            }
        }
        return 1;
    }
    else if(m_in == nullptr){
        memset(pcm, 0, sizeof(int16_t) * s);
        return 1;
    }
    else{
        return 0;
    }
}

uint16_t AudioEngine::read(int16_t *pcm)
{
    int s;
    m_maxlevel = 0;

    if(m_audioinq.size() >= 160){
        s = 160;
    }
    else{
        s = m_audioinq.size();
    }

    for(int i = 0; i < s; ++i){
        pcm[i] = m_audioinq.dequeue();
        if(pcm[i] > m_maxlevel){
            m_maxlevel = pcm[i];
        }
    }

    return s;
}

// process_audio() based on code from DSD https://github.com/szechyjs/dsd
void AudioEngine::process_audio(int16_t *pcm, size_t s)
{
    float aout_abs, max, gainfactor, gaindelta, maxbuf;

    for(size_t i = 0; i < s; ++i){
        m_audio_out_temp_buf[i] = static_cast<float>(pcm[i]);
    }

    // detect max level
    max = 0;
    m_audio_out_temp_buf_p = m_audio_out_temp_buf;

    for (size_t i = 0; i < s; i++){
        aout_abs = fabsf(*m_audio_out_temp_buf_p);

        if (aout_abs > max){
            max = aout_abs;
        }

        m_audio_out_temp_buf_p++;
    }

    *m_aout_max_buf_p = max;
    m_aout_max_buf_p++;
    m_aout_max_buf_idx++;

    if (m_aout_max_buf_idx > 24){
        m_aout_max_buf_idx = 0;
        m_aout_max_buf_p = m_aout_max_buf;
    }

    // lookup max history
    for (size_t i = 0; i < 25; i++){
        maxbuf = m_aout_max_buf[i];

        if (maxbuf > max){
            max = maxbuf;
        }
    }

    // determine optimal gain level
    if (max > static_cast<float>(0)){
        gainfactor = (static_cast<float>(30000) / max);
    }
    else{
        gainfactor = static_cast<float>(50);
    }

    if (gainfactor < m_aout_gain){
        m_aout_gain = gainfactor;
        gaindelta = static_cast<float>(0);
    }
    else{
        if (gainfactor > static_cast<float>(50)){
            gainfactor = static_cast<float>(50);
        }

        gaindelta = gainfactor - m_aout_gain;

        if (gaindelta > (static_cast<float>(0.05) * m_aout_gain)){
            gaindelta = (static_cast<float>(0.05) * m_aout_gain);
        }
    }

    gaindelta /= static_cast<float>(s); //160

    // adjust output gain
    m_audio_out_temp_buf_p = m_audio_out_temp_buf;

    for (size_t i = 0; i < s; i++){
        *m_audio_out_temp_buf_p = (m_aout_gain + (static_cast<float>(i) * gaindelta)) * (*m_audio_out_temp_buf_p);
        m_audio_out_temp_buf_p++;
    }

    m_aout_gain += (static_cast<float>(s) * gaindelta);
    m_audio_out_temp_buf_p = m_audio_out_temp_buf;

    for (size_t i = 0; i < s; i++){
        *m_audio_out_temp_buf_p *= m_volume;
        if (*m_audio_out_temp_buf_p > static_cast<float>(32760)){
            *m_audio_out_temp_buf_p = static_cast<float>(32760);
        }
        else if (*m_audio_out_temp_buf_p < static_cast<float>(-32760)){
            *m_audio_out_temp_buf_p = static_cast<float>(-32760);
        }
        pcm[i] = static_cast<int16_t>(*m_audio_out_temp_buf_p);
        m_audio_out_temp_buf_p++;
    }
}

QString AudioEngine::getFriendlyName(const QString& deviceIdentifier) {
    if (deviceIdentifier == "default") {
        return "System Default";
    } else if (deviceIdentifier.contains("com.apple.airpods")) {
        return "AirPods";
    } else if (deviceIdentifier.contains("Bluetooth")) {
        return "Bluetooth Device";
    } else if (deviceIdentifier.contains("com.apple.avfoundation.avcapturedevice.built-in_audio")) {
        return "Built-in Microphone";
    } else if (deviceIdentifier.contains("com.apple.avfoundation.avcapturedevice.external_microphone")) {
        return "External Microphone";
    } else {
        return deviceIdentifier; // Fallback to original identifier if no match is found
    }
}


QString AudioEngine::mapFriendlyNameToDevice(const QString &friendlyName) {
    if (friendlyName == "System Default") {
        return "default";
    } else if (friendlyName == "AirPods") {
        return "com.apple.airpods";
    } else if (friendlyName.contains("Bluetooth")) {
        return "Bluetooth"; // General Bluetooth fallback
    } else {
        return friendlyName; // Return the original name if no mapping exists
    }
}


void AudioEngine::setOutputDevice(const QString &deviceName) {
    m_outputdevice = deviceName;
    if (m_out != nullptr) {
        stop_playback();
        init(); // Reinitialize the audio with the new output device
        start_playback();
    }
}

void AudioEngine::setInputDevice(const QString &deviceName) {
    m_inputdevice = deviceName;
    if (m_in != nullptr) {
        stop_capture();
        init(); // Reinitialize the audio with the new input device
        start_capture();
    }
}


void AudioEngine::handleStateChanged(QAudio::State newState)
{
    static bool isSessionActive = false;

    switch (newState) {
    case QAudio::ActiveState:
        qDebug() << "AudioOut state active";
        if (!isSessionActive) {
            setupAVAudioSession();
            isSessionActive = true;
            setPreferredInputDevice();
        }
        break;

    case QAudio::SuspendedState:
        qDebug() << "AudioOut state suspended";
        break;

    case QAudio::IdleState:
        qDebug() << "AudioOut state idle, renewing background task...";
        // IdleState can happen due to benign starvation; avoid reconfiguring AVAudioSession here.
        if (isAppInBackground()) {
            renewBackgroundTask();
        }
        break;

    case QAudio::StoppedState:
        qDebug() << "AudioOut state stopped";
            
        break;

    default:
        break;
    }
}
