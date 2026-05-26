#include "ThemeManager.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>

namespace lumen {

QString themeDisplayName(Theme t) {
    switch (t) {
        case Theme::Light:    return QObject::tr("Light");
        case Theme::Blackout: return QObject::tr("Blackout");
        case Theme::Rgb:      return QObject::tr("RGB");
    }
    return QString();
}

ThemeManager::ThemeManager(QApplication* app, QObject* parent)
    : QObject(parent), m_app(app) {
    // 60 Hz RGB hue cycling so the accent feels as smooth as the
    // 60 fps preview tick. setTimerType(PreciseTimer) so the gap doesn't
    // jitter when the GUI thread is busy.
    m_rgbTimer.setTimerType(Qt::PreciseTimer);
    m_rgbTimer.setInterval(1000 / 60);
    connect(&m_rgbTimer, &QTimer::timeout, this, &ThemeManager::onRgbTick);
}

void ThemeManager::applyTheme(Theme t) {
    m_theme = t;
    if (t == Theme::Rgb) {
        m_rgbTimer.start();
    } else {
        m_rgbTimer.stop();
        // Restore the user's static accent — m_accent gets clobbered by
        // every onRgbTick(), so we have to recover it from m_userAccent.
        m_accent = m_userAccent.isValid() ? m_userAccent : QColor(255, 153, 0);
    }
    rebuildStylesheet();
    emit themeChanged(t);
}

void ThemeManager::setAccent(const QColor& color) {
    if (!color.isValid()) return;
    m_userAccent = color;
    // Under RGB the displayed color is owned by the animation timer, so
    // we don't touch m_accent here — but we still persist the user's pick
    // so a later switch back to Light/Blackout restores it.
    if (m_theme != Theme::Rgb) {
        m_accent = color;
        rebuildStylesheet();
    }
    emit accentChanged(color);
}

void ThemeManager::onRgbTick() {
    // ~24°/sec at 60 Hz — matches the original 30Hz×1.5° cadence so we
    // don't speed up the colour wheel just because we tick more often.
    m_rgbHue += 0.4;
    if (m_rgbHue >= 360.0) m_rgbHue -= 360.0;
    m_accent = QColor::fromHsvF(m_rgbHue / 360.0, 0.85, 1.0);
    // Repainting QSS-styled widgets requires a full setStyleSheet, which
    // is expensive. Throttle the QSS rebuild to ~30 Hz; native-painted
    // widgets (PreviewWidget) just listen to accentChanged
    // and call update(), so they keep getting the new colour every tick.
    ++m_rgbStyleSkip;
    if (m_rgbStyleSkip >= 2) {
        m_rgbStyleSkip = 0;
        rebuildStylesheet();
    }
    emit accentChanged(m_accent);
}

QString ThemeManager::loadQssTemplate(Theme t) const {
    const char* path = ":/resources/themes/blackout.qss";
    switch (t) {
        case Theme::Light:    path = ":/resources/themes/light.qss"; break;
        case Theme::Blackout: path = ":/resources/themes/blackout.qss"; break;
        case Theme::Rgb:      path = ":/resources/themes/rgb.qss"; break;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    return QString::fromUtf8(f.readAll());
}

void ThemeManager::rebuildStylesheet() {
    QString qss = loadQssTemplate(m_theme);
    if (qss.isEmpty()) return;

    auto rgbStr = [](const QColor& c) {
        return QString("rgb(%1, %2, %3)").arg(c.red()).arg(c.green()).arg(c.blue());
    };
    auto rgbaStr = [](const QColor& c, qreal a) {
        return QString("rgba(%1, %2, %3, %4)")
            .arg(c.red()).arg(c.green()).arg(c.blue())
            .arg(int(a * 255));
    };

    QColor accentDim = m_accent.darker(140);
    QColor accentSoft = m_accent.lighter(120);

    qss.replace(QStringLiteral("{{ACCENT}}"),       rgbStr(m_accent));
    qss.replace(QStringLiteral("{{ACCENT_DIM}}"),   rgbStr(accentDim));
    qss.replace(QStringLiteral("{{ACCENT_SOFT}}"),  rgbStr(accentSoft));
    qss.replace(QStringLiteral("{{ACCENT_GHOST}}"), rgbaStr(m_accent, 0.18));

    if (m_app) m_app->setStyleSheet(qss);
}

} // namespace lumen
