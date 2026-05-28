#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <thread>

namespace lumen {

// Captures PCM from a Windows audio endpoint via WASAPI. It serves two
// distinct jobs that share one capture thread:
//
//  1. Level metering (start()): digests the samples into a rolling peak
//     in dBFS and emits it from the GUI thread so the existing
//     AudioLevelMeter widgets can consume it directly.
//
//  2. PCM pumping (startCapture()): in addition to metering, writes the
//     raw interleaved samples (in the endpoint's mix format, normally
//     32-bit float) to a Windows named pipe. The streaming pipeline
//     points an ffmpeg `-f f32le -i \\.\pipe\<name>` input at that pipe,
//     so stream audio bypasses ffmpeg-dshow entirely. dshow capture
//     devices stamp packets with the device's *uptime* (a ~5-hour
//     absolute offset on a long-running box), which froze the mux while
//     it tried to sync that against a zero-based video pipe. WASAPI hands
//     us near-zero-based timestamps, so the offset — and the stalls it
//     caused — simply don't exist on this path.
//
// On non-Windows builds the class compiles to a no-op stub so callers
// don't need to gate every signal/slot connection — start()/startCapture()
// return false and `levelChanged` never fires. Those platforms have their
// own loopback story (PulseAudio monitor sources, AVFoundation system
// audio) which the rest of the pipeline picks up through dshow/pulse.
class WasapiLoopback : public QObject {
    Q_OBJECT
public:
    explicit WasapiLoopback(QObject* parent = nullptr);
    ~WasapiLoopback() override;

    // Metering-only mode: capture from the default render endpoint
    // (loopback) and emit levelChanged. No PCM is written anywhere.
    // Returns true if a capture thread was spun up. Calling start()
    // twice stops the previous thread before re-arming.
    bool start();

    // Streaming mode: capture from an endpoint and pump raw PCM into the
    // named pipe `pipeName` (full Win32 form, e.g. \\.\pipe\volchay-...),
    // while still emitting levelChanged. When `loopback` is true we hook
    // the default *render* endpoint with AUDCLNT_STREAMFLAGS_LOOPBACK
    // (desktop audio); when false we open the default *capture* endpoint
    // (a real microphone). Returns false on non-Windows or if the WASAPI
    // chain or pipe server couldn't be created.
    //
    // The pipe server is created on the calling thread before the worker
    // starts, so sampleRate()/channels() are valid as soon as this
    // returns true. The worker blocks on ConnectNamedPipe until ffmpeg
    // (the client) opens the other end.
    bool startCapture(const QString& pipeName, bool loopback);

    // Cleanly stop the worker, close the pipe, and emit one silent level
    // so the meter settles back to -inf.
    void stop();

    // Actual endpoint mix format, valid after a successful startCapture().
    // Used to build the matching ffmpeg `-ar`/`-ac` input flags. Both
    // return 0 before capture has been armed.
    int sampleRate() const { return m_sampleRate.load(std::memory_order_acquire); }
    int channels()   const { return m_channels.load(std::memory_order_acquire); }

signals:
    // Emitted ~30 Hz with the peak (in dBFS) of every PCM chunk we
    // pulled from the endpoint since the last emit.
    void levelChanged(double db);

private:
    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool>            m_stop{false};

    // Streaming-mode state. m_pipeName is empty in metering-only mode.
    QString                      m_pipeName;
    bool                         m_loopback = true;
    std::atomic<int>             m_sampleRate{0};
    std::atomic<int>             m_channels{0};
    // Win32 HANDLE for the named-pipe server, stored as void* to keep
    // <windows.h> out of this header. Owned by the worker thread.
    void*                        m_pipeHandle = nullptr;
};

} // namespace lumen
