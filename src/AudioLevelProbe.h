#pragma once

#include "Source.h"
#include "WasapiLoopback.h"

#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QObject>
#include <QPointer>

namespace lumen {

// Subscribes to PCM samples from the host audio system via Qt's
// QAudioSource and emits rolling peak/RMS levels in dBFS. No external
// process is involved, so the meter works on a stock Windows install
// without ffmpeg or any other helper binary.
//
// If the configured source isn't a microphone / desktop-audio entry,
// is disabled, or doesn't match any QMediaDevices input, the probe
// stops itself and the meter stays at -infinity.
class AudioLevelProbe : public QObject {
    Q_OBJECT
public:
    explicit AudioLevelProbe(QObject* parent = nullptr);
    ~AudioLevelProbe() override;

    // Start probing the given audio source. Subsequent calls stop any
    // existing capture before re-arming with the new device.
    void start(const Source& s);

    // Stop the probe and emit one final silent level so the meter
    // visually settles back to -inf.
    void stop();

signals:
    // Emitted ~30 Hz with the current peak in dBFS (0 = full scale,
    // -60 = floor we render).
    void levelChanged(double db);

private slots:
    void onReadyRead();

private:
    QPointer<QAudioSource> m_source;
    QIODevice*             m_io = nullptr;
    QAudioFormat           m_format;
    WasapiLoopback*        m_loopback = nullptr;
};

} // namespace lumen
