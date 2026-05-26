#pragma once

#include <QWidget>

namespace lumen {

// OBS-style horizontal level meter. Accepts peak levels in dBFS (where
// 0 dB is full scale and -60 dB is the floor) via `setLevelDb()` and
// paints a green→yellow→red bar plus a slowly-decaying peak indicator.
//
// The widget never starts its own audio probe — it's a pure paint
// surface. `AudioLevelProbe` (or another producer) feeds it dB values
// at whatever rate is available.
class AudioLevelMeter : public QWidget {
    Q_OBJECT
public:
    explicit AudioLevelMeter(QWidget* parent = nullptr);

    // Convenience for callers that only have a 0..1 RMS — converts to dB
    // and forwards to setLevelDb().
    void setLevel(double rms01);

    // Set the current peak level in dBFS. -inf, NaN and values below the
    // floor are treated as "silent".
    void setLevelDb(double db);

    // Drop level to silence (-inf). Used when a strip is muted or the
    // associated probe stops emitting.
    void clear();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* e) override;
    void timerEvent(QTimerEvent* e) override;

private:
    // Decay both the live and the peak indicator towards the floor on
    // every tick so a momentary peak still leaves a visible mark.
    double m_db = -120.0;      // current level
    double m_peakDb = -120.0;  // sliding peak indicator
    int    m_peakHoldTicks = 0;
    int    m_timerId = 0;
};

} // namespace lumen
