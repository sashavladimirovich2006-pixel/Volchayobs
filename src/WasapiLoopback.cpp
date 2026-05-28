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
    m_pipeName.clear();
    // The worker closed the pipe handle in its cleanup() before
    // exiting; we just drop the (now-dangling) reference here.
    m_pipeHandle = nullptr;
    m_sampleRate.store(0, std::memory_order_release);
    m_channels.store(0, std::memory_order_release);
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
bool WasapiLoopback::startCapture(const QString&, bool) { return false; }

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
#include <vector>

namespace lumen {

namespace {

// Parameters handed to the worker thread. Bundled so the metering-only
// path (start()) and the streaming path (startCapture()) share one body.
struct CaptureParams {
    bool              loopback = true;  // true: render endpoint + LOOPBACK; false: capture mic
    bool              writePcm = false; // true: pump raw PCM into the named pipe
    HANDLE            pipe     = nullptr; // server end of the named pipe (already created)
    std::atomic<int>* outSampleRate = nullptr; // worker publishes mix format here
    std::atomic<int>* outChannels   = nullptr;
};

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

void runLoopbackThread(WasapiLoopback* owner, std::atomic<bool>* stopFlag,
                       CaptureParams params) {
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
        if (params.pipe) {
            // FlushFileBuffers blocks until the client drains the pipe;
            // skip it on a hard stop and just disconnect — the ffmpeg
            // client sees EOF and exits cleanly on its own.
            DisconnectNamedPipe(params.pipe);
            CloseHandle(params.pipe);
        }
        if (coOwned)    CoUninitialize();
    };

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                   CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   reinterpret_cast<void**>(&enumerator));
    if (FAILED(hr) || !enumerator) { cleanup(); return; }

    // Loopback mode hooks the default render endpoint (what speakers
    // play); capture mode hooks the default capture endpoint (a real
    // mic). Both use the eConsole role so we follow whatever the user
    // selected as their primary device in Windows Sound settings.
    const EDataFlow flow = params.loopback ? eRender : eCapture;
    hr = enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device);
    if (FAILED(hr) || !device) { cleanup(); return; }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(&client));
    if (FAILED(hr) || !client) { cleanup(); return; }

    hr = client->GetMixFormat(&mixFormat.ptr);
    if (FAILED(hr) || !mixFormat.ptr) { cleanup(); return; }

    // Publish the format so callers (StreamEngine) can build matching
    // ffmpeg `-ar`/`-ac` flags. Release-store pairs with the acquire-load
    // in WasapiLoopback::sampleRate()/channels().
    if (params.outSampleRate)
        params.outSampleRate->store(int(mixFormat.ptr->nSamplesPerSec),
                                    std::memory_order_release);
    if (params.outChannels)
        params.outChannels->store(int(mixFormat.ptr->nChannels),
                                  std::memory_order_release);

    // 200 ms loopback buffer gives the OS plenty of slack. Capture
    // endpoints take the same flag set minus LOOPBACK.
    constexpr REFERENCE_TIME kBufferDuration100ns = 2 * 1000 * 1000;
    const DWORD streamFlags = params.loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            streamFlags,
                            kBufferDuration100ns, 0, mixFormat.ptr, nullptr);
    if (FAILED(hr)) { cleanup(); return; }

    hr = client->GetService(__uuidof(IAudioCaptureClient),
                            reinterpret_cast<void**>(&capture));
    if (FAILED(hr) || !capture) { cleanup(); return; }

    // If we're pumping PCM, wait for the ffmpeg client to open the
    // other end before we start the WASAPI clock. ConnectNamedPipe
    // returns when the client connects (ERROR_PIPE_CONNECTED if it
    // beat us). We poll stopFlag in case the user hits Stop while we
    // wait, so a stuck ffmpeg launch can't pin the worker forever.
    if (params.writePcm && params.pipe) {
        // Overlapped connect lets us wake up on stopFlag without
        // blocking the thread inside ConnectNamedPipe.
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) { cleanup(); return; }
        BOOL connected = ConnectNamedPipe(params.pipe, &ov);
        DWORD lastErr = GetLastError();
        if (!connected && lastErr == ERROR_IO_PENDING) {
            // Wait in 100ms slices so Stop() interrupts within ~100ms.
            for (;;) {
                if (stopFlag->load(std::memory_order_acquire)) {
                    CancelIoEx(params.pipe, &ov);
                    CloseHandle(ov.hEvent);
                    cleanup();
                    return;
                }
                const DWORD w = WaitForSingleObject(ov.hEvent, 100);
                if (w == WAIT_OBJECT_0) break;
            }
        } else if (!connected && lastErr != ERROR_PIPE_CONNECTED) {
            CloseHandle(ov.hEvent);
            cleanup();
            return;
        }
        CloseHandle(ov.hEvent);
    }

    // Event reused across overlapped WriteFile calls in the pull loop.
    // Created lazily — only the streaming path needs it.
    HANDLE writeEvent = nullptr;
    if (params.writePcm && params.pipe) {
        writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!writeEvent) { cleanup(); return; }
    }
    auto closeWriteEvent = [&] {
        if (writeEvent) { CloseHandle(writeEvent); writeEvent = nullptr; }
    };

    hr = client->Start();
    if (FAILED(hr)) { closeWriteEvent(); cleanup(); return; }

    // Pull loop. We rate-limit emits to ~30 Hz so the meter doesn't
    // repaint faster than necessary. When writePcm is true we also
    // forward every captured chunk to the named pipe verbatim — that's
    // what the streaming mux ffmpeg reads as `-f f32le`.
    const UINT32 frameBytes = (mixFormat.ptr->wBitsPerSample / 8u)
                            * mixFormat.ptr->nChannels;
    constexpr int kPollMs = 20;
    double batchPeak = 0.0;
    int batchTicks = 0;
    bool pipeAlive = (params.pipe != nullptr);
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

            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            if (!silent) {
                const double p = peakFromBuffer(data, frames, mixFormat.ptr);
                if (p > batchPeak) batchPeak = p;
            }

            if (params.writePcm && pipeAlive && frames > 0 && frameBytes > 0) {
                // WASAPI flags AUDCLNT_BUFFERFLAGS_SILENT when no audio
                // was rendered in this period — ffmpeg still needs PCM
                // at the right rate, so we write zeros to keep the
                // timeline contiguous.
                const DWORD toWrite = DWORD(frames) * DWORD(frameBytes);
                const BYTE* src = data;
                static thread_local std::vector<BYTE> zeros;
                if (silent) {
                    if (zeros.size() < toWrite) zeros.assign(toWrite, 0);
                    src = zeros.data();
                }
                // Pipe was created with FILE_FLAG_OVERLAPPED, so every
                // WriteFile MUST hand it an OVERLAPPED struct or the
                // call is UB. We block on the completion event with a
                // 1s timeout — long enough to absorb a reader stall,
                // short enough that Stop() interrupts quickly.
                OVERLAPPED wov{};
                ResetEvent(writeEvent);
                wov.hEvent = writeEvent;
                DWORD written = 0;
                BOOL ok = WriteFile(params.pipe, src, toWrite, &written, &wov);
                if (!ok && GetLastError() == ERROR_IO_PENDING) {
                    const DWORD w = WaitForSingleObject(writeEvent, 1000);
                    if (w == WAIT_OBJECT_0) {
                        ok = GetOverlappedResult(params.pipe, &wov,
                                                 &written, FALSE);
                    } else {
                        // Reader hung — cancel and give up on the pipe.
                        CancelIoEx(params.pipe, &wov);
                        ok = FALSE;
                    }
                }
                if (!ok || written != toWrite) {
                    // ffmpeg client closed the pipe — stop pumping but
                    // keep metering for as long as the GUI wants it.
                    pipeAlive = false;
                }
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

    closeWriteEvent();
    cleanup();
}

} // namespace

bool WasapiLoopback::start() {
    stop();
    m_stop.store(false, std::memory_order_release);
    CaptureParams params;
    params.loopback = true;
    params.writePcm = false;
    params.pipe     = nullptr;
    params.outSampleRate = &m_sampleRate;
    params.outChannels   = &m_channels;
    m_thread = std::make_unique<std::thread>(
        runLoopbackThread, this, &m_stop, params);
    return true;
}

bool WasapiLoopback::startCapture(const QString& pipeName, bool loopback) {
    stop();
    if (pipeName.isEmpty()) return false;

    // Create the named-pipe server BEFORE we spin the worker so callers
    // can immediately point an ffmpeg client at the same name without a
    // race. We size the OS buffer for ~250 ms of 48k stereo float
    // (≈190KB) — large enough that a brief stutter on the reader side
    // doesn't block WriteFile.
    const std::wstring wname = pipeName.toStdWString();
    constexpr DWORD kPipeBufferBytes = 256 * 1024;
    HANDLE h = CreateNamedPipeW(
        wname.c_str(),
        PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1, // one instance — exactly one ffmpeg reader
        kPipeBufferBytes, kPipeBufferBytes,
        0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    m_pipeName   = pipeName;
    m_loopback   = loopback;
    m_pipeHandle = h;
    m_stop.store(false, std::memory_order_release);

    CaptureParams params;
    params.loopback = loopback;
    params.writePcm = true;
    params.pipe     = h;
    params.outSampleRate = &m_sampleRate;
    params.outChannels   = &m_channels;
    m_thread = std::make_unique<std::thread>(
        runLoopbackThread, this, &m_stop, params);
    return true;
}

} // namespace lumen

#endif // Q_OS_WIN
