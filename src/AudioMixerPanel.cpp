#include "AudioMixerPanel.h"

#include "AudioLevelMeter.h"
#include "AudioLevelProbe.h"
#include "Devices.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace lumen {

namespace {

// Pick a default device id for a new audio source the user just added
// via the quick-add buttons. Returns the first device the platform
// enumerator reports, or an empty pair if nothing's plugged in (the
// strip still appears, just with no captureable input until the user
// opens SourceConfigDialog to pick one).
QPair<QString, QString> defaultAudioDevice(SourceType t) {
    const auto list = (t == SourceType::Microphone)
                          ? enumerateMicrophones()
                          : enumerateDesktopAudio();
    if (list.isEmpty()) return {};
    return {list.first().id, list.first().label};
}

// Compact device-label string for the strip subtitle. We prefer the
// stored audio-device label because that's what the user picked in
// SourceConfigDialog; fall back to a friendly hint so a user without
// any enumerated devices doesn't see a bare "(no devices found)".
QString deviceSubtitle(const Source& s) {
    const QString label = s.settings.value(
        SourceFields::AUDIO_DEVICE_LABEL).toString();
    if (!label.trimmed().isEmpty()) return label;
    const QString id = s.settings.value(
        SourceFields::AUDIO_DEVICE_ID).toString();
    if (!id.trimmed().isEmpty()) return id;
    if (s.type == SourceType::DesktopAudio) {
        return QObject::tr("Click Configure to pick a desktop-audio source");
    }
    return QObject::tr("Click Configure to pick an input device");
}

} // namespace

AudioMixerPanel::AudioMixerPanel(ThemeManager* theme, QWidget* parent)
    : QWidget(parent), m_theme(theme) {
    Q_UNUSED(m_theme);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    // Header row: title + quick "Add mic / Add desktop" affordances.
    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(tr("AUDIO MIXER"));
    title->setProperty("role", "sectionHeader");
    m_addMicBtn = new QPushButton(tr("+ Microphone"));
    m_addDesktopBtn = new QPushButton(tr("+ Desktop audio"));
    for (QPushButton* b : { m_addMicBtn, m_addDesktopBtn }) {
        b->setProperty("role", "secondary");
        b->setCursor(Qt::PointingHandCursor);
    }
    connect(m_addMicBtn, &QPushButton::clicked, this,
            [this] { addAudioSource(SourceType::Microphone); });
    connect(m_addDesktopBtn, &QPushButton::clicked, this,
            [this] { addAudioSource(SourceType::DesktopAudio); });
    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_addMicBtn);
    header->addWidget(m_addDesktopBtn);
    root->addLayout(header);

    // Container for the per-source strips.
    auto* stripsWrap = new QWidget;
    m_strips = new QVBoxLayout(stripsWrap);
    m_strips->setContentsMargins(0, 0, 0, 0);
    m_strips->setSpacing(8);
    root->addWidget(stripsWrap, 1);

    // Empty-state shown when no audio sources exist yet.
    m_empty = new QLabel(tr("No audio sources yet — add a microphone or "
                            "desktop audio above to start mixing."));
    m_empty->setProperty("role", "sectionHeader");
    m_empty->setWordWrap(true);
    m_empty->setAlignment(Qt::AlignCenter);
    m_strips->addWidget(m_empty);
}

void AudioMixerPanel::setSources(const SourceList& sources) {
    m_sources = sources;
    rebuild();
}

void AudioMixerPanel::rebuild() {
    // Tear down existing strips (everything but the empty-state label,
    // which we hide/show depending on whether any audio sources exist).
    while (m_strips->count() > 0) {
        QLayoutItem* it = m_strips->takeAt(0);
        if (!it) break;
        if (QWidget* w = it->widget()) {
            if (w != m_empty) w->deleteLater();
        }
        delete it;
    }
    // The meter widgets we just queued for deletion are about to be
    // re-created below. Drop the stale pointers now so signal handlers
    // never dereference them between teardown and rebuild.
    m_meters.clear();

    int strips = 0;
    for (int i = 0; i < m_sources.size(); ++i) {
        const Source& s = m_sources[i];
        if (!sourceTypeIsAudio(s.type)) continue;
        ++strips;

        auto* strip = new QWidget;
        strip->setProperty("role", "card");
        auto* h = new QHBoxLayout(strip);
        h->setContentsMargins(14, 10, 14, 10);
        h->setSpacing(12);

        // Name + device subtitle + live VU meter underneath the name.
        auto* nameCol = new QVBoxLayout;
        nameCol->setSpacing(2);
        auto* name = new QLabel(s.name.isEmpty()
                                    ? sourceTypeDisplayName(s.type)
                                    : s.name);
        QFont nf = name->font();
        nf.setWeight(QFont::DemiBold);
        name->setFont(nf);
        auto* sub = new QLabel(QString("%1 · %2")
                                   .arg(sourceTypeDisplayName(s.type),
                                        deviceSubtitle(s)));
        sub->setProperty("role", "sectionHeader");
        auto* meter = new AudioLevelMeter;
        meter->setFixedHeight(12);
        meter->setMinimumWidth(220);
        m_meters.insert(s.id, meter);
        nameCol->addWidget(name);
        nameCol->addWidget(sub);
        nameCol->addSpacing(4);
        nameCol->addWidget(meter);

        // Mute toggle reuses the source's enabled bit so the rest of
        // the app (stream pipeline, sources panel) stays in sync.
        auto* mute = new QToolButton;
        mute->setText(s.enabled ? tr("MUTE") : tr("MUTED"));
        mute->setCheckable(true);
        mute->setChecked(!s.enabled);
        mute->setCursor(Qt::PointingHandCursor);
        mute->setMinimumWidth(74);
        // Pill-shaped button that lights up red when muted, mirroring
        // OBS's audio rack. Text color cascades from the active theme
        // (QApplication stylesheet QToolButton color) so it's readable on
        // both Blackout and Light. Border uses rgba so it's visible on
        // any background. :checked turns the pill solid red regardless.
        mute->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  background: rgba(128, 128, 128, 25);"
            "  border: 1px solid rgba(160, 160, 160, 120);"
            "  border-radius: 10px;"
            "  padding: 5px 14px;"
            "  font-weight: 700;"
            "  letter-spacing: 0.5px;"
            "}"
            "QToolButton:hover {"
            "  background: rgba(160, 160, 160, 50);"
            "  border-color: rgba(200, 200, 200, 180);"
            "}"
            "QToolButton:checked {"
            "  background: #c0392b;"
            "  border: 1px solid #ff6b5e;"
            "  color: #FFFFFF;"
            "}"
            "QToolButton:checked:hover {"
            "  background: #e74c3c;"
            "  border-color: #ff8a7c;"
            "  color: #FFFFFF;"
            "}"
        ));
        const QString id = s.id;
        connect(mute, &QToolButton::toggled, this,
                [this, id, mute](bool checked) {
                    mute->setText(checked ? tr("MUTED") : tr("MUTE"));
                    onMuteToggled(id, checked);
                });

        // 0..200% volume slider. The stored AUDIO_VOLUME is a multiplier
        // (1.0 = unity, 2.0 = +6dB-ish) so we round-trip through *100.
        const double stored = s.settings.value(
            SourceFields::AUDIO_VOLUME, 1.0).toDouble();
        const int percent = qBound(0, int(stored * 100.0 + 0.5), 200);

        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 200);
        slider->setValue(percent);
        slider->setMinimumWidth(180);
        slider->setCursor(Qt::PointingHandCursor);
        // OBS-style fader: thin groove with the live volume drawn in
        // the amber brand accent, a round handle that sits above the
        // groove. Hardcoded colors (not palette(...)) because Qt's
        // platform palette gives a Windows-system blue for `highlight`
        // and a near-white grey for `mid` — both wrong on Blackout/RGB.
        // rgba grey reads as a faint rail on every theme.
        slider->setStyleSheet(QStringLiteral(
            "QSlider::groove:horizontal {"
            "  height: 4px;"
            "  background: rgba(128, 128, 128, 60);"
            "  border-radius: 2px;"
            "}"
            "QSlider::sub-page:horizontal {"
            "  background: #FF9900;"
            "  border-radius: 2px;"
            "}"
            "QSlider::add-page:horizontal {"
            "  background: rgba(128, 128, 128, 60);"
            "  border-radius: 2px;"
            "}"
            "QSlider::handle:horizontal {"
            "  background: #FF9900;"
            "  border: 2px solid rgba(0, 0, 0, 140);"
            "  width: 14px;"
            "  height: 14px;"
            "  margin: -7px 0;"
            "  border-radius: 9px;"
            "}"
            "QSlider::handle:horizontal:hover {"
            "  background: #FFB347;"
            "  border: 2px solid rgba(255, 255, 255, 180);"
            "}"
        ));

        auto* pct = new QLabel(QString("%1%").arg(percent));
        pct->setMinimumWidth(44);
        pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        connect(slider, &QSlider::valueChanged, this,
                [this, id, pct](int v) {
                    pct->setText(QString("%1%").arg(v));
                    onVolumeChanged(id, v);
                });

        h->addLayout(nameCol, 1);
        h->addWidget(slider, 2);
        h->addWidget(pct, 0);
        h->addWidget(mute, 0);

        m_strips->addWidget(strip);
    }

    syncProbes();

    m_empty->setVisible(strips == 0);
    if (strips == 0) {
        // Make sure the empty-state label is in the layout (it stays
        // owned by us but rebuild() may have detached it above).
        if (m_strips->indexOf(m_empty) < 0) m_strips->addWidget(m_empty);
    } else if (m_strips->indexOf(m_empty) >= 0) {
        m_strips->removeWidget(m_empty);
    }
    m_strips->addStretch(0);
}

void AudioMixerPanel::onMuteToggled(const QString& id, bool muted) {
    for (auto& s : m_sources) {
        if (s.id != id) continue;
        const bool enabled = !muted;
        if (s.enabled == enabled) return;
        s.enabled = enabled;
        emit sourcesChanged();
        return;
    }
}

void AudioMixerPanel::onVolumeChanged(const QString& id, int percent) {
    for (auto& s : m_sources) {
        if (s.id != id) continue;
        const double vol = qBound(0, percent, 200) / 100.0;
        const double prev = s.settings.value(
            SourceFields::AUDIO_VOLUME, 1.0).toDouble();
        if (qFuzzyCompare(prev, vol)) return;
        s.settings[SourceFields::AUDIO_VOLUME] = vol;
        emit sourcesChanged();
        return;
    }
}

void AudioMixerPanel::addAudioSource(SourceType t) {
    Source s;
    s.type = t;
    s.name = sourceTypeDisplayName(t);
    s.enabled = true;
    auto dev = defaultAudioDevice(t);
    if (!dev.first.isEmpty()) {
        s.settings[SourceFields::AUDIO_DEVICE_ID]    = dev.first;
        s.settings[SourceFields::AUDIO_DEVICE_LABEL] = dev.second;
    }
    s.settings[SourceFields::AUDIO_VOLUME] = 1.0;
    m_sources.append(s);
    rebuild();
    emit sourcesChanged();
}

void AudioMixerPanel::syncProbes() {
    // Build the live id set so we can tear down probes for removed sources.
    QSet<QString> live;
    for (const Source& s : m_sources) {
        if (sourceTypeIsAudio(s.type)) live.insert(s.id);
    }

    // Drop probes for sources that no longer exist.
    for (auto it = m_probes.begin(); it != m_probes.end(); ) {
        if (!live.contains(it.key())) {
            it.value()->stop();
            it.value()->deleteLater();
            it = m_probes.erase(it);
        } else {
            ++it;
        }
    }

    // (Re)start probes for current sources. start() stops the existing
    // child if any, so this also handles device or mute changes.
    for (const Source& s : m_sources) {
        if (!sourceTypeIsAudio(s.type)) continue;
        AudioLevelProbe* probe = m_probes.value(s.id, nullptr);
        if (!probe) {
            probe = new AudioLevelProbe(this);
            m_probes.insert(s.id, probe);
        }
        // Wire probe → meter. We must reconnect on every rebuild since
        // the meter widget is recreated.
        const QString id = s.id;
        QObject::disconnect(probe, nullptr, this, nullptr);
        connect(probe, &AudioLevelProbe::levelChanged, this,
                [this, id](double db) {
                    if (auto* m = m_meters.value(id, nullptr)) {
                        m->setLevelDb(db);
                    }
                });

        if (s.enabled && !s.settings.value(
                              SourceFields::AUDIO_DEVICE_ID).toString().isEmpty()) {
            probe->start(s);
        } else {
            probe->stop();
            if (auto* m = m_meters.value(s.id, nullptr)) m->clear();
        }
    }
}

} // namespace lumen
