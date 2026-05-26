#include "PreviewWidget.h"

#include "ThemeManager.h"

#include <QCursor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygon>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>

#if defined(VOLCHAY_HAVE_WEBENGINE)
#  include <QWebEngineSettings>
#  include <QWebEngineView>
#  include <QWebEnginePage>
#endif

namespace lumen {

// BrowserLayer keeps one off-screen QWebEngineView per Browser source.
// Reloads the URL only when settings actually change; otherwise just
// returns the current rendered QPixmap on every preview tick.
//
// When QtWebEngine isn't linked (e.g. headless smoke build) we keep a
// stub that renders a friendly placeholder so the source still shows
// up as a video layer with the right transform.
class BrowserLayer : public QObject {
    // Intentionally no Q_OBJECT: we don't expose signals/slots, all
    // connections use lambdas which only need QObject's connect()
    // overload, not moc-generated metadata. Skipping Q_OBJECT also
    // avoids needing a manual #include "PreviewWidget.moc" line.
public:
    explicit BrowserLayer(QObject* parent = nullptr) : QObject(parent) {
#if defined(VOLCHAY_HAVE_WEBENGINE)
        m_view = new QWebEngineView();
        // Keep the view off-screen but sized so the engine actually
        // lays the page out. Showing it would steal focus.
        m_view->setAttribute(Qt::WA_DontShowOnScreen);
        m_view->resize(1280, 720);
        m_view->show();
        if (auto* page = m_view->page()) {
            // Transparent background so widget overlays composite
            // cleanly over scene below.
            page->setBackgroundColor(Qt::transparent);
            auto* settings = page->settings();
            settings->setAttribute(QWebEngineSettings::ShowScrollBars, false);
            settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
            settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);
            QObject::connect(page, &QWebEnginePage::loadFinished,
                             this, [this](bool ok) {
                if (!ok) return;
                if (!m_customCss.isEmpty()) injectCss(m_customCss);
            });
        }
#endif
    }

    ~BrowserLayer() override {
#if defined(VOLCHAY_HAVE_WEBENGINE)
        delete m_view;
        m_view = nullptr;
#endif
    }

    // Update URL/size/css. No-op if nothing actually changed.
    void configure(const QString& url, int w, int h,
                   int refreshSec, const QString& css) {
#if defined(VOLCHAY_HAVE_WEBENGINE)
        if (!m_view) return;
        const QSize sz(qMax(64, w), qMax(64, h));
        if (m_view->size() != sz) m_view->resize(sz);
        if (m_customCss != css) {
            m_customCss = css;
            if (!css.isEmpty()) injectCss(css);
        }
        if (m_url != url) {
            m_url = url;
            if (!url.isEmpty()) m_view->load(QUrl(url));
        }
        if (m_refreshSec != refreshSec) {
            m_refreshSec = refreshSec;
            if (m_reloadTimer) { m_reloadTimer->stop(); delete m_reloadTimer; m_reloadTimer = nullptr; }
            if (refreshSec > 0) {
                m_reloadTimer = new QTimer(this);
                m_reloadTimer->setInterval(refreshSec * 1000);
                QObject::connect(m_reloadTimer, &QTimer::timeout, this,
                                 [this] { if (m_view) m_view->reload(); });
                m_reloadTimer->start();
            }
        }
#else
        Q_UNUSED(url); Q_UNUSED(w); Q_UNUSED(h);
        Q_UNUSED(refreshSec); Q_UNUSED(css);
#endif
    }

    QString url() const { return m_url; }

    // Grab the current page into a QPixmap. Returns null if the engine
    // isn't ready / not linked — caller falls back to a text note.
    QPixmap grab() const {
#if defined(VOLCHAY_HAVE_WEBENGINE)
        if (!m_view) return {};
        return m_view->grab();
#else
        return {};
#endif
    }

private:
#if defined(VOLCHAY_HAVE_WEBENGINE)
    void injectCss(const QString& css) {
        if (!m_view) return;
        auto* page = m_view->page();
        if (!page) return;
        const QString escaped = QString(css).replace("\\", "\\\\")
                                            .replace("`",  "\\`");
        const QString js = QStringLiteral(
            "(function(){var s=document.getElementById('volchay-css');"
            "if(!s){s=document.createElement('style');"
            "s.id='volchay-css';document.head.appendChild(s);}"
            "s.textContent=`%1`;})();").arg(escaped);
        page->runJavaScript(js);
    }
    QWebEngineView* m_view = nullptr;
#endif
    QString m_url;
    QString m_customCss;
    int     m_refreshSec = 0;
    QTimer* m_reloadTimer = nullptr;
};

namespace {

QPixmap renderColorPreview(const Source& s, QSize widgetSize) {
    QColor c(s.settings.value(SourceFields::COLOR_RGB,
                               QStringLiteral("#202020")).toString());
    if (!c.isValid()) c = QColor("#202020");
    QPixmap pm(widgetSize);
    pm.fill(c);
    return pm;
}

QPixmap renderTextPreview(const Source& s, QSize size) {
    QColor bg(s.settings.value(SourceFields::TEXT_BG_COLOR,
                                QStringLiteral("#000000")).toString());
    QColor fg(s.settings.value(SourceFields::TEXT_COLOR,
                                QStringLiteral("#ffffff")).toString());
    if (!bg.isValid()) bg = Qt::black;
    if (!fg.isValid()) fg = Qt::white;
    QPixmap pm(size);
    pm.fill(bg);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont f = p.font();
    int ptSize = s.settings.value(SourceFields::TEXT_SIZE, 48).toInt();
    f.setPointSize(qMax(8, ptSize / 2));   // shrink for preview
    p.setFont(f);
    p.setPen(fg);
    QString body = s.settings.value(SourceFields::TEXT_BODY).toString();
    if (body.isEmpty()) body = QObject::tr("(empty text)");
    p.drawText(pm.rect(), Qt::AlignCenter | Qt::TextWordWrap, body);
    return pm;
}

// Overlay a classic arrow-cursor at `posOnPixmap` on top of `pm`.
// Used to compensate for Qt's QScreen::grabWindow() and ffmpeg's
// default desktop-grab pipelines, both of which omit the OS cursor —
// users expect to see where they're pointing in the preview/stream.
void overlayCursor(QPixmap& pm, QPointF posOnPixmap, qreal scale) {
    // Simple 12-point cursor polygon (matches the standard left-pointing
    // arrow). Drawn at `scale` so it stays a consistent visual size when
    // the screen capture is downscaled into the preview pixmap.
    static const QPoint kArrow[] = {
        QPoint( 0,  0),  QPoint( 0, 16),  QPoint( 4, 12),
        QPoint( 7, 18),  QPoint( 9, 17),  QPoint( 6, 11),
        QPoint(11, 11),
    };
    QPolygonF poly;
    for (const QPoint& p : kArrow) {
        poly << QPointF(posOnPixmap.x() + p.x() * scale,
                        posOnPixmap.y() + p.y() * scale);
    }
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Qt::black, qMax(1.0, scale)));
    p.setBrush(Qt::white);
    p.drawPolygon(poly);
}

QPixmap renderTestPattern(QSize size, int pulse) {
    QPixmap pm(size);
    pm.fill(Qt::black);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    static const QColor bars[] = {
        QColor("#ffffff"), QColor("#ffff00"), QColor("#00ffff"),
        QColor("#00ff00"), QColor("#ff00ff"), QColor("#ff0000"),
        QColor("#0000ff"), QColor("#202020"),
    };
    const int barCount = sizeof(bars) / sizeof(bars[0]);
    const qreal bw = size.width() / qreal(barCount);
    for (int i = 0; i < barCount; ++i) {
        p.fillRect(QRectF(i * bw, 0, bw, size.height()), bars[i]);
    }
    // Sweep marker so it's clearly an animated test pattern.
    qreal x = (pulse % 360) / 360.0 * size.width();
    p.setPen(QPen(QColor(255, 153, 0, 200), 4));
    p.drawLine(QPointF(x, 0), QPointF(x, size.height()));
    return pm;
}

} // namespace

PreviewWidget::PreviewWidget(ThemeManager* theme, QWidget* parent)
    : QWidget(parent), m_theme(theme) {
    setMinimumHeight(280);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    // 60 fps paint timer — drives the idle pulse, the test pattern
    // sweep, handle hover feedback and per-source repaints. The actual
    // frame *capture* (QScreen::grabWindow / image decode + smooth
    // scale) is the heavy bit, so we only do it on every other tick
    // and only for one source at a time (round-robin) — the cached
    // pixmaps from the previous capture are re-blitted at full 60 fps.
    auto* tick = new QTimer(this);
    tick->setTimerType(Qt::PreciseTimer);
    tick->setInterval(1000 / 60);
    connect(tick, &QTimer::timeout, this, &PreviewWidget::tickFrame);
    tick->start();

    if (m_theme) {
        connect(m_theme, &ThemeManager::accentChanged, this,
                [this] { update(); });
    }
}

void PreviewWidget::setSummary(const QString& s) {
    m_summary = s;
    update();
}

void PreviewWidget::setLive(bool live) {
    m_live = live;
    update();
}

void PreviewWidget::setSources(const SourceList& sources) {
    // Don't clobber the rect the user is currently dragging — keep our
    // local edited copy of the active source's transform until release.
    QRectF dragT;
    QString dragId;
    if (m_drag != Handle::None) {
        const int ai = activeIndex();
        if (ai >= 0) {
            dragT = m_sources[ai].transform;
            dragId = m_sources[ai].id;
        }
    }

    m_sources = sources;

    if (!dragId.isEmpty()) {
        for (auto& s : m_sources) {
            if (s.id == dragId) { s.transform = dragT; break; }
        }
    }

    // Drop cached frames/notes for sources that disappeared.
    QSet<QString> alive;
    for (const auto& s : m_sources) alive.insert(s.id);
    for (auto it = m_frames.begin(); it != m_frames.end();) {
        if (!alive.contains(it.key())) it = m_frames.erase(it);
        else ++it;
    }
    for (auto it = m_frameNotes.begin(); it != m_frameNotes.end();) {
        if (!alive.contains(it.key())) it = m_frameNotes.erase(it);
        else ++it;
    }
    for (auto it = m_browserLayers.begin(); it != m_browserLayers.end();) {
        if (!alive.contains(it.key())) {
            delete it.value();
            it = m_browserLayers.erase(it);
        } else {
            ++it;
        }
    }

    // Refresh static / cheap sources right away so they appear without
    // having to wait for the next round-robin capture tick.
    for (auto& s : m_sources) {
        if (!s.enabled) continue;
        if (!sourceTypeIsVideo(s.type)) continue;
        if (sourceNeedsContinuousCapture(s.type)) continue;
        grabFrameForSource(s);
    }

    update();
}

void PreviewWidget::setActiveSourceId(const QString& id) {
    if (m_activeId == id) return;
    m_activeId = id;
    if (activeIndex() < 0) {
        // Selection was cleared or pointed at a dead id.
        m_drag = Handle::None;
        setCursor(Qt::ArrowCursor);
    }
    update();
}

void PreviewWidget::tickFrame() {
    m_pulse = (m_pulse + 6) % 360;

    // Every other tick, capture from exactly ONE source (round-robin)
    // so the GUI thread does at most one heavy grab per ~32 ms even if
    // there are several display-capture sources on the canvas. The
    // active source gets a small bias — when it cycles up, we capture
    // it; when others come up, we capture them too. Sources that don't
    // need continuous capture are skipped (their pixmap was cached on
    // setSources / on edit).
    if (++m_captureTickSkip >= 2 && !m_sources.isEmpty()) {
        m_captureTickSkip = 0;
        const int n = m_sources.size();
        for (int tries = 0; tries < n; ++tries) {
            int idx = (m_captureRR + tries) % n;
            Source& s = m_sources[idx];
            if (!s.enabled) continue;
            if (!sourceTypeIsVideo(s.type)) continue;
            if (!sourceNeedsContinuousCapture(s.type)) continue;
            grabFrameForSource(s);
            m_captureRR = (idx + 1) % n;
            break;
        }
    }
    update();
}

bool PreviewWidget::sourceNeedsContinuousCapture(SourceType t) {
    switch (t) {
        case SourceType::DisplayCapture:
        case SourceType::TestPattern:
        case SourceType::BrowserSource:
            return true;
        case SourceType::WindowCapture:
        case SourceType::VideoCaptureDevice:
        case SourceType::MediaFile:
        case SourceType::Image:
        case SourceType::ColorSource:
        case SourceType::TextSource:
        case SourceType::Microphone:
        case SourceType::DesktopAudio:
            return false;
    }
    return false;
}

void PreviewWidget::grabFrameForSource(Source& s) {
    // The pixmap is grabbed at (roughly) the source's *destination*
    // size on the canvas, capped for memory. The source's transform
    // determines where it lands inside the canvas; we render the
    // cached pixmap aspect-fit into that rect at paint time.
    const QRectF dstF = sourceRectF(s);
    const QSize target = dstF.toAlignedRect().size().boundedTo(QSize(1280, 720));
    if (target.width() < 16 || target.height() < 16) {
        // Too small to be visible; skip until the user enlarges it.
        return;
    }

    auto setNote = [&](const QString& note) {
        m_frameNotes.insert(s.id, note);
        m_frames.remove(s.id);
    };
    auto setFrame = [&](const QPixmap& pm) {
        m_frames.insert(s.id, pm);
        m_frameNotes.remove(s.id);
    };

    switch (s.type) {
        case SourceType::DisplayCapture: {
            const int idx = s.settings.value(
                SourceFields::SCREEN_INDEX, 0).toInt();
            const auto screens = QGuiApplication::screens();
            QScreen* sc = (idx >= 0 && idx < screens.size())
                              ? screens[idx]
                              : QGuiApplication::primaryScreen();
            if (!sc) {
                setNote(tr("Monitor not found"));
                return;
            }
            QPixmap raw = sc->grabWindow(0);
            if (raw.isNull()) {
                setNote(tr("Could not capture frame (X11/Wayland needs xcb)."));
                return;
            }
            // grabWindow strips the OS cursor on every platform; paint
            // it back in at the global cursor position so the preview
            // matches what the streamer sees on their own desktop.
            const QRect scGeom = sc->geometry();
            const QPoint cursorGlobal = QCursor::pos();
            if (scGeom.contains(cursorGlobal) &&
                raw.width() > 0 && scGeom.width() > 0 &&
                raw.height() > 0 && scGeom.height() > 0) {
                const qreal sx = qreal(raw.width())  / qreal(scGeom.width());
                const qreal sy = qreal(raw.height()) / qreal(scGeom.height());
                const QPointF pmPos(
                    (cursorGlobal.x() - scGeom.x()) * sx,
                    (cursorGlobal.y() - scGeom.y()) * sy);
                overlayCursor(raw, pmPos, qMax(sx, sy));
            }
            setFrame(raw.scaled(target, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation));
            return;
        }
        case SourceType::WindowCapture: {
            setNote(tr("Window preview only available on Windows during streaming."));
            return;
        }
        case SourceType::VideoCaptureDevice: {
            setNote(tr("Camera preview will appear during streaming."));
            return;
        }
        case SourceType::MediaFile: {
            const QString path = s.settings.value(
                SourceFields::FILE_PATH).toString();
            setNote(path.isEmpty()
                ? tr("No file selected.")
                : tr("Media file preview will appear during streaming:\n%1").arg(path));
            return;
        }
        case SourceType::Image: {
            const QString path = s.settings.value(
                SourceFields::FILE_PATH).toString();
            if (path.isEmpty() || !QFileInfo::exists(path)) {
                setNote(tr("No image file selected."));
                return;
            }
            QPixmap raw(path);
            if (raw.isNull()) {
                setNote(tr("Failed to load image:\n%1").arg(path));
                return;
            }
            setFrame(raw.scaled(target, Qt::KeepAspectRatio,
                                Qt::SmoothTransformation));
            return;
        }
        case SourceType::ColorSource: {
            setFrame(renderColorPreview(s, target));
            return;
        }
        case SourceType::TextSource: {
            setFrame(renderTextPreview(s, target));
            return;
        }
        case SourceType::TestPattern: {
            setFrame(renderTestPattern(target, m_pulse));
            return;
        }
        case SourceType::BrowserSource: {
            const QString url = s.settings.value(SourceFields::BROWSER_URL).toString();
            if (url.isEmpty()) {
                setNote(tr("No URL set."));
                return;
            }
            BrowserLayer* layer = m_browserLayers.value(s.id);
            if (!layer) {
                layer = new BrowserLayer(this);
                m_browserLayers.insert(s.id, layer);
            }
            layer->configure(url,
                             s.settings.value(SourceFields::BROWSER_WIDTH, 1280).toInt(),
                             s.settings.value(SourceFields::BROWSER_HEIGHT, 720).toInt(),
                             s.settings.value(SourceFields::BROWSER_REFRESH_SEC, 0).toInt(),
                             s.settings.value(SourceFields::BROWSER_CUSTOM_CSS).toString());
            const QPixmap pm = layer->grab();
            if (pm.isNull()) {
                setNote(tr("Loading %1…").arg(url));
                return;
            }
            setFrame(pm.scaled(target, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation));
            return;
        }
        case SourceType::Microphone:
        case SourceType::DesktopAudio: {
            setNote(tr("Audio source — no video preview."));
            return;
        }
    }
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect r = canvasRect();
    QPainterPath path;
    path.addRoundedRect(r, 16, 16);

    QColor accent = m_theme ? m_theme->accent() : QColor(255, 153, 0);
    QLinearGradient bg(r.topLeft(), r.bottomRight());
    bg.setColorAt(0.0, QColor(20, 20, 20));
    bg.setColorAt(1.0, QColor(8, 8, 8));
    p.fillPath(path, bg);

    p.save();
    p.setClipPath(path);

    if (anyEnabledVideo()) {
        // Bottom-up paint: list order = z-order, first item at the
        // bottom of the stack (matches OBS conventions where moving a
        // source "up" in the list moves it forward visually).
        for (int i = m_sources.size() - 1; i >= 0; --i) {
            const Source& s = m_sources[i];
            if (!s.enabled) continue;
            if (!sourceTypeIsVideo(s.type)) continue;
            const QRectF dstF = sourceRectF(s);
            const QRect dst = dstF.toAlignedRect();
            if (dst.width() <= 4 || dst.height() <= 4) continue;

            // Subtle backdrop fill so an undersized source doesn't
            // blend into the card gradient.
            p.fillRect(dst, QColor(6, 6, 6));

            const QPixmap pm = m_frames.value(s.id);
            if (!pm.isNull()) {
                const QSize fs = pm.size().scaled(dst.size(),
                                                   Qt::KeepAspectRatio);
                const QRect inner((dst.width() - fs.width()) / 2 + dst.left(),
                                  (dst.height() - fs.height()) / 2 + dst.top(),
                                  fs.width(), fs.height());
                p.drawPixmap(inner, pm);
            } else {
                // No cached frame yet — show a tiny placeholder caption
                // inside the rect so the user sees that the source is
                // present even before the first capture lands.
                const QString note = m_frameNotes.value(s.id);
                if (!note.isEmpty()) {
                    p.setPen(QColor(180, 180, 180));
                    QFont nf = p.font();
                    nf.setPointSize(9);
                    p.setFont(nf);
                    p.drawText(dst.adjusted(8, 6, -8, -6),
                               Qt::AlignCenter | Qt::TextWordWrap, note);
                }
            }
        }
    } else {
        paintIdle(p, r);
    }
    p.restore();

    // Selection frame + 8 resize handles around the active source only.
    const int ai = activeIndex();
    if (ai >= 0) {
        const Source& as = m_sources[ai];
        if (sourceTypeIsVideo(as.type)) {
            const QRectF srcF = sourceRectF(as);
            const QRect src = srcF.toAlignedRect().intersected(r);
            if (src.width() > 4 && src.height() > 4) {
                QPen frame(accent, 1.5, Qt::DashLine);
                p.setPen(frame);
                p.setBrush(Qt::NoBrush);
                p.drawRect(src);

                const Handle handles[] = {
                    Handle::NW, Handle::N, Handle::NE,
                    Handle::W,             Handle::E,
                    Handle::SW, Handle::S, Handle::SE,
                };
                p.setPen(QPen(QColor(10, 10, 10), 1));
                p.setBrush(accent);
                for (Handle h : handles) {
                    p.drawRect(handleRect(h, srcF));
                }
            }
        }
    }

    QPen border(accent, 1.4);
    p.setPen(border);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    p.setPen(QColor(180, 180, 180));
    QFont f = p.font();
    f.setPointSize(10);
    f.setWeight(QFont::Medium);
    p.setFont(f);

    QString caption;
    if (m_live) {
        caption = tr("ON AIR");
    } else if (ai >= 0) {
        const Source& as = m_sources[ai];
        const QString note = m_frameNotes.value(as.id);
        if (!note.isEmpty()) {
            caption = note;
        } else {
            caption = tr("Selected: %1  ·  drag corners to resize, body to move").arg(as.name);
        }
    } else if (anyEnabledVideo()) {
        caption = tr("Click a source on the canvas or in the sources list to edit");
    } else {
        caption = tr("Add a source from the «Sources» panel");
    }
    QRect captionRect(r.left() + 12, r.bottom() - 44, r.width() - 24, 18);
    p.setPen(QColor(0, 0, 0, 160));
    p.drawText(captionRect.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter, caption);
    p.setPen(QColor(220, 220, 220));
    p.drawText(captionRect, Qt::AlignLeft | Qt::AlignVCenter, caption);

    if (!m_summary.isEmpty()) {
        p.setPen(QColor(160, 160, 160));
        f.setPointSize(9);
        f.setWeight(QFont::Normal);
        p.setFont(f);
        QRect sumRect(r.left() + 12, captionRect.bottom() + 2,
                       r.width() - 24, 18);
        p.drawText(sumRect, Qt::AlignLeft | Qt::AlignVCenter, m_summary);
    }
}

void PreviewWidget::paintIdle(QPainter& p, const QRect& r) {
    QColor accent = m_theme ? m_theme->accent() : QColor(255, 153, 0);
    const QPointF center = r.center();
    QRadialGradient bloom(center, r.height() * 0.45);
    QColor bloomColor = accent;
    bloomColor.setAlpha(45);
    bloom.setColorAt(0.0, bloomColor);
    bloomColor.setAlpha(0);
    bloom.setColorAt(1.0, bloomColor);
    p.setPen(Qt::NoPen);
    p.setBrush(bloom);
    p.drawEllipse(center, r.height() * 0.45, r.height() * 0.45);

    const qreal phase = (m_pulse / 360.0) * 2.0 * M_PI;
    const qreal radius = 22.0 + 6.0 * std::sin(phase);
    QColor pulseColor = m_live ? QColor(224, 59, 59) : accent;
    p.setBrush(pulseColor);
    p.drawEllipse(center, radius, radius);

    QPainterPath glyph;
    if (m_live) {
        glyph.addRoundedRect(QRectF(center.x() - 8, center.y() - 8, 16, 16),
                             3, 3);
    } else {
        glyph.moveTo(center.x() - 6, center.y() - 9);
        glyph.lineTo(center.x() + 10, center.y());
        glyph.lineTo(center.x() - 6, center.y() + 9);
        glyph.closeSubpath();
    }
    p.fillPath(glyph, QColor(10, 10, 10));
}

QRect PreviewWidget::canvasRect() const {
    return rect().adjusted(8, 8, -8, -8);
}

QRectF PreviewWidget::sourceRectF(const Source& s) const {
    const QRect c = canvasRect();
    const QRectF& t = s.transform;
    return QRectF(c.left() + t.x() * c.width(),
                  c.top()  + t.y() * c.height(),
                  t.width()  * c.width(),
                  t.height() * c.height());
}

QRect PreviewWidget::handleRect(Handle h, const QRectF& src) const {
    constexpr int hs = 9;          // half-size of the handle square
    QPointF center;
    switch (h) {
        case Handle::NW: center = src.topLeft();      break;
        case Handle::N:  center = QPointF(src.center().x(), src.top());     break;
        case Handle::NE: center = src.topRight();     break;
        case Handle::E:  center = QPointF(src.right(), src.center().y());   break;
        case Handle::SE: center = src.bottomRight();  break;
        case Handle::S:  center = QPointF(src.center().x(), src.bottom());  break;
        case Handle::SW: center = src.bottomLeft();   break;
        case Handle::W:  center = QPointF(src.left(),  src.center().y());   break;
        default:         return QRect();
    }
    return QRect(int(center.x()) - hs, int(center.y()) - hs, hs * 2, hs * 2);
}

PreviewWidget::Handle PreviewWidget::hitTestHandles(const QPoint& p) const {
    const int ai = activeIndex();
    if (ai < 0) return Handle::None;
    const Source& as = m_sources[ai];
    if (!sourceTypeIsVideo(as.type)) return Handle::None;
    const QRectF srcF = sourceRectF(as);
    if (srcF.width() < 4 || srcF.height() < 4) return Handle::None;
    const Handle handles[] = {
        Handle::NW, Handle::N, Handle::NE,
        Handle::W,             Handle::E,
        Handle::SW, Handle::S, Handle::SE,
    };
    for (Handle h : handles) {
        if (handleRect(h, srcF).contains(p)) return h;
    }
    if (srcF.toAlignedRect().contains(p)) return Handle::Body;
    return Handle::None;
}

int PreviewWidget::hitTestBody(const QPoint& p) const {
    // Top-down (front-most first): list head wins so click follows the
    // visual stacking from paintEvent.
    for (int i = 0; i < m_sources.size(); ++i) {
        const Source& s = m_sources[i];
        if (!s.enabled) continue;
        if (!sourceTypeIsVideo(s.type)) continue;
        const QRectF srcF = sourceRectF(s);
        if (srcF.toAlignedRect().contains(p)) return i;
    }
    return -1;
}

void PreviewWidget::updateCursorFor(Handle h) {
    Qt::CursorShape c = Qt::ArrowCursor;
    switch (h) {
        case Handle::N: case Handle::S: c = Qt::SizeVerCursor; break;
        case Handle::E: case Handle::W: c = Qt::SizeHorCursor; break;
        case Handle::NW: case Handle::SE: c = Qt::SizeFDiagCursor; break;
        case Handle::NE: case Handle::SW: c = Qt::SizeBDiagCursor; break;
        case Handle::Body:               c = Qt::SizeAllCursor;   break;
        case Handle::None: default:      c = Qt::ArrowCursor;     break;
    }
    setCursor(c);
}

QRectF PreviewWidget::clamp(const QRectF& r) const {
    constexpr qreal MIN = 0.05; // never let a source shrink below 5%
    qreal x = std::clamp(r.x(), qreal(0.0), qreal(1.0) - MIN);
    qreal y = std::clamp(r.y(), qreal(0.0), qreal(1.0) - MIN);
    qreal w = std::clamp(r.width(),  MIN, qreal(1.0) - x);
    qreal h = std::clamp(r.height(), MIN, qreal(1.0) - y);
    return QRectF(x, y, w, h);
}

int PreviewWidget::activeIndex() const {
    if (m_activeId.isEmpty()) return -1;
    for (int i = 0; i < m_sources.size(); ++i) {
        if (m_sources[i].id == m_activeId) return i;
    }
    return -1;
}

bool PreviewWidget::anyEnabledVideo() const {
    for (const auto& s : m_sources) {
        if (s.enabled && sourceTypeIsVideo(s.type)) return true;
    }
    return false;
}

void PreviewWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }

    // Prefer a handle of the active source — they take priority even if
    // they spill outside another source's body rect (corners sit on
    // the source border).
    const Handle h = hitTestHandles(e->pos());
    if (h != Handle::None && h != Handle::Body) {
        m_drag = h;
        m_dragStart = e->pos();
        m_dragStartT = m_sources[activeIndex()].transform;
        updateCursorFor(h);
        return;
    }

    // Otherwise, see which source body the click landed on. Top-most
    // wins; if it's not the active one, switch selection (no drag this
    // click — user can click again to start moving). If it IS the
    // active one, start a body drag.
    const int hit = hitTestBody(e->pos());
    if (hit >= 0) {
        const Source& s = m_sources[hit];
        if (s.id != m_activeId) {
            m_activeId = s.id;
            m_drag = Handle::None;
            updateCursorFor(Handle::None);
            update();
            emit sourceSelected(s.id);
            return;
        }
        // Active source body: start move.
        m_drag = Handle::Body;
        m_dragStart = e->pos();
        m_dragStartT = m_sources[activeIndex()].transform;
        updateCursorFor(Handle::Body);
        return;
    }

    // Click landed on empty canvas — drop the current selection so the
    // dashed border and resize handles disappear until the user clicks
    // a source again. Mirrors how OBS and most editors behave.
    m_drag = Handle::None;
    if (!m_activeId.isEmpty()) {
        m_activeId.clear();
        updateCursorFor(Handle::None);
        update();
        emit sourceSelected(QString());
    }
}

void PreviewWidget::mouseMoveEvent(QMouseEvent* e) {
    if (m_drag == Handle::None) {
        // Hover-cursor feedback for handles of the active source so
        // they're discoverable.
        Handle h = hitTestHandles(e->pos());
        if (h == Handle::None) {
            // Show "move" over any draggable source body so the user
            // knows the area is interactive.
            const int hit = hitTestBody(e->pos());
            if (hit >= 0) {
                setCursor(Qt::PointingHandCursor);
                return;
            }
        }
        updateCursorFor(h);
        return;
    }
    const QRect c = canvasRect();
    const int ai = activeIndex();
    if (c.width() <= 0 || c.height() <= 0 || ai < 0) return;

    const qreal dx = qreal(e->pos().x() - m_dragStart.x()) / c.width();
    const qreal dy = qreal(e->pos().y() - m_dragStart.y()) / c.height();

    QRectF t = m_dragStartT;
    auto adjustLeft = [&] { t.setLeft(m_dragStartT.left() + dx); };
    auto adjustRight = [&] { t.setRight(m_dragStartT.right() + dx); };
    auto adjustTop = [&] { t.setTop(m_dragStartT.top() + dy); };
    auto adjustBottom = [&] { t.setBottom(m_dragStartT.bottom() + dy); };

    switch (m_drag) {
        case Handle::Body:
            t.translate(dx, dy);
            break;
        case Handle::N:  adjustTop();    break;
        case Handle::S:  adjustBottom(); break;
        case Handle::W:  adjustLeft();   break;
        case Handle::E:  adjustRight();  break;
        case Handle::NW: adjustLeft();  adjustTop();    break;
        case Handle::NE: adjustRight(); adjustTop();    break;
        case Handle::SW: adjustLeft();  adjustBottom(); break;
        case Handle::SE: adjustRight(); adjustBottom(); break;
        case Handle::None: break;
    }
    if (t.width() < 0)  { t.setLeft(t.right());   t.setWidth(0);  }
    if (t.height() < 0) { t.setTop(t.bottom());   t.setHeight(0); }

    m_sources[ai].transform = clamp(t);
    update();
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    if (m_drag == Handle::None) return;
    const int ai = activeIndex();
    m_drag = Handle::None;
    updateCursorFor(hitTestHandles(e->pos()));
    if (ai >= 0) {
        emit sourceTransformChanged(m_sources[ai].id, m_sources[ai].transform);
    }
}

void PreviewWidget::leaveEvent(QEvent* e) {
    if (m_drag == Handle::None) setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(e);
}

} // namespace lumen
