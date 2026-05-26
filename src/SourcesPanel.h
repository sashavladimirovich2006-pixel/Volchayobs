#pragma once

#include "Source.h"

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QToolButton;

namespace lumen {

class ThemeManager;

// OBS-style sources dock: a list of every source the user has added,
// with controls to enable/disable, configure, reorder, and delete each
// row. Owns the canonical SourceList and emits `sourcesChanged()`
// whenever it mutates so MainWindow can repaint the preview, persist
// state, and forward the new list into StreamEngine on next start.
class SourcesPanel : public QWidget {
    Q_OBJECT
public:
    explicit SourcesPanel(ThemeManager* theme, QWidget* parent = nullptr);

    SourceList sources() const { return m_sources; }
    void setSources(const SourceList& sources);

    // Index of the currently selected row, or -1.
    int selectedIndex() const;

public slots:
    // Drive the row selection from outside the panel (e.g. when the
    // preview widget passes back a click on a source body).
    void setSelectedSourceId(const QString& id);

signals:
    void sourcesChanged();
    void selectionChanged();

private slots:
    void onAddRequested();
    void onRemoveSelected();
    void onConfigureSelected();
    void onMoveUp();
    void onMoveDown();
    void onItemEnableToggled(QListWidgetItem* item);

private:
    // Pick a free spot on the [0..1] canvas grid for a new source so it
    // doesn't slap on top of existing rectangles.
    QRectF nextFreeTransform() const;

private:
    void rebuildList();
    QListWidgetItem* makeItem(const Source& s) const;

    ThemeManager*  m_theme;
    SourceList     m_sources;

    QListWidget*   m_list = nullptr;
    QPushButton*   m_addBtn = nullptr;
    QToolButton*   m_removeBtn = nullptr;
    QToolButton*   m_configBtn = nullptr;
    QToolButton*   m_upBtn = nullptr;
    QToolButton*   m_downBtn = nullptr;
};

} // namespace lumen
