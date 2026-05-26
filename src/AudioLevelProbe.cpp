#include "AudioLevelProbe.h"

#include <QAudioDevice>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lumen {

namespace {

// Match a stored device id/label against the platform's QMediaDevices
// list. We saved the device description in the source settings, which
// is exactly the friendly name Qt also reports — but we also fall back
// to substring matches so saved dshow names ("Microphone (Realtek)")
// still resolve to the corresponding WASAPI device.
//
// Returns a null QAudioDevice when nothing matches. We deliberately do
// not silently fall back to the default input here, because that makes
// a desktop-audio strip whose device (e.g. virtual-audio-capturer) Qt
// can't reach light up with mic levels — exactly the misleading
// behaviour the user hit.
QAudioDevice resolveAudioDevice(const Source& s) {
    const QString wantId    = s.settings.value(SourceFields::AUDIO_DEVICE_ID).toString();
    const QString wantLabel = s.settings.value(SourceFields::AUDIO_DEVICE_LABEL).toString();

    // Brand-new microphone strips with no chosen device yet — let the
    // user see levels from whatever the OS considers the default input
    // so the meter feels "alive" before they open Configure.
    if (wantId.trimmed().isEmpty() && wantLabel.trimmed().isEmpty()) {
        if (s.type == SourceType::Microphone) return QMediaDevices::defaultAudioInput();
        return {};
    }

    const QList<QAudioDevice> all = QMediaDevices::audioInputs();
    if (all.isEmpty()) return {};

    auto exact = [&](const QString& needle) -> QAudioDevice {
        if (needle.isEmpty()) return {};
        for (const QAudioDevice& d : all) {
            if (d.description().compare(needle, Qt::CaseInsensitive) == 0) return d;
            if (QString::fromUtf8(d.id()).compare(needle, Qt::CaseInsensitive) == 0) return d;
        }
        return {};
    };
    auto partial = [&](const QString& needle) -> QAudioDevice {
        if (needle.isEmpty()) return {};
        for (const QAudioDevice& d : all) {
            if (d.description().contains(needle, Qt::CaseInsensitive)) return d;
        }
        return {};
    };

    QAudioDevice hit = exact(wantId);
    if (!hit.isNull()) return hit;
    hit = exact(wantLabel);
    if (!hit.isNull()) return hit;
    hit = partial(wantId);
    if (!hit.isNull()) return hit;
    hit = partial(wantLabel);
    if (!hit.isNull()) return hit;

    // No match. Returning null leaves the meter at -inf, which is
    // correct: we don't know how to capture from this device through
    // Qt's WASAPI/PulseAudio backends. Configurations that need a
    // ffmpeg-only loopback (Windows virtual-audio-capturer) end up
    // here on purpose.
    return {};
}

// Compute peak amplitude from an interleaved PCM buffer in [-1, 1].
double peakFloat(const float* samples, qsizetype count) {
    double peak = 0.0;
    for (qsizetype i = 0; i < count; ++i) {
        const double a = std::fabs(double(samples[i]));
        if (a > peak) peak = a;
    }
    return peak;
}

double peakInt16(const int16_t* samples, qsizetype count) {
    int32_t peak = 0;
    for (qsizetype i = 0; i < count; ++i) {
        const int32_t a = std::abs(int32_t(samples[i]));
        if (a > peak) peak = a;
    }
    return double(peak) / 32768.0;
}

double peakInt32(const int32_t* samples, qsizetype count) {
    int64_t peak = 0;
    for (qsizetype i = 0; i < count; ++i) {
        int64_t a = samples[i];
        if (a < 0) a = -a;
        if (a > peak) peak = a;
    }
    return double(peak) / 2147483648.0;
}

double peakUInt8(const uint8_t* samples, qsizetype count) {
    int peak = 0;
    for (qsizetype i = 0; i < count; ++i) {
        const int a = std::abs(int(samples[i]) - 128);
        if (a > peak) peak = a;
    }
    return double(peak) / 128.0;
}

} // namespace

AudioLevelProbe::AudioLevelProbe(QObject* parent) : QObject(parent) {}

AudioLevelProbe::~AudioLevelProbe() {
    stop();
}

void AudioLevelProbe::start(const Source& s) {
    stop();
    if (s.type != SourceType::Microphone && s.type != SourceType::DesktopAudio)
        return;
    if (!s.enabled) return;

#ifdef Q_OS_WIN
    // Desktop audio on Windows: route through native WASAPI loopback.
    // Qt's QAudioSource only sees recording endpoints (mics, Stereo Mix
    // if enabled), which means a stock install never gets a moving
    // meter for what the speakers are playing. WASAPI loopback hooks
    // the default render endpoint directly and works without any extra
    // driver or filter installed.
    if (s.type == SourceType::DesktopAudio) {
        m_loopback = new WasapiLoopback(this);
        connect(m_loopback, &WasapiLoopback::levelChanged,
                this, &AudioLevelProbe::levelChanged);
        m_loopback->start();
        return;
    }
#endif

    const QAudioDevice device = resolveAudioDevice(s);
    if (device.isNull()) return;

    // Pick a format the device can actually deliver. We don't care
    // about fidelity here — only peaks for visualisation — so let Qt
    // choose its preferred format and only override channel count to
    // keep the math simple.
    QAudioFormat fmt = device.preferredFormat();
    if (!device.isFormatSupported(fmt)) {
        fmt.setSampleRate(44100);
        fmt.setChannelCount(1);
        fmt.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(fmt)) return;
    }
    m_format = fmt;

    m_source = new QAudioSource(device, fmt, this);
    // Keep the buffer small (~30 ms) so we get fresh peaks ~30 times
    // a second, matching the meter's repaint cadence.
    const int frameBytes = fmt.bytesPerFrame();
    if (frameBytes > 0) {
        m_source->setBufferSize(qMax(frameBytes,
            (fmt.sampleRate() / 30) * frameBytes));
    }

    m_io = m_source->start();
    if (!m_io) {
        delete m_source;
        m_source = nullptr;
        return;
    }
    connect(m_io, &QIODevice::readyRead, this, &AudioLevelProbe::onReadyRead);
}

void AudioLevelProbe::stop() {
    if (m_loopback) {
        m_loopback->stop();
        m_loopback->deleteLater();
        m_loopback = nullptr;
    }
    if (m_source) {
        m_source->stop();
        m_source->deleteLater();
        m_source = nullptr;
    }
    m_io = nullptr;
    emit levelChanged(-120.0);
}

void AudioLevelProbe::onReadyRead() {
    if (!m_io || !m_source) return;
    const QByteArray buf = m_io->readAll();
    if (buf.isEmpty()) return;

    double peak = 0.0;
    switch (m_format.sampleFormat()) {
        case QAudioFormat::Float: {
            peak = peakFloat(reinterpret_cast<const float*>(buf.constData()),
                             buf.size() / qsizetype(sizeof(float)));
            break;
        }
        case QAudioFormat::Int16: {
            peak = peakInt16(reinterpret_cast<const int16_t*>(buf.constData()),
                             buf.size() / qsizetype(sizeof(int16_t)));
            break;
        }
        case QAudioFormat::Int32: {
            peak = peakInt32(reinterpret_cast<const int32_t*>(buf.constData()),
                             buf.size() / qsizetype(sizeof(int32_t)));
            break;
        }
        case QAudioFormat::UInt8: {
            peak = peakUInt8(reinterpret_cast<const uint8_t*>(buf.constData()),
                             buf.size());
            break;
        }
        default:
            return;
    }

    if (peak <= 1.0e-6) {
        emit levelChanged(-120.0);
        return;
    }
    const double db = 20.0 * std::log10(peak);
    emit levelChanged(db);
}

} // namespace lumen
