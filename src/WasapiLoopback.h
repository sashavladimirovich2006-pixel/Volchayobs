#pragma once

#include <QObject>

#include <atomic>
#include <memory>
#include <thread>

namespace lumen {

// Captures PCM from the default Windows audio render endpoint via
// WASAPI loopback (AUDCLNT_STREAMFLAGS_LOOPBACK). The raw samples are
// digested into a rolling peak in dBFS and emitted from the GUI thread
// so the existing AudioLevelMeter widgets can consume them directly.
//
// On non-Windows builds the class compiles to a no-op stub so callers
// don't need to gate every signal/slot connection — `start()` simply
// does nothing and `levelChanged` never fires.
class WasapiLoopback : public QObject {
    Q_OBJECT
public:
    explicit WasapiLoopback(QObject* parent = nullptr);
    ~WasapiLoopback() override;

    // Start capturing from the default render endpoint. Returns true
    // if a capture thread was spun up. Calling start() twice stops the
    // previous thread before re-arming.
    bool start();

    // Cleanly stop the worker and emit one silent level so the meter
    // settles back to -inf.
    void stop();

signals:
    // Emitted ~30 Hz with the peak (in dBFS) of every PCM chunk we
    // pulled from the loopback endpoint since the last emit.
    void levelChanged(double db);

private:
    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool>            m_stop{false};
};

} // namespace lumen
