#pragma once

#include "SettingsDialog.h"
#include "StreamEngine.h"
#include "ThemeManager.h"

#include <QMainWindow>

class QPushButton;
class QPlainTextEdit;
class QStackedWidget;
class QLabel;

namespace lumen {

class PreviewWidget;
class AccentBadge;
class SourcesPanel;
class AudioMixerPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(ThemeManager* theme, QWidget* parent = nullptr);

private slots:
    void onGoLiveClicked();
    void onSettingsClicked();
    void onStreamStarted();
    void onStreamStopped(int exitCode, QProcess::ExitStatus status);
    void onStreamLog(const QString& line);
    void onStreamError(const QString& message);
    void onStreamStats(double fps,
                      double bitrateKbps,
                      int droppedFrames,
                      double timeSec,
                      double speed);
    void onSourcesChanged();
    void onSourceSelectionChanged();
    void onMixerSourcesChanged();

private:
    void buildSidebar();
    void buildStreamPage();
    void buildAboutPage();
    void selectNav(int index);
    void refreshStreamSummary();
    void refreshActiveSourcePreview();

    ThemeManager*  m_theme;
    StreamEngine*  m_engine;
    Settings       m_settings;

    QStackedWidget* m_pages = nullptr;
    QPushButton*    m_navStream  = nullptr;
    QPushButton*    m_navSettings = nullptr;
    QPushButton*    m_navAbout   = nullptr;

    PreviewWidget*  m_preview = nullptr;
    AccentBadge*    m_badge = nullptr;
    QLabel*         m_statusPill = nullptr;
    QLabel*         m_presetLabel = nullptr;
    QLabel*         m_bitrateLabel = nullptr;
    // OBS-style live status: small column shown only while streaming
    // with uptime, real outgoing bitrate, dropped frames, encoder fps,
    // and a coloured health dot (green / amber / red).
    QWidget*        m_statsBlock = nullptr;
    QLabel*         m_statsHealthDot = nullptr;
    QLabel*         m_statsUptimeLabel = nullptr;
    QLabel*         m_statsBitrateLabel = nullptr;
    QLabel*         m_statsDropsLabel = nullptr;
    QLabel*         m_statsFpsLabel = nullptr;
    QPushButton*    m_goLiveButton = nullptr;
    QPlainTextEdit* m_logView = nullptr;
    SourcesPanel*    m_sourcesPanel = nullptr;
    AudioMixerPanel* m_audioMixer = nullptr;
};

} // namespace lumen
