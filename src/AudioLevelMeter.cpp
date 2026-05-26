#include "AudioLevelMeter.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPalette>
#include <QTimerEvent>

#include <cmath>

namespace lumen {

namespace {

constexpr double kFloorDb = -60.0;
constexpr double kCeilDb  =   0.0;

double clampDb(double v) {
    if (!std::isfinite(v) || v <= kFloorDb) return kFloorDb;
    if (v >= kCeilDb) return kCeilDb;
    return v;
}

double dbToFraction(double db) {
    db = clampDb(db);
    return (db - kFloorDb) / (kCeilDb - kFloorDb);
}

} // namespace

AudioLevelMeter::AudioLevelMeter(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    // ~30 fps decay tick. Cheap — just nudges the value and repaints
    // a single thin strip.
    m_timerId = startTimer(33);
}

void AudioLevelMeter::setLevel(double rms01) {
    if (rms01 <= 0.0 || !std::isfinite(rms01)) {
        setLevelDb(kFloorDb);
        return;
    }
    setLevelDb(20.0 * std::log10(rms01));
}

void AudioLevelMeter::setLevelDb(double db) {
    db = clampDb(db);
    // Snap up to a new high immediately, decay only on the timer.
    if (db > m_db) m_db = db;
    if (db > m_peakDb) {
        m_peakDb = db;
        m_peakHoldTicks = 30; // ~1 s hold before the peak starts falling
    }
    update();
}

void AudioLevelMeter::clear() {
    m_db = kFloorDb;
    m_peakDb = kFloorDb;
    m_peakHoldTicks = 0;
    update();
}

QSize AudioLevelMeter::sizeHint() const {
    return QSize(160, 12);
}

QSize AudioLevelMeter::minimumSizeHint() const {
    return QSize(60, 10);
}

void AudioLevelMeter::timerEvent(QTimerEvent* e) {
    if (e->timerId() != m_timerId) {
        QWidget::timerEvent(e);
        return;
    }
    // Decay ~30 dB per second on the live bar, ~12 dB per second on
    // the peak marker (after its hold expires) — matches the visual
    // pacing OBS uses for its mixer meters.
    constexpr double kLiveDecayDbPerTick = 1.0;
    constexpr double kPeakDecayDbPerTick = 0.4;
    if (m_db > kFloorDb) {
        m_db -= kLiveDecayDbPerTick;
        if (m_db < kFloorDb) m_db = kFloorDb;
    }
    if (m_peakHoldTicks > 0) {
        --m_peakHoldTicks;
    } else if (m_peakDb > kFloorDb) {
        m_peakDb -= kPeakDecayDbPerTick;
        if (m_peakDb < kFloorDb) m_peakDb = kFloorDb;
    }
    update();
}

void AudioLevelMeter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect r = rect();
    if (r.width() <= 0 || r.height() <= 0) return;

    // Track (background). Faint neutral grey via rgba so it reads as
    // a subtle rail on every theme — previously we pulled from
    // palette(WindowText), which on Blackout/RGB is near-white and
    // produced an obvious "white stripe" that competed with the live
    // gradient fill (Screenshot_7).
    p.fillRect(r, QColor(128, 128, 128, 60));

    // Filled portion — green/yellow/red gradient matching OBS.
    const double live = dbToFraction(m_db);
    if (live > 0.0) {
        const int fillW = int(r.width() * live + 0.5);
        QRect fillRect(r.x(), r.y(), fillW, r.height());
        QLinearGradient g(r.topLeft(), r.topRight());
        g.setColorAt(0.00, QColor("#2ecc71")); // green
        g.setColorAt(0.65, QColor("#f1c40f")); // yellow around -21 dB
        g.setColorAt(0.85, QColor("#e67e22")); // orange around  -9 dB
        g.setColorAt(1.00, QColor("#e74c3c")); // red around 0 dB
        p.fillRect(fillRect, QBrush(g));
    }

    // Sliding peak indicator — thin 2 px vertical bar in the brand
    // amber so it stays visible on top of both the grey rail and the
    // green/yellow/red fill gradient.
    const double peak = dbToFraction(m_peakDb);
    if (peak > 0.0) {
        const int px = int(r.x() + r.width() * peak - 1.0);
        QRect peakRect(px, r.y(), 2, r.height());
        p.fillRect(peakRect, QColor(0xFF, 0x99, 0x00));
    }
}

} // namespace lumen
