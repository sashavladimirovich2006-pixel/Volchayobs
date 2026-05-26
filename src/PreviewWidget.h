#pragma once

#include "Source.h"

#include <QHash>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QWidget>

namespace lumen {

class BrowserLayer;
class ThemeManager;

// Live preview canvas. Renders every enabled video source at its own
// per-source transform (normalised in [0..1] of the canvas) so the
// user gets an OBS-style scene view: each source has its own rectangle
// inside the same canvas instead of stretching edge-to-edge.
//
// The currently-selected source gets a dashed selection frame plus 8
// corner/edge handles for interactive drag and resize. Clicking on
// the body of any other enabled source re-selects it (emits
// sourceSelected) so the user can jump between sources without
// leaving the preview.
//
// Falls back to the original glassmorphic "no source" pulse animation
// when no source is enabled.
class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(ThemeManager* theme, QWidget* parent = nullptr);

    void setSummary(const QString& s);
    void setLive(bool live);
    // Replace the full source list. The widget keeps its own copy so it
    // can mutate the active source's transform during drag/resize before
    // the change is round-tripped through MainWindow's saveSettings.
    void setSources(const SourceList& sources);
    // Pick which source's selection frame + handles to show. Empty id =
    // no selection. The id should match a source already passed via
    // setSources(); unknown ids hide the selection.
    void setActiveSourceId(const QString& id);

signals:
    // Fired while the user drags or resizes the active source. The new
    // transform is in normalised canvas coordinates, clamped to [0..1].
    void sourceTransformChanged(const QString& sourceId, const QRectF& t);
    // Fired when the user clicks the body of a non-selected source on
    // the canvas. MainWindow uses this to move the row selection in
    // the sources panel so the rest of the UI stays in sync.
    void sourceSelected(const QString& sourceId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void tickFrame();

private:
    enum class Handle {
        None, Body,
        N, S, W, E,
        NW, NE, SW, SE,
    };

    void grabFrameForSource(Source& s);
    void paintIdle(QPainter& p, const QRect& r);
    static bool sourceNeedsContinuousCapture(SourceType t);

    QRect  canvasRect() const;
    QRectF sourceRectF(const Source& s) const; // s.transform mapped into canvasRect
    QRect  handleRect(Handle h, const QRectF& srcRect) const;
    Handle hitTestHandles(const QPoint& p) const;            // active source only
    int    hitTestBody(const QPoint& p) const;               // any enabled video source; -1 = none
    void   updateCursorFor(Handle h);
    QRectF clamp(const QRectF& r) const; // clamp normalised rect to [0..1]
    int    activeIndex() const;          // index of active source in m_sources, or -1
    bool   anyEnabledVideo() const;

    ThemeManager* m_theme;
    QString       m_summary;
    bool          m_live = false;
    int           m_pulse = 0;

    SourceList    m_sources;
    QString       m_activeId;
    // Per-source cached pixmap so we can draw all sources every paint
    // tick without re-grabbing each one. Sources keep their own slot
    // keyed by id; entries are dropped on setSources().
    QHash<QString, QPixmap> m_frames;
    QHash<QString, QString> m_frameNotes;
    // QWebEngineView wrappers keyed by source id; dropped together
    // with m_frames when setSources() removes the source.
    QHash<QString, BrowserLayer*> m_browserLayers;
    int           m_captureTickSkip = 0; // every other paint = capture pass
    int           m_captureRR = 0;       // round-robin index of next source

    // Interactive drag state (mouse). Always references m_activeId.
    Handle  m_drag = Handle::None;
    QPoint  m_dragStart;
    QRectF  m_dragStartT;
};

} // namespace lumen
