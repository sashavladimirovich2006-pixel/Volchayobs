#include "MainWindow.h"

#include "AudioMixerPanel.h"
#include "Logger.h"
#include "PresetManager.h"
#include "PreviewWidget.h"
#include "SourcesPanel.h"

#include <QApplication>
#include <QColor>
#include <QDesktopServices>
#include <QEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

namespace lumen {

namespace {

QPushButton* makeNavButton(const QString& label, const QString& iconRes) {
    auto* b = new QPushButton(label);
    b->setIcon(QIcon(iconRes));
    b->setIconSize({18, 18});
    b->setProperty("role", "nav");
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(44);
    return b;
}

QFrame* makeCard() {
    auto* card = new QFrame;
    card->setProperty("role", "card");
    return card;
}

void restyle(QWidget* w) {
    if (!w) return;
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

} // namespace

MainWindow::MainWindow(ThemeManager* theme, QWidget* parent)
    : QMainWindow(parent), m_theme(theme), m_engine(new StreamEngine(this)) {
    setWindowTitle(QStringLiteral("Volchay Obs"));
    setWindowIcon(QIcon(QStringLiteral(":/resources/icons/logo.svg")));
    resize(1100, 700);

    m_settings = loadSettings();
    if (auto* p = PresetManager::findById(m_settings.presetId)) {
        // Honor the preset's resolution/fps/bitrate when the user hasn't
        // hand-edited away from a preset since last save.
        // We trust the persisted config; reapply happens via Settings UI.
        Q_UNUSED(p);
    }

    if (m_theme) {
        m_theme->setAccent(m_settings.accent);
        m_theme->applyTheme(m_settings.theme);
        LOG_I("ui", QStringLiteral("Initial theme applied: %1, accent=%2")
                  .arg(static_cast<int>(m_settings.theme))
                  .arg(m_settings.accent.name()));
    }

    auto* central = new QWidget;
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Sidebar
    auto* sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sideBar"));
    sidebar->setFixedWidth(220);
    auto* sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 24, 0, 24);
    sideLayout->setSpacing(0);

    auto* brand = new QWidget;
    auto* brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(20, 0, 20, 24);
    brandLayout->setSpacing(10);
    auto* brandIcon = new QLabel;
    brandIcon->setFixedSize(36, 36);
    brandIcon->setPixmap(
        QIcon(QStringLiteral(":/resources/icons/logo.svg"))
            .pixmap(36, 36));
    brandIcon->setScaledContents(true);
    auto* brandTextWrap = new QWidget;
    auto* brandText = new QVBoxLayout(brandTextWrap);
    brandText->setContentsMargins(0, 0, 0, 0);
    brandText->setSpacing(0);
    auto* brandTitle = new QLabel(QStringLiteral("VOLCHAY"));
    brandTitle->setObjectName(QStringLiteral("brandTitle"));
    auto* brandSub = new QLabel(QStringLiteral("OBS"));
    brandSub->setObjectName(QStringLiteral("brandSub"));
    brandText->addWidget(brandTitle);
    brandText->addWidget(brandSub);
    brandLayout->addWidget(brandIcon);
    brandLayout->addWidget(brandTextWrap, 1);

    m_navStream = makeNavButton(tr("Stream"), QStringLiteral(":/resources/icons/stream.svg"));
    m_navSettings = makeNavButton(tr("Settings"), QStringLiteral(":/resources/icons/settings.svg"));
    m_navAbout = makeNavButton(tr("About"), QStringLiteral(":/resources/icons/info.svg"));

    sideLayout->addWidget(brand);
    sideLayout->addWidget(m_navStream);
    sideLayout->addWidget(m_navSettings);
    sideLayout->addWidget(m_navAbout);
    sideLayout->addStretch(1);

    auto* version = new QLabel(QStringLiteral("v0.1.0"));
    version->setProperty("role", "sectionHeader");
    version->setContentsMargins(20, 0, 20, 0);
    sideLayout->addWidget(version);

    // Content area
    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("contentArea"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(28, 28, 28, 28);
    contentLayout->setSpacing(20);

    m_pages = new QStackedWidget;
    contentLayout->addWidget(m_pages);

    buildStreamPage();
    buildAboutPage();

    // Wire navigation
    connect(m_navStream, &QPushButton::clicked, this, [this] { selectNav(0); });
    connect(m_navAbout,  &QPushButton::clicked, this, [this] { selectNav(1); });
    connect(m_navSettings, &QPushButton::clicked, this,
            &MainWindow::onSettingsClicked);
    selectNav(0);

    root->addWidget(sidebar);
    root->addWidget(content, 1);
    setCentralWidget(central);

    // Engine wiring
    connect(m_engine, &StreamEngine::started, this, &MainWindow::onStreamStarted);
    connect(m_engine, &StreamEngine::stopped, this, &MainWindow::onStreamStopped);
    connect(m_engine, &StreamEngine::logLine, this, &MainWindow::onStreamLog);
    connect(m_engine, &StreamEngine::errorOccurred, this, &MainWindow::onStreamError);
    connect(m_engine, &StreamEngine::statsUpdated, this, &MainWindow::onStreamStats);

    refreshStreamSummary();

    // App-wide click watcher: any LMB press outside the preview and the
    // sources panel drops the active selection so the dashed frame +
    // 8 resize handles disappear. Clicking back on a source body
    // inside the preview re-selects it via PreviewWidget's own
    // mousePressEvent — no extra wiring needed.
    qApp->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (ev->type() == QEvent::MouseButtonPress && m_preview) {
        auto* w = qobject_cast<QWidget*>(obj);
        if (w && w->window() == this) {
            const bool inPreview = (w == m_preview) || m_preview->isAncestorOf(w);
            const bool inSources = m_sourcesPanel &&
                (w == m_sourcesPanel || m_sourcesPanel->isAncestorOf(w));
            if (!inPreview && !inSources) {
                m_preview->setActiveSourceId(QString());
                if (m_sourcesPanel) m_sourcesPanel->setSelectedSourceId(QString());
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::buildStreamPage() {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(20);

    // Header card with status + go-live action.
    auto* header = makeCard();
    auto* headerLay = new QHBoxLayout(header);
    headerLay->setContentsMargins(20, 16, 20, 16);
    headerLay->setSpacing(16);

    auto* leftCol = new QVBoxLayout;
    leftCol->setSpacing(2);
    auto* hintLabel = new QLabel(tr("CURRENT PRESET"));
    hintLabel->setProperty("role", "sectionHeader");
    m_presetLabel = new QLabel(QStringLiteral("Twitch 1080p60"));
    QFont f = m_presetLabel->font();
    f.setPointSize(15);
    f.setWeight(QFont::DemiBold);
    m_presetLabel->setFont(f);
    leftCol->addWidget(hintLabel);
    leftCol->addWidget(m_presetLabel);

    auto* midCol = new QVBoxLayout;
    midCol->setSpacing(2);
    auto* bitrateHint = new QLabel(tr("BITRATE"));
    bitrateHint->setProperty("role", "sectionHeader");
    m_bitrateLabel = new QLabel(QStringLiteral("6000 kbps"));
    QFont bf = m_bitrateLabel->font();
    bf.setPointSize(15);
    bf.setWeight(QFont::DemiBold);
    m_bitrateLabel->setFont(bf);
    midCol->addWidget(bitrateHint);
    midCol->addWidget(m_bitrateLabel);

    m_statusPill = new QLabel(tr("OFFLINE"));
    m_statusPill->setObjectName(QStringLiteral("statusPill"));
    m_statusPill->setAlignment(Qt::AlignCenter);

    // OBS-like compact stats block (uptime / bitrate / drops / fps).
    // Lives between BITRATE column and the status pill. Hidden until the
    // stream actually starts so the topbar stays clean while OFFLINE.
    m_statsBlock = new QWidget;
    auto* statsLay = new QHBoxLayout(m_statsBlock);
    statsLay->setContentsMargins(8, 0, 8, 0);
    statsLay->setSpacing(14);

    m_statsHealthDot = new QLabel;
    m_statsHealthDot->setFixedSize(10, 10);
    m_statsHealthDot->setStyleSheet(
        QStringLiteral("background-color:#2ecc71;border-radius:5px;"));
    m_statsHealthDot->setToolTip(tr("Stream health"));

    auto buildStat = [](const QString& cap) {
        auto* col = new QVBoxLayout;
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(1);
        auto* h = new QLabel(cap);
        h->setProperty("role", "sectionHeader");
        auto* v = new QLabel(QStringLiteral("—"));
        QFont vf = v->font();
        vf.setWeight(QFont::DemiBold);
        v->setFont(vf);
        col->addWidget(h);
        col->addWidget(v);
        return std::make_pair(col, v);
    };

    auto [upCol, upVal] = buildStat(tr("UPTIME"));
    auto [brCol, brVal] = buildStat(tr("LIVE BITRATE"));
    auto [drCol, drVal] = buildStat(tr("DROPS"));
    auto [fpsCol, fpsVal] = buildStat(tr("FPS"));
    m_statsUptimeLabel = upVal;
    m_statsBitrateLabel = brVal;
    m_statsDropsLabel = drVal;
    m_statsFpsLabel = fpsVal;

    statsLay->addWidget(m_statsHealthDot, 0, Qt::AlignVCenter);
    statsLay->addLayout(upCol);
    statsLay->addLayout(brCol);
    statsLay->addLayout(drCol);
    statsLay->addLayout(fpsCol);
    // Initial OFFLINE placeholder values — labels keep their
    // structure visible so the user knows the topbar has stats, and
    // they animate in for real on the first ffmpeg progress tick.
    if (m_statsUptimeLabel) m_statsUptimeLabel->setText(QStringLiteral("--:--:--"));
    if (m_statsBitrateLabel) m_statsBitrateLabel->setText(QStringLiteral("— kbps"));
    if (m_statsDropsLabel) m_statsDropsLabel->setText(QStringLiteral("—"));
    if (m_statsFpsLabel) m_statsFpsLabel->setText(QStringLiteral("—"));
    if (m_statsHealthDot)
        m_statsHealthDot->setStyleSheet(
            QStringLiteral("background-color:#666;border-radius:5px;"));

    m_goLiveButton = new QPushButton(tr("Go live"));
    m_goLiveButton->setProperty("role", "primary");
    m_goLiveButton->setIcon(QIcon(QStringLiteral(":/resources/icons/play.svg")));
    m_goLiveButton->setIconSize({16, 16});
    m_goLiveButton->setCursor(Qt::PointingHandCursor);
    connect(m_goLiveButton, &QPushButton::clicked,
            this, &MainWindow::onGoLiveClicked);

    headerLay->addLayout(leftCol, 2);
    headerLay->addLayout(midCol, 1);
    headerLay->addWidget(m_statsBlock, 0);
    headerLay->addWidget(m_statusPill, 0);
    headerLay->addWidget(m_goLiveButton, 0);

    // Preview card
    auto* previewCard = makeCard();
    auto* previewLay = new QVBoxLayout(previewCard);
    previewLay->setContentsMargins(0, 0, 0, 0);
    m_preview = new PreviewWidget(m_theme);
    previewLay->addWidget(m_preview);
    connect(m_preview, &PreviewWidget::sourceTransformChanged,
            this, [this](const QString& id, const QRectF& t) {
                if (!m_sourcesPanel) return;
                auto sources = m_sourcesPanel->sources();
                for (auto& s : sources) {
                    if (s.id == id) {
                        s.transform = t;
                        break;
                    }
                }
                m_sourcesPanel->setSources(sources);
                m_settings.sources = sources;
                saveSettings(m_settings);
            });
    connect(m_preview, &PreviewWidget::sourceSelected,
            this, [this](const QString& id) {
                if (m_sourcesPanel) m_sourcesPanel->setSelectedSourceId(id);
            });

    // Sources card on the right of the preview.
    auto* sourcesCard = makeCard();
    sourcesCard->setMinimumWidth(280);
    sourcesCard->setMaximumWidth(360);
    auto* sourcesLay = new QVBoxLayout(sourcesCard);
    sourcesLay->setContentsMargins(16, 16, 16, 16);
    m_sourcesPanel = new SourcesPanel(m_theme);
    m_sourcesPanel->setSources(m_settings.sources);
    sourcesLay->addWidget(m_sourcesPanel, 1);
    connect(m_sourcesPanel, &SourcesPanel::sourcesChanged,
            this, &MainWindow::onSourcesChanged);
    connect(m_sourcesPanel, &SourcesPanel::selectionChanged,
            this, &MainWindow::onSourceSelectionChanged);

    // Compose preview + sources panel side-by-side.
    auto* mid = new QHBoxLayout;
    mid->setContentsMargins(0, 0, 0, 0);
    mid->setSpacing(20);
    mid->addWidget(previewCard, 1);
    mid->addWidget(sourcesCard, 0);

    // Audio mixer card — sits between the scene (preview + sources)
    // and the encoder log. Mirrors the audio rows from m_sourcesPanel,
    // adds per-strip mute + volume controls, and lets the user spin
    // up a microphone or desktop-audio source in two clicks.
    auto* mixerCard = makeCard();
    auto* mixerLay = new QVBoxLayout(mixerCard);
    mixerLay->setContentsMargins(20, 16, 20, 16);
    m_audioMixer = new AudioMixerPanel(m_theme);
    m_audioMixer->setSources(m_settings.sources);
    mixerLay->addWidget(m_audioMixer);
    connect(m_audioMixer, &AudioMixerPanel::sourcesChanged,
            this, &MainWindow::onMixerSourcesChanged);

    // Encoder log card — lives below the audio mixer inside the
    // scrollable bottom panel so the scene preview keeps the full
    // vertical height it had before the mixer was added.
    auto* logCard = makeCard();
    auto* logLay = new QVBoxLayout(logCard);
    logLay->setContentsMargins(20, 16, 20, 16);
    auto* logHeader = new QLabel(tr("ENCODER LOG"));
    logHeader->setProperty("role", "sectionHeader");
    m_logView = new QPlainTextEdit;
    m_logView->setObjectName(QStringLiteral("logView"));
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    // The log itself stays a generous height — the parent QScrollArea
    // is what caps the bottom-panel footprint, so users can scroll to
    // see more ffmpeg output without it eating into the preview.
    m_logView->setMinimumHeight(140);
    logLay->addWidget(logHeader);
    logLay->addWidget(m_logView);

    // Bottom panel: AUDIO MIXER + ENCODER LOG stacked inside a fixed-
    // height QScrollArea. The scroll area's height is bounded so it
    // can't push the preview out of view; if the mixer ends up with
    // many strips or the log accumulates lines, the user just scrolls.
    auto* bottomWrap = new QWidget;
    auto* bottomLay = new QVBoxLayout(bottomWrap);
    bottomLay->setContentsMargins(0, 0, 0, 0);
    bottomLay->setSpacing(12);
    bottomLay->addWidget(mixerCard);
    bottomLay->addWidget(logCard);

    auto* bottomScroll = new QScrollArea;
    bottomScroll->setWidget(bottomWrap);
    bottomScroll->setWidgetResizable(true);
    bottomScroll->setFrameShape(QFrame::NoFrame);
    bottomScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bottomScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    bottomScroll->setMinimumHeight(180);
    bottomScroll->setMaximumHeight(240);

    lay->addWidget(header);
    lay->addLayout(mid, 1);
    lay->addWidget(bottomScroll, 0);

    m_pages->addWidget(page);

    // Now that the SourcesPanel exists, wire its initial state into the
    // preview canvas.
    refreshActiveSourcePreview();
}

void MainWindow::buildAboutPage() {
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);

    auto* card = makeCard();
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(28, 28, 28, 28);
    cardLay->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Volchay Obs"));
    QFont tf = title->font();
    tf.setPointSize(20);
    tf.setWeight(QFont::Bold);
    title->setFont(tf);

    auto* sub = new QLabel(tr("Modern Twitch streaming client — C++/Qt6"));
    sub->setProperty("role", "sectionHeader");
    sub->setWordWrap(true);

    auto* body = new QLabel(tr(
        "<p>Minimalist streamer with presets following the official Twitch guidelines.<br>"
        "Themes: Light, Blackout (OLED), RGB (animated accent).</p>"
        "<p>The stream is sent to Twitch RTMP through a local <b>ffmpeg</b> — "
        "install it and make sure it is on PATH.</p>"
        "<p>Twitch Broadcasting Guidelines: "
        "<a href=\"https://help.twitch.tv/s/article/broadcasting-guidelines\">help.twitch.tv</a></p>"));
    body->setOpenExternalLinks(true);
    body->setWordWrap(true);

    // Open-log row: shows current log file path and opens its folder
    // in the system file manager. Makes the file logger discoverable
    // for users who need to diagnose a stream failure.
    auto* logRow = new QHBoxLayout;
    logRow->setSpacing(10);
    auto* openLogBtn = new QPushButton(tr("Open log folder"));
    openLogBtn->setIcon(QIcon(QStringLiteral(":/resources/icons/log.svg")));
    openLogBtn->setIconSize({16, 16});
    openLogBtn->setCursor(Qt::PointingHandCursor);
    openLogBtn->setProperty("role", "secondary");
    connect(openLogBtn, &QPushButton::clicked, this, [] {
        const QString dir = Logger::instance().logDir();
        LOG_I("ui", QStringLiteral("Opening log folder: %1").arg(dir));
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    auto* logPathLabel = new QLabel(tr("Log file: %1").arg(Logger::instance().logFilePath()));
    logPathLabel->setStyleSheet(QStringLiteral("color:#888;"));
    logPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    logPathLabel->setWordWrap(true);
    logRow->addWidget(openLogBtn, 0);
    logRow->addWidget(logPathLabel, 1);

    cardLay->addWidget(title);
    cardLay->addWidget(sub);
    cardLay->addWidget(body);
    cardLay->addLayout(logRow);
    cardLay->addStretch(1);

    lay->addWidget(card);
    m_pages->addWidget(page);
}

void MainWindow::selectNav(int index) {
    m_navStream->setProperty("selected", index == 0);
    m_navAbout->setProperty("selected", index == 1);
    m_navSettings->setProperty("selected", false);
    restyle(m_navStream);
    restyle(m_navAbout);
    restyle(m_navSettings);
    m_pages->setCurrentIndex(index);
}

void MainWindow::onSettingsClicked() {
    SettingsDialog dlg(m_settings, m_theme, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_settings = dlg.result();
        if (m_theme) {
            // Order matters: setAccent first so ThemeManager::m_userAccent
            // is up to date even under RGB. applyTheme() relies on
            // m_userAccent to restore the static color when leaving RGB.
            m_theme->setAccent(m_settings.accent);
            m_theme->applyTheme(m_settings.theme);
        }
        saveSettings(m_settings);
        refreshStreamSummary();
        LOG_I("settings", QStringLiteral("Settings saved: theme=%1, accent=%2, preset=%3")
                  .arg(static_cast<int>(m_settings.theme))
                  .arg(m_settings.accent.name())
                  .arg(m_settings.presetId));
    } else if (m_theme && m_theme->currentTheme() != Theme::Rgb) {
        // The dialog live-previews accent picks; revert to the saved one
        // when the user cancels so the rest of the app stays in sync.
        m_theme->setAccent(m_settings.accent);
    }
}

void MainWindow::refreshStreamSummary() {
    QString presetName = tr("Custom");
    if (auto* p = PresetManager::findById(m_settings.presetId)) {
        presetName = p->displayName;
    }
    m_presetLabel->setText(presetName);
    m_bitrateLabel->setText(
        QString("%1 kbps").arg(m_settings.config.videoBitrateKbps));
}

void MainWindow::onGoLiveClicked() {
    if (m_engine->isRunning()) {
        LOG_I("stream", QStringLiteral("User clicked Stop"));
        m_engine->stop();
        return;
    }
    LOG_I("stream", QStringLiteral("User clicked Go Live, target=%1, preset=%2, %3x%4@%5fps, %6kbps")
              .arg(m_settings.target.rtmpUrl)
              .arg(m_settings.presetId)
              .arg(m_settings.config.widthPx)
              .arg(m_settings.config.heightPx)
              .arg(m_settings.config.fps)
              .arg(m_settings.config.videoBitrateKbps));
    m_engine->setFfmpegPath(m_settings.ffmpegPath);
    m_engine->start(m_settings.config, m_settings.target, m_settings.sources);
}

void MainWindow::onStreamStarted() {
    LOG_I("stream", QStringLiteral("Stream started"));
    m_statusPill->setText(tr("LIVE"));
    m_statusPill->setProperty("live", true);
    restyle(m_statusPill);
    m_goLiveButton->setText(tr("Stop"));
    m_goLiveButton->setIcon(QIcon(QStringLiteral(":/resources/icons/stop.svg")));
    m_goLiveButton->setProperty("live", true);
    restyle(m_goLiveButton);
    if (m_preview) m_preview->setLive(true);
    // Stats block is always visible. Reset numbers — they fill in
    // from the first ffmpeg progress tick (usually ~1 s after start).
    if (m_statsUptimeLabel) m_statsUptimeLabel->setText(QStringLiteral("00:00:00"));
    if (m_statsBitrateLabel) m_statsBitrateLabel->setText(QStringLiteral("— kbps"));
    if (m_statsDropsLabel) m_statsDropsLabel->setText(QStringLiteral("0"));
    if (m_statsFpsLabel) m_statsFpsLabel->setText(QStringLiteral("—"));
    if (m_statsHealthDot)
        m_statsHealthDot->setStyleSheet(
            QStringLiteral("background-color:#2ecc71;border-radius:5px;"));
}

void MainWindow::onStreamStopped(int exitCode, QProcess::ExitStatus status) {
    LOG_I("stream", QStringLiteral("Stream stopped (exitCode=%1, status=%2)")
              .arg(exitCode).arg(static_cast<int>(status)));
    m_statusPill->setText(tr("OFFLINE"));
    m_statusPill->setProperty("live", false);
    restyle(m_statusPill);
    m_goLiveButton->setText(tr("Go live"));
    m_goLiveButton->setIcon(QIcon(QStringLiteral(":/resources/icons/play.svg")));
    m_goLiveButton->setProperty("live", false);
    restyle(m_goLiveButton);
    if (m_preview) m_preview->setLive(false);
    // Reset to placeholders but keep the block visible so the
    // user can see at a glance where the live numbers will land.
    if (m_statsUptimeLabel) m_statsUptimeLabel->setText(QStringLiteral("--:--:--"));
    if (m_statsBitrateLabel) m_statsBitrateLabel->setText(QStringLiteral("— kbps"));
    if (m_statsDropsLabel) m_statsDropsLabel->setText(QStringLiteral("—"));
    if (m_statsFpsLabel) m_statsFpsLabel->setText(QStringLiteral("—"));
    if (m_statsHealthDot)
        m_statsHealthDot->setStyleSheet(
            QStringLiteral("background-color:#666;border-radius:5px;"));
}

void MainWindow::onStreamStats(double fps,
                               double bitrateKbps,
                               int droppedFrames,
                               double timeSec,
                               double speed) {
    if (!m_statsBlock) return;

    const int secs = static_cast<int>(timeSec);
    if (m_statsUptimeLabel) {
        m_statsUptimeLabel->setText(QStringLiteral("%1:%2:%3")
            .arg(secs / 3600, 2, 10, QChar('0'))
            .arg((secs / 60) % 60, 2, 10, QChar('0'))
            .arg(secs % 60, 2, 10, QChar('0')));
    }
    if (m_statsBitrateLabel) {
        m_statsBitrateLabel->setText(
            QStringLiteral("%1 kbps").arg(bitrateKbps, 0, 'f', 0));
    }
    if (m_statsFpsLabel) {
        m_statsFpsLabel->setText(QStringLiteral("%1").arg(fps, 0, 'f', 0));
    }

    // Mirror OBS health logic: red if real-time encoding is behind
    // (speed < 0.9) or drops are growing fast; amber on lighter drift;
    // otherwise green.
    QColor dot(QStringLiteral("#2ecc71"));
    if (m_statsDropsLabel) {
        m_statsDropsLabel->setText(QString::number(droppedFrames));
    }
    if (speed > 0.0 && speed < 0.85) dot = QColor(QStringLiteral("#e74c3c"));
    else if (speed > 0.0 && speed < 0.97) dot = QColor(QStringLiteral("#f39c12"));
    if (droppedFrames > 60) dot = QColor(QStringLiteral("#e74c3c"));
    else if (droppedFrames > 10 && dot.name() == QStringLiteral("#2ecc71"))
        dot = QColor(QStringLiteral("#f39c12"));

    if (m_statsHealthDot) {
        m_statsHealthDot->setStyleSheet(
            QStringLiteral("background-color:%1;border-radius:5px;").arg(dot.name()));
        m_statsHealthDot->setToolTip(
            tr("speed=%1x, drops=%2").arg(speed, 0, 'f', 2).arg(droppedFrames));
    }
}

void MainWindow::onStreamLog(const QString& line) {
    if (m_logView) m_logView->appendPlainText(line);
}

void MainWindow::onStreamError(const QString& message) {
    LOG_E("stream", message);
    if (m_logView) {
        m_logView->appendPlainText(QStringLiteral("[ERROR] ") + message);
    }
    statusBar()->showMessage(message, 5000);
}

void MainWindow::onSourcesChanged() {
    if (!m_sourcesPanel) return;
    m_settings.sources = m_sourcesPanel->sources();
    saveSettings(m_settings);
    if (m_audioMixer) m_audioMixer->setSources(m_settings.sources);
    refreshActiveSourcePreview();
}

void MainWindow::onMixerSourcesChanged() {
    if (!m_audioMixer) return;
    m_settings.sources = m_audioMixer->sources();
    saveSettings(m_settings);
    if (m_sourcesPanel) m_sourcesPanel->setSources(m_settings.sources);
    refreshActiveSourcePreview();
}

void MainWindow::onSourceSelectionChanged() {
    refreshActiveSourcePreview();
}

void MainWindow::refreshActiveSourcePreview() {
    if (!m_preview || !m_sourcesPanel) return;
    const auto sources = m_sourcesPanel->sources();

    // Push the full list so the preview can render every enabled video
    // source on the same canvas (OBS-style scene).
    m_preview->setSources(sources);

    // Pick the active source for handles + drag/resize. Prefer the row
    // currently selected in the panel; fall back to the first enabled
    // video source.
    QString activeId;
    int row = m_sourcesPanel->selectedIndex();
    if (row >= 0 && row < sources.size() && sourceTypeIsVideo(sources[row].type)) {
        activeId = sources[row].id;
    } else {
        int idx = firstEnabledVideoIndex(sources);
        if (idx >= 0) activeId = sources[idx].id;
    }
    m_preview->setActiveSourceId(activeId);
}

} // namespace lumen
