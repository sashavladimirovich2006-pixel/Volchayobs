#pragma once

#include "PresetManager.h"
#include "Source.h"
#include "WasapiLoopback.h"

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <memory>
#include <vector>

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
//     [mux ffmpeg]   mpegts on stdin + WASAPI named pipes (raw f32le) → flv → RTMP
//
// Single-process ffmpeg with ddagrab + dshow audio + amix stalls the
// lavfi pull thread (audio demuxing dominates the main loop, ddagrab
// gets polled at ~6Hz instead of 60). Splitting into two processes
// gives the capture loop sole ownership of the video pull -- proven
// in user testing to recover full 60fps. See the README dev log.
//
// Audio sources do NOT go through ffmpeg-dshow anymore. dshow capture
// devices stamp packets with the device's *uptime* (5-hour absolute
// offset on a long-running box), which froze the mux trying to sync
// audio against a zero-based video pipe — that was the root cause of
// 14 iterations of stream stalls. Instead, each enabled microphone /
// desktop-audio source spins up its own WasapiLoopback worker that
// captures the endpoint's mix format (f32le 48k stereo by default) and
// pumps the raw samples into a Windows named pipe; the mux ffmpeg reads
// each pipe with `-f f32le -ar <sr> -ac <ch>`. WASAPI hands us
// near-zero-based timestamps, so the offset (and the stalls it caused)
// simply don't exist on this path.
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
    //
    // On Windows, audio sources are pumped through Windows named pipes
    // by WasapiLoopback workers — one per source. AudioCapturePlan
    // describes a single such pipe so start() can spin up the matching
    // WasapiLoopback before the ffmpeg client tries to connect.
    struct AudioCapturePlan {
        int     sourceIndex = -1;        // index into the SourceList we built from
        QString pipeName;                // full Win32 form, e.g. \\.\pipe\volchay-...
        bool    loopback = true;         // true: render+LOOPBACK (desktop); false: mic
        double  volume   = 1.0;          // applied via `volume=` filter on mux
    };
    struct FfmpegPlan {
        QStringList                captureArgs;   // process A: video capture + encode → pipe
        QStringList                muxArgs;       // process B (or single): audio + mux → RTMP
        std::vector<AudioCapturePlan> audioPipes; // Windows-only WASAPI sources, in input order
    };
    // The plan is built in two phases: first sizes and decides which
    // audio sources will be WASAPI-pumped, then the engine calls back
    // with the actual sampleRate/channels each WasapiLoopback published
    // so the right `-ar`/`-ac` flags land in muxArgs. For tests and
    // pure non-Windows paths the static helper still works as before
    // (audioPipes will be empty).
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
    // WASAPI audio pumps — one per enabled mic/desktop source on Windows.
    // Each owns a named pipe whose path is what mux ffmpeg connects to
    // as a `-f f32le -i ...` input. Reset on every start()/stop() so the
    // mux ffmpeg and the pump start and stop together.
    std::vector<std::unique_ptr<WasapiLoopback>> m_audioCaptures;
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
