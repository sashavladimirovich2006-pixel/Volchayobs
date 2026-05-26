#pragma once

#include "Source.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace lumen {

class AudioLevelMeter;
class AudioLevelProbe;
class ThemeManager;

// OBS-style audio mixer dock. Renders one strip per audio source
// (Microphone + DesktopAudio) currently in the live SourceList, with
// a mute toggle and a 0..200% volume slider per strip.
//
// The panel owns a copy of the source list (mirroring SourcesPanel's
// model) and emits `sourcesChanged()` whenever the user mutates an
// audio knob. MainWindow takes the new list, persists it, and pushes
// it back into both the sources panel and the mixer so the two stay
// in sync.
//
// Bonus: a header row with quick "+ Microphone" / "+ Desktop audio"
// buttons so a new user can plug a mic in two clicks without opening
// the full SourceConfigDialog.
class AudioMixerPanel : public QWidget {
    Q_OBJECT
public:
    explicit AudioMixerPanel(ThemeManager* theme, QWidget* parent = nullptr);

    SourceList sources() const { return m_sources; }
    void setSources(const SourceList& sources);

signals:
    // Fired whenever the user mutes/un-mutes a strip, drags a volume
    // slider, or adds a new audio source via one of the quick buttons.
    // MainWindow is expected to read sources() and persist them.
    void sourcesChanged();

private:
    void rebuild();
    void onMuteToggled(const QString& id, bool muted);
    void onVolumeChanged(const QString& id, int percent);
    void addAudioSource(SourceType t);
    void syncProbes();

    ThemeManager* m_theme;
    SourceList    m_sources;

    QVBoxLayout*  m_strips = nullptr;
    QLabel*       m_empty = nullptr;
    QPushButton*  m_addMicBtn = nullptr;
    QPushButton*  m_addDesktopBtn = nullptr;

    // One probe + meter per audio source id. Probes outlive a single
    // rebuild() (which tears down all the widgets) so we don't churn
    // ffmpeg processes on every UI repaint.
    QHash<QString, AudioLevelProbe*> m_probes;
    QHash<QString, AudioLevelMeter*> m_meters;
};

} // namespace lumen
