#include "WasapiLoopback.h"

#include <QMetaObject>
#include <QPointer>

namespace lumen {

WasapiLoopback::WasapiLoopback(QObject* parent) : QObject(parent) {}

WasapiLoopback::~WasapiLoopback() { stop(); }

void WasapiLoopback::stop() {
    m_stop.store(true, std::memory_order_release);
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    m_thread.reset();
    emit levelChanged(-120.0);
}

} // namespace lumen

#ifndef Q_OS_WIN

namespace lumen {

// Non-Windows stub. The host platforms have their own loopback
// mechanism (pulseaudio monitor sources, AVFoundation system audio)
// which the rest of the pipeline picks up through QAudioSource, so we
// never spin a thread here.
bool WasapiLoopback::start() { return false; }

} // namespace lumen

#else // Q_OS_WIN

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <objbase.h>
#include <cstdint>
#include <cmath>
#include <chrono>

namespace lumen {

namespace {

// CoTaskMemFree-aware unique pointer for WAVEFORMATEX out-params.
struct WaveFormatGuard {
    WAVEFORMATEX* ptr = nullptr;
    ~WaveFormatGuard() { if (ptr) CoTaskMemFree(ptr); }
};

// Compute peak amplitude (0..1) of an interleaved PCM/IEEE-float
// buffer. WASAPI gives us the shared mix format, which is float32 by
// default on every Windows version we care about (Vista+).
double peakFromBuffer(const BYTE* data, UINT32 frames, const WAVEFORMATEX* fmt) {
    if (!data || frames == 0 || !fmt) return 0.0;
    const int channels = fmt->nChannels;
    if (channels <= 0) return 0.0;
    const UINT32 samples = frames * channels;

    const bool isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt)->SubFormat ==
             KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);

    if (isFloat) {
        const float* s = reinterpret_cast<const float*>(data);
        double peak = 0.0;
        for (UINT32 i = 0; i < samples; ++i) {
            const double a = std::fabs(double(s[i]));
            if (a > peak) peak = a;
        }
        return peak;
    }

    if (fmt->wFormatTag == WAVE_FORMAT_PCM && fmt->wBitsPerSample == 16) {
        const int16_t* s = reinterpret_cast<const int16_t*>(data);
        int32_t peak = 0;
        for (UINT32 i = 0; i < samples; ++i) {
            int32_t a = s[i];
            if (a < 0) a = -a;
            if (a > peak) peak = a;
        }
        return double(peak) / 32768.0;
    }

    if (fmt->wFormatTag == WAVE_FORMAT_PCM && fmt->wBitsPerSample == 32) {
        const int32_t* s = reinterpret_cast<const int32_t*>(data);
        int64_t peak = 0;
        for (UINT32 i = 0; i < samples; ++i) {
            int64_t a = s[i];
            if (a < 0) a = -a;
            if (a > peak) peak = a;
        }
        return double(peak) / 2147483648.0;
    }

    return 0.0;
}

void runLoopbackThread(WasapiLoopback* owner, std::atomic<bool>* stopFlag) {
    QPointer<WasapiLoopback> safeOwner(owner);

    auto emitLevel = [&](double db) {
        if (!safeOwner) return;
        QMetaObject::invokeMethod(safeOwner.data(),
            [target = safeOwner, db]() {
                if (target) emit target->levelChanged(db);
            }, Qt::QueuedConnection);
    };

    const HRESULT coRes = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool coOwned = (coRes == S_OK || coRes == S_FALSE);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice*           device     = nullptr;
    IAudioClient*        client     = nullptr;
    IAudioCaptureClient* capture    = nullptr;
    WaveFormatGuard      mixFormat;

    auto cleanup = [&] {
        if (capture)    capture->Release();
        if (client) {
            client->Stop();
            client->Release();
        }
        if (device)     device->Release();
        if (enumerator) enumerator->Release();
        if (coOwned)    CoUninitialize();
    };

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                   CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) { cleanup(); return; }

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr) || !device) { cleanup(); return; }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&client));
    if (FAILED(hr) || !client) { cleanup(); return; }

    hr = client->GetMixFormat(&mixFormat.ptr);
    if (FAILED(hr) || !mixFormat.ptr) { cleanup(); return; }

    // 200 ms loopback buffer gives the OS plenty of slack.
    constexpr REFERENCE_TIME kBufferDuration100ns = 2 * 1000 * 1000;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            AUDCLNT_STREAMFLAGS_LOOPBACK,
                            kBufferDuration100ns, 0, mixFormat.ptr, nullptr);
    if (FAILED(hr)) { cleanup(); return; }

    hr = client->GetService(__uuidof(IAudioCaptureClient),
                            reinterpret_cast<void**>(&capture));
    if (FAILED(hr) || !capture) { cleanup(); return; }

    hr = client->Start();
    if (FAILED(hr)) { cleanup(); return; }

    // Pull loop. We rate-limit emits to ~30 Hz so the meter doesn't
    // repaint faster than necessary.
    constexpr int kPollMs = 20;
    double batchPeak = 0.0;
    int batchTicks = 0;
    while (!stopFlag->load(std::memory_order_acquire)) {
        UINT32 packetSize = 0;
        hr = capture->GetNextPacketSize(&packetSize);
        if (FAILED(hr)) break;

        while (packetSize != 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr)) { packetSize = 0; break; }

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                const double p = peakFromBuffer(data, frames, mixFormat.ptr);
                if (p > batchPeak) batchPeak = p;
            }
            capture->ReleaseBuffer(frames);
            hr = capture->GetNextPacketSize(&packetSize);
            if (FAILED(hr)) break;
        }

        ++batchTicks;
        if (batchTicks * kPollMs >= 33) {
            double db;
            if (batchPeak <= 1.0e-6) db = -120.0;
            else                     db = 20.0 * std::log10(batchPeak);
            emitLevel(db);
            batchPeak = 0.0;
            batchTicks = 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }

    cleanup();
}

} // namespace

bool WasapiLoopback::start() {
    stop();
    m_stop.store(false, std::memory_order_release);
    m_thread = std::make_unique<std::thread>(runLoopbackThread, this, &m_stop);
    return true;
}

} // namespace lumen

#endif // Q_OS_WIN
