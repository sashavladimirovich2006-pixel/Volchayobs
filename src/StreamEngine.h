#pragma once

#include "PresetManager.h"
#include "Source.h"

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

namespace lumen {

struct StreamTarget {
    // Twitch ingest URL — see https://help.twitch.tv/s/twitch-ingest-recommendation
    QString rtmpUrl  = QStringLiteral("rtmp://live.twitch.tv/app");
    QString streamKey;
};

// Wraps the ffmpeg child process(es) that push RTMP to Twitch.
//
// On Windows with audio sources we run a **two-process pipeline**:
//
//     [capture ffmpeg]  ddagrab → h264_nvenc → mpegts on stdout
//             |
//             | (Qt-managed pipe)
//             v
//     [mux ffmpeg]   mpegts on stdin + dshow audio → flv → RTMP
//
// Single-process ffmpeg with ddagrab + dshow audio + amix stalls the
// lavfi pull thread (audio demuxing dominates the main loop, ddagrab
// gets polled at ~6Hz instead of 60). Splitting into two processes
// gives the capture loop sole ownership of the video pull -- proven
// in user testing to recover full 60fps. See the README dev log.
//
// For sources without audio (or non-Windows), we keep the simpler
// single-process path. m_muxProc is null in that case.
class StreamEngine : public QObject {
    Q_OBJECT
public:
    explicit StreamEngine(QObject* parent = nullptr);
    ~StreamEngine() override;

    bool isRunning() const;

    void setFfmpegPath(const QString& path); // empty = auto-detect

    // Built command lines for the (one or two) ffmpeg processes that
    // implement a stream session. captureArgs is empty when the session
    // can be served by a single ffmpeg invocation (no audio, or non-
    // Windows where dshow contention isn't a concern).
    struct FfmpegPlan {
        QStringList captureArgs;  // process A: video capture + encode → pipe
        QStringList muxArgs;      // process B (or single): audio + mux → RTMP
    };
    static FfmpegPlan buildFfmpegPlan(const StreamConfig& cfg,
                                      const StreamTarget& target,
                                      const SourceList& sources,
                                      QString* failureReason = nullptr);

public slots:
    void start(const StreamConfig& cfg,
               const StreamTarget& target,
               const SourceList& sources);
    void stop();

signals:
    void started();
    void stopped(int exitCode, QProcess::ExitStatus status);
    void logLine(const QString& line);
    void errorOccurred(const QString& message);

    // Periodic ffmpeg progress snapshot, parsed out of stderr. Fields
    // mirror what ffmpeg emits on its single status line: encoder fps,
    // outgoing bitrate, dropped frames, encoded seconds, real-time speed
    // ratio. The UI uses these to render an OBS-like status pill. We
    // parse from the **mux** process when the pipeline is split, since
    // that's the one writing to RTMP.
    void statsUpdated(double fps,
                      double bitrateKbps,
                      int droppedFrames,
                      double timeSec,
                      double speed);

private slots:
    void onMuxReadyReadStandardError();
    void onCaptureReadyReadStandardError();
    void onMuxFinished(int exitCode, QProcess::ExitStatus status);
    void onCaptureFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError err);

private:
    // m_muxProc is the user-facing ffmpeg (writes to RTMP). It's always
    // present once a stream is running. m_captureProc is non-null only
    // for the two-process path; in single-process mode m_muxProc is the
    // only ffmpeg and reads inputs directly.
    QProcess* m_muxProc;
    QProcess* m_captureProc;
    QString   m_ffmpegPath; // empty = auto-detect via findFfmpeg()
    // Async shutdown ladder: q\n → terminate → kill, driven by these
    // single-shot timers so the GUI thread never blocks on waitForFinished.
    QTimer* m_terminateTimer;
    QTimer* m_killTimer;
    // Set while waitForStarted() is running. onProcessErrorOccurred()
    // suppresses its own emission during this window so start() can
    // produce a single, deduplicated error message.
    bool m_swallowProcessErrors = false;
};

} // namespace lumen
