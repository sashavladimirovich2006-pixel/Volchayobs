#include "StreamEngine.h"

#include "Devices.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringBuilder>

namespace lumen {

namespace {

QString findFfmpeg() {
    // 1. Already on PATH
    QString found = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!found.isEmpty()) return found;

    // 2. Next to our own executable
    QString appDir = QCoreApplication::applicationDirPath();
    for (const QString& rel : {QStringLiteral("ffmpeg.exe"), QStringLiteral("ffmpeg/bin/ffmpeg.exe")}) {
        QString candidate = appDir + '/' + rel;
        if (QFileInfo::exists(candidate)) return candidate;
    }

    // 3. Common Windows install locations
    const QStringList roots = {
        qEnvironmentVariable("ProgramFiles"),
        qEnvironmentVariable("ProgramFiles(x86)"),
        qEnvironmentVariable("LOCALAPPDATA"),
        QStringLiteral("C:/ffmpeg"),
        QStringLiteral("C:/tools/ffmpeg"),
    };
    for (const QString& root : roots) {
        if (root.isEmpty()) continue;
        for (const QString& rel : {QStringLiteral("ffmpeg/bin/ffmpeg.exe"), QStringLiteral("bin/ffmpeg.exe"), QStringLiteral("ffmpeg.exe")}) {
            QString candidate = root + '/' + rel;
            if (QFileInfo::exists(candidate)) return candidate;
        }
    }

    return QStringLiteral("ffmpeg"); // fallback — will fail with a clear error
}

QString encoderName(Encoder e) {
    switch (e) {
        case Encoder::X264:       return QStringLiteral("libx264");
        case Encoder::NVENC_H264: return QStringLiteral("h264_nvenc");
        case Encoder::QSV_H264:   return QStringLiteral("h264_qsv");
        case Encoder::AMF_H264:   return QStringLiteral("h264_amf");
    }
    return QStringLiteral("libx264");
}

QString rateControlMode(RateControl rc, Encoder e) {
    // Map our enum to encoder-specific ffmpeg `-rc` flag values.
    switch (e) {
        case Encoder::X264:
            return rc == RateControl::CBR ? QStringLiteral("cbr") :
                   rc == RateControl::VBR ? QStringLiteral("vbr") :
                                            QStringLiteral("crf");
        case Encoder::NVENC_H264:
            return rc == RateControl::CBR ? QStringLiteral("cbr") :
                   rc == RateControl::VBR ? QStringLiteral("vbr_hq") :
                                            QStringLiteral("constqp");
        case Encoder::QSV_H264:
            return rc == RateControl::CBR ? QStringLiteral("cbr") :
                   rc == RateControl::VBR ? QStringLiteral("vbr") :
                                            QStringLiteral("icq");
        case Encoder::AMF_H264:
            return rc == RateControl::CBR ? QStringLiteral("cbr") :
                   rc == RateControl::VBR ? QStringLiteral("vbr_peak") :
                                            QStringLiteral("cqp");
    }
    return QStringLiteral("cbr");
}

// Produce ffmpeg -i input args for one video source. Returns {} and
// optionally fills *failureReason if the source can't be expressed
// for the current OS or its required field is empty.
QStringList videoInputArgsFor(const Source& s, const StreamConfig& cfg,
                              QString* failureReason) {
    const QString fps = QString::number(cfg.fps);
    const QString sz  = QString("%1x%2").arg(cfg.widthPx).arg(cfg.heightPx);

    auto fail = [&](const QString& msg) -> QStringList {
        if (failureReason) *failureReason = msg;
        return {};
    };

    switch (s.type) {
        case SourceType::DisplayCapture: {
            const int idx = s.settings.value(SourceFields::SCREEN_INDEX, 0).toInt();
            const auto screens = enumerateScreens();
#if defined(Q_OS_WIN)
            QStringList args;
            // ddagrab via -f lavfi: GPU-accelerated DXGI Desktop Duplication.
            // We tried gdigrab as a "real demuxer with its own thread" hack
            // to dodge what looked like main-thread starvation from dshow
            // audio, but gdigrab on this user's box was actually slower
            // (2 unique fps vs ddagrab's 6) — so the bottleneck is not
            // pull-vs-push. ddagrab is the lesser evil while we keep
            // hunting the real cause.
            const int screenIdx = (idx >= 0 && idx < screens.size()) ? idx : 0;
            args << "-thread_queue_size" << "1024"
                 << "-f" << "lavfi"
                 << "-i" << QString("ddagrab=output_idx=%1:draw_mouse=1:framerate=%2")
                              .arg(screenIdx).arg(fps);
            return args;
#elif defined(Q_OS_MACOS)
            QStringList args;
            args << "-f" << "avfoundation"
                 << "-framerate" << fps
                 << "-video_size" << sz
                 << "-i" << QString::number(qMax(0, idx)) + ":";
            return args;
#else
            const QString display = qEnvironmentVariable("DISPLAY", ":0.0");
            int x = 0, y = 0, w = cfg.widthPx, h = cfg.heightPx;
            if (idx >= 0 && idx < screens.size()) {
                const auto& sc = screens[idx];
                x = sc.x; y = sc.y; w = sc.w; h = sc.h;
            }
            QStringList args;
            // -draw_mouse 1 keeps the X11 cursor visible in the captured
            // stream (matches the gdigrab path on Windows).
            args << "-f" << "x11grab"
                 << "-draw_mouse" << "1"
                 << "-framerate" << fps
                 << "-video_size" << QString("%1x%2").arg(w).arg(h)
                 << "-i" << QString("%1+%2,%3").arg(display).arg(x).arg(y);
            return args;
#endif
        }
        case SourceType::WindowCapture: {
            const QString title = s.settings.value(SourceFields::WINDOW_TITLE).toString();
            if (title.trimmed().isEmpty())
                return fail(QObject::tr("«%1»: window title is not set.").arg(s.name));
#if defined(Q_OS_WIN)
            QStringList args;
            args << "-f" << "gdigrab"
                 << "-framerate" << fps
                 << "-i" << QString("title=%1").arg(title);
            return args;
#else
            return fail(QObject::tr(
                "«%1»: window capture by title is only available on Windows.")
                    .arg(s.name));
#endif
        }
        case SourceType::VideoCaptureDevice: {
            const QString id = s.settings.value(SourceFields::VIDEO_DEVICE_ID).toString();
            if (id.isEmpty())
                return fail(QObject::tr("«%1»: device not selected.").arg(s.name));
#if defined(Q_OS_WIN)
            QStringList args;
            args << "-f" << "dshow"
                 << "-video_size" << sz
                 << "-framerate" << fps
                 << "-i" << QString("video=%1").arg(id);
            return args;
#elif defined(Q_OS_MACOS)
            QStringList args;
            args << "-f" << "avfoundation"
                 << "-video_size" << sz
                 << "-framerate" << fps
                 << "-i" << QString("%1:").arg(id);
            return args;
#else
            // Linux v4l2 device path (e.g. /dev/video0).
            QStringList args;
            args << "-f" << "v4l2"
                 << "-video_size" << sz
                 << "-framerate" << fps
                 << "-i" << id;
            return args;
#endif
        }
        case SourceType::MediaFile: {
            const QString path = s.settings.value(SourceFields::FILE_PATH).toString();
            if (path.isEmpty() || !QFileInfo::exists(path))
                return fail(QObject::tr("«%1»: file not found (%2).")
                                .arg(s.name, path));
            QStringList args;
            const bool loop = s.settings.value(SourceFields::LOOP, true).toBool();
            args << "-re";
            if (loop) args << "-stream_loop" << "-1";
            args << "-i" << path;
            return args;
        }
        case SourceType::Image: {
            const QString path = s.settings.value(SourceFields::FILE_PATH).toString();
            if (path.isEmpty() || !QFileInfo::exists(path))
                return fail(QObject::tr("«%1»: image not found (%2).")
                                .arg(s.name, path));
            // -loop 1 + -framerate keeps the still image producing frames at
            // the configured fps so it can be a video input alongside live
            // sources.
            QStringList args;
            args << "-loop" << "1"
                 << "-framerate" << fps
                 << "-i" << path;
            return args;
        }
        case SourceType::ColorSource: {
            const QColor c(s.settings.value(SourceFields::COLOR_RGB,
                            QStringLiteral("#000000")).toString());
            QStringList args;
            args << "-f" << "lavfi"
                 << "-i" << QString("color=c=%1:s=%2:r=%3")
                                .arg(c.name(QColor::HexRgb).mid(1))
                                .arg(sz, fps);
            return args;
        }
        case SourceType::TextSource: {
            const QString text = s.settings.value(SourceFields::TEXT_BODY).toString();
            const int fontSize = s.settings.value(SourceFields::TEXT_SIZE, 48).toInt();
            const QString textColor = s.settings.value(
                SourceFields::TEXT_COLOR, QStringLiteral("#ffffff")).toString();
            const QString bgColor = s.settings.value(
                SourceFields::TEXT_BG_COLOR, QStringLiteral("#000000")).toString();
            // ffmpeg drawtext escapes :, ', \ — keep it simple by replacing
            // the few characters we know are problematic.
            QString safe = text;
            safe.replace('\\', QStringLiteral("\\\\"));
            safe.replace('\'', QStringLiteral("\\'"));
            safe.replace(':',  QStringLiteral("\\:"));
            QStringList args;
            args << "-f" << "lavfi"
                 << "-i" << QString("color=c=%1:s=%2:r=%3,drawtext=text='%4':"
                                    "fontcolor=%5:fontsize=%6:x=(w-text_w)/2:"
                                    "y=(h-text_h)/2")
                                .arg(QColor(bgColor).name(QColor::HexRgb).mid(1))
                                .arg(sz, fps, safe)
                                .arg(QColor(textColor).name(QColor::HexRgb).mid(1))
                                .arg(fontSize);
            return args;
        }
        case SourceType::TestPattern: {
            QStringList args;
            args << "-f" << "lavfi"
                 << "-i" << QString("testsrc2=size=%1:rate=%2").arg(sz, fps);
            return args;
        }
        case SourceType::BrowserSource:
            // Browser sources render to the preview via QtWebEngine but
            // are not yet piped into ffmpeg's stdin. Returning {} keeps
            // them out of the encode graph so the stream stays valid
            // until the WebEngine→ffmpeg pipe is wired up.
            return {};
        case SourceType::Microphone:
        case SourceType::DesktopAudio:
            // Audio-only sources don't contribute video.
            return {};
    }
    return {};
}

QStringList audioInputArgsFor(const Source& s) {
    QStringList args;
    const QString id = s.settings.value(SourceFields::AUDIO_DEVICE_ID).toString();
    if (id.isEmpty()) return args;

#if defined(Q_OS_WIN)
    // -rtbufsize 64M lets dshow buffer up to 64MB of audio internally
    // before producing packets. Default is 3MB, which on this user's
    // box was too small with two simultaneous USB mics — once the kernel
    // dshow ring filled, dshow producer threads stalled, and the stall
    // back-pressured the ffmpeg main loop's video pull.
    args << "-thread_queue_size" << "1024"
         << "-rtbufsize" << "64M"
         << "-f" << "dshow"
         << "-i" << QString("audio=%1").arg(id);
#elif defined(Q_OS_MACOS)
    args << "-f" << "avfoundation" << "-i" << QString(":%1").arg(id);
#else
    args << "-f" << "pulse" << "-i" << id;
#endif
    return args;
}

} // namespace

StreamEngine::StreamEngine(QObject* parent)
    : QObject(parent),
      m_muxProc(new QProcess(this)),
      m_captureProc(new QProcess(this)),
      m_terminateTimer(new QTimer(this)),
      m_killTimer(new QTimer(this)) {
    m_muxProc->setProcessChannelMode(QProcess::SeparateChannels);
    m_captureProc->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_muxProc, &QProcess::readyReadStandardError,
            this, &StreamEngine::onMuxReadyReadStandardError);
    connect(m_captureProc, &QProcess::readyReadStandardError,
            this, &StreamEngine::onCaptureReadyReadStandardError);
    connect(m_muxProc, &QProcess::finished,
            this, &StreamEngine::onMuxFinished);
    connect(m_captureProc, &QProcess::finished,
            this, &StreamEngine::onCaptureFinished);
    connect(m_muxProc, &QProcess::errorOccurred,
            this, &StreamEngine::onProcessErrorOccurred);
    connect(m_captureProc, &QProcess::errorOccurred,
            this, &StreamEngine::onProcessErrorOccurred);

    m_terminateTimer->setSingleShot(true);
    m_killTimer->setSingleShot(true);
    connect(m_terminateTimer, &QTimer::timeout, this, [this] {
        if (m_muxProc->state() == QProcess::Running) {
            m_muxProc->terminate();
        }
        if (m_captureProc->state() == QProcess::Running) {
            m_captureProc->terminate();
        }
        m_killTimer->start(2000);
    });
    connect(m_killTimer, &QTimer::timeout, this, [this] {
        if (m_muxProc->state() == QProcess::Running) {
            m_muxProc->kill();
        }
        if (m_captureProc->state() == QProcess::Running) {
            m_captureProc->kill();
        }
    });
}

StreamEngine::~StreamEngine() {
    m_terminateTimer->stop();
    m_killTimer->stop();
    for (QProcess* p : { m_muxProc, m_captureProc }) {
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(2000);
        }
    }
}

void StreamEngine::setFfmpegPath(const QString& path) {
    m_ffmpegPath = path;
}

bool StreamEngine::isRunning() const {
    return m_muxProc->state() == QProcess::Running
        || m_captureProc->state() == QProcess::Running;
}

StreamEngine::FfmpegPlan StreamEngine::buildFfmpegPlan(
        const StreamConfig& cfg,
        const StreamTarget& target,
        const SourceList& sources,
        QString* failureReason) {
    FfmpegPlan plan;
    auto fail = [&](const QString& msg) -> FfmpegPlan {
        if (failureReason) *failureReason = msg;
        return {};
    };

    // Locate the primary video source. Phase 1 streams exactly one.
    int videoIdx = firstEnabledVideoIndex(sources);
    if (videoIdx < 0) {
        return fail(QObject::tr(
            "No enabled video source. Add at least one on the main screen."));
    }
    QString videoFail;
    QStringList videoArgs = videoInputArgsFor(sources[videoIdx], cfg, &videoFail);
    if (videoArgs.isEmpty()) return fail(videoFail);

    // Collect every enabled native audio source.
    QList<int> audioInputs;
    for (int i = 0; i < sources.size(); ++i) {
        const Source& s = sources[i];
        if (!s.enabled) continue;
        if (s.type == SourceType::Microphone || s.type == SourceType::DesktopAudio) {
            audioInputs.push_back(i);
        }
    }

    const bool primaryHasAudio = (sources[videoIdx].type == SourceType::MediaFile);

    // Decide whether we need the two-process split pipeline. On Windows
    // with ddagrab in a lavfi source AND any dshow audio in the same
    // ffmpeg, the lavfi pull thread stalls (proven empirically: no audio
    // = 60fps; two mics + amix = 6fps; the video encoder itself runs
    // > 1.0x speed throughout, so it isn't the bottleneck). Splitting
    // capture and mux into separate processes gives the capture loop
    // sole ownership of the ddagrab pull and the audio handling stays
    // on a second ffmpeg that just reads pre-encoded video off stdin.
    const bool isDisplayCapture =
        (sources[videoIdx].type == SourceType::DisplayCapture);
    bool useTwoProcess = false;
#if defined(Q_OS_WIN)
    useTwoProcess = isDisplayCapture && !audioInputs.isEmpty();
#endif

    // ---- Volume helper (shared) ----
    auto volumeFor = [](const Source& s) {
        const QVariant v = s.settings.value(SourceFields::AUDIO_VOLUME, 1.0);
        bool ok = false;
        double d = v.toDouble(&ok);
        if (!ok) d = 1.0;
        return qBound(0.0, d, 4.0);
    };

    // ---- Decide if video filter is needed on the encode side ----
    bool encoderSeesD3D11 = false;
    QString videoFilter;
#if defined(Q_OS_WIN)
    {
        const bool isNvenc = (cfg.encoder == Encoder::NVENC_H264);
        bool needsScale = true;
        if (isDisplayCapture) {
            const auto screens = enumerateScreens();
            const int sIdx = sources[videoIdx]
                .settings.value(SourceFields::SCREEN_INDEX, 0).toInt();
            if (sIdx >= 0 && sIdx < screens.size()) {
                const auto& sc = screens[sIdx];
                needsScale = (sc.w != cfg.widthPx || sc.h != cfg.heightPx);
            }
        }
        if (isNvenc && isDisplayCapture && !needsScale) {
            encoderSeesD3D11 = true;
        } else if (isDisplayCapture) {
            videoFilter = needsScale
                ? QString("hwdownload,format=bgra,scale=%1:%2:flags=bilinear,format=yuv420p")
                      .arg(cfg.widthPx).arg(cfg.heightPx)
                : QStringLiteral("hwdownload,format=bgra,format=yuv420p");
        }
    }
#endif

    // ---- Helper: append encoder settings to an arg list ----
    auto appendVideoEncoder = [&](QStringList& a) {
        a << "-c:v" << encoderName(cfg.encoder);
        if (cfg.encoder == Encoder::X264) {
            a << "-preset" << cfg.x264Preset
              << "-profile:v" << cfg.profile
              << "-tune" << "zerolatency";
        } else if (cfg.encoder == Encoder::NVENC_H264) {
            a << "-tune" << "ll";
        }
        if (!encoderSeesD3D11) {
            a << "-pix_fmt" << "yuv420p";
        }
        a << "-b:v" << QString("%1k").arg(cfg.videoBitrateKbps)
          << "-g" << QString::number(cfg.fps * cfg.keyframeIntervalSec)
          << "-keyint_min" << QString::number(cfg.fps * cfg.keyframeIntervalSec);
        const QString rcMode = rateControlMode(cfg.rateControl, cfg.encoder);
        if (cfg.encoder == Encoder::X264) {
            if (cfg.rateControl == RateControl::CBR) {
                a << "-maxrate" << QString("%1k").arg(cfg.videoBitrateKbps)
                  << "-bufsize" << QString("%1k").arg(cfg.videoBitrateKbps * 2)
                  << "-x264-params"
                  << QString("nal-hrd=cbr:keyint=%1:min-keyint=%1")
                         .arg(cfg.fps * cfg.keyframeIntervalSec);
            } else if (cfg.rateControl == RateControl::VBR) {
                a << "-maxrate" << QString("%1k").arg(cfg.videoBitrateKbps * 3 / 2)
                  << "-bufsize" << QString("%1k").arg(cfg.videoBitrateKbps * 2);
            } else {
                a << "-crf" << QStringLiteral("23");
            }
        } else {
            a << "-rc" << rcMode;
            if (cfg.rateControl == RateControl::CBR) {
                a << "-maxrate" << QString("%1k").arg(cfg.videoBitrateKbps)
                  << "-bufsize" << QString("%1k").arg(cfg.videoBitrateKbps * 2);
            } else if (cfg.rateControl == RateControl::VBR) {
                a << "-maxrate" << QString("%1k").arg(cfg.videoBitrateKbps * 3 / 2)
                  << "-bufsize" << QString("%1k").arg(cfg.videoBitrateKbps * 2);
            }
        }
    };

    QString rtmpFull = target.rtmpUrl;
    if (!rtmpFull.endsWith('/')) rtmpFull += '/';
    rtmpFull += target.streamKey;

    // ===== Branch A: two-process pipeline (Windows + audio) =====
    if (useTwoProcess) {
        // ----- Capture process -----
        // Reads ddagrab → optional -vf → encode → MPEG-TS on stdout.
        // No audio anywhere in this process, so the lavfi pull thread
        // is never starved.
        QStringList& cap = plan.captureArgs;
        cap << "-hide_banner" << "-loglevel" << "info";
        cap << videoArgs;
        cap << "-map" << "0:v";
        if (!videoFilter.isEmpty()) cap << "-vf" << videoFilter;
        appendVideoEncoder(cap);
        // Real-time MPEG-TS over a pipe: default muxdelay 0.7s and
        // muxpreload 0.5s buffer ~1.2s of video inside mpegts muxer
        // before flush, hitting the pipe in bursts. Zero them so packets
        // leave the muxer as soon as they're produced.
        // iter 14 rolled back: -flush_packets 1 (too aggressive — caused
        // ~30k write() syscalls/sec at 188-byte mpegts packets) and -stats
        // (didn't actually produce progress lines on this piped stderr).
        cap << "-muxdelay" << "0"
            << "-muxpreload" << "0";
        // `-avoid_negative_ts make_zero` anchors the first packet at PTS 0
        // so the mux side doesn't see a video stream starting at 1.4s
        // while dshow audio starts at 15730s — that mismatch made mux
        // buffer audio and back-pressure the pipe.
        cap << "-avoid_negative_ts" << "make_zero";
        cap << "-f" << "mpegts" << "pipe:1";

        // ----- Mux process -----
        // Reads encoded video from stdin (input 0), then dshow audio
        // (inputs 1, 2, ...), mixes audio with the same filter graph
        // we used in the single-process path, and writes to RTMP.
        QStringList& mux = plan.muxArgs;
        mux << "-hide_banner" << "-loglevel" << "info";
        // `+genpts` regenerates PTS if any packet comes through without
        // one. `+igndts` keeps mux from stalling on dts jitter at startup.
        // iter 14 rolled back: +nobuffer and +discardcorrupt made the
        // first 4 seconds of stream show frame=0 (they dropped packets
        // waiting for a sync'd keyframe that never arrived because of
        // the dshow uptime offset). Net effect was strictly worse — the
        // stall pattern persisted AND the initial latency grew.
        mux << "-fflags" << "+genpts+igndts";
        // 16384 packets ≈ 4s of video burst headroom. Iter 12 used 4096
        // (~1s) and the stalls persisted, so we're overshooting hard to
        // rule out OS-pipe backpressure entirely.
        mux << "-thread_queue_size" << "16384";
        mux << "-f" << "mpegts" << "-i" << "pipe:0";

        int audioInputCount = 0;
        constexpr int firstAudioInputIndex = 1; // video is input 0 (pipe)
        for (int i : audioInputs) {
            const QStringList a = audioInputArgsFor(sources[i]);
            if (!a.isEmpty()) {
                mux << a;
                ++audioInputCount;
            }
        }
        if (audioInputCount > 0) {
            QString filter;
            QStringList branchLabels;
            int branchIdx = 0;
            for (int j = 0; j < audioInputCount; ++j) {
                const int inputIdx = firstAudioInputIndex + j;
                double vol = 1.0;
                int counted = 0;
                for (int i : audioInputs) {
                    if (audioInputArgsFor(sources[i]).isEmpty()) continue;
                    if (counted == j) { vol = volumeFor(sources[i]); break; }
                    ++counted;
                }
                const QString label = QString("[a%1]").arg(branchIdx++);
                // asetpts=N/SR/TB rewrites the audio PTS from sample count,
                // wiping out the absolute dshow uptime offset (observed:
                // start=14970s vs video pipe start=0s, a 4-hour gap that
                // froze mux trying to sync). aresample=async=1:first_pts=0
                // then locks the first output sample to PTS=0.
                // NB: 11-я итерация добавила asetpts только в single-process
                // ветке — Edit с replace_all пропустил это вхождение, и
                // two-process путь продолжал заикаться. Двенадцатая —
                // именно это и чинит.
                filter += QString("[%1:a]asetpts=N/SR/TB,aresample=async=1:first_pts=0,volume=%2")
                              .arg(inputIdx).arg(QString::number(vol, 'f', 3))
                       + label + ";";
                branchLabels << label;
            }
            if (branchLabels.size() == 1) {
                filter.chop(branchLabels.first().size() + 1);
                filter += QStringLiteral("[aout];");
            } else {
                filter += branchLabels.join("")
                       + QString("amix=inputs=%1:duration=longest:dropout_transition=0[aout];")
                             .arg(branchLabels.size());
            }
            if (filter.endsWith(';')) filter.chop(1);
            mux << "-filter_complex" << filter;
            mux << "-map" << "0:v:0" << "-map" << "[aout]";
        } else {
            mux << "-map" << "0:v:0";
        }

        // Video is already encoded — copy it through without re-encoding.
        // `-fps_mode passthrough` (was `-vsync passthrough`) tells the output
        // not to rewrite or sync video PTS; capture already produced stable
        // 60fps so mux just copies bytes through. **Output option** — must
        // sit between inputs and the output URL (iter 13.1 had it before
        // `-i pipe:0` and ffmpeg refused to start).
        mux << "-c:v" << "copy" << "-fps_mode" << "passthrough";
        if (audioInputCount > 0) {
            mux << "-c:a" << "aac"
                << "-b:a" << QString("%1k").arg(cfg.audioBitrateKbps)
                << "-ar" << QString::number(cfg.audioSampleRateHz)
                << "-ac" << "2";
        }
        // Anchor the first output packet at DTS 0 so Twitch (FLV) doesn't
        // see giant timestamps from the absolute dshow uptime — without
        // this, even after asetpts on the filter side, the FLV muxer can
        // still write negative or huge DTS if aresample emits a packet
        // before the first video keyframe lands.
        mux << "-avoid_negative_ts" << "make_zero";
        mux << "-f" << "flv" << rtmpFull;
        return plan;
    }

    // ===== Branch B: single-process pipeline =====
    QStringList& args = plan.muxArgs;
    args << "-hide_banner" << "-loglevel" << "info";
    args << videoArgs;

    int audioInputCount = 0;
    constexpr int firstAudioInputIndex = 1; // video is input 0
    for (int i : audioInputs) {
        const QStringList a = audioInputArgsFor(sources[i]);
        if (!a.isEmpty()) {
            args << a;
            ++audioInputCount;
        }
    }
    if (audioInputCount == 0 && sources[videoIdx].type == SourceType::TestPattern) {
        args << "-f" << "lavfi"
             << "-i" << QString("sine=frequency=440:sample_rate=%1")
                            .arg(cfg.audioSampleRateHz);
        ++audioInputCount;
    }

    QString audioMap;
    if (audioInputCount == 0 && !primaryHasAudio) {
        // no audio
    } else if (audioInputCount == 0 && primaryHasAudio) {
        audioMap = QStringLiteral("0:a?");
    } else {
        QString filter;
        QStringList branchLabels;
        int branchIdx = 0;
        if (primaryHasAudio) {
            const QString label = QString("[a%1]").arg(branchIdx++);
            filter += QStringLiteral("[0:a]anull") + label + ";";
            branchLabels << label;
        }
        for (int j = 0; j < audioInputCount; ++j) {
            const int inputIdx = firstAudioInputIndex + j;
            double vol = 1.0;
            int counted = 0;
            for (int i : audioInputs) {
                if (audioInputArgsFor(sources[i]).isEmpty()) continue;
                if (counted == j) { vol = volumeFor(sources[i]); break; }
                ++counted;
            }
            const QString label = QString("[a%1]").arg(branchIdx++);
            // asetpts=N/SR/TB rewrites the audio PTS from sample count,
            // wiping out the absolute dshow uptime offset (observed:
            // start=13917.837s vs video pipe start=1.4s, a 4-hour gap
            // that made mux freeze trying to sync). aresample=async=1
            // then nudges samples to align with the video stream.
            filter += QString("[%1:a]asetpts=N/SR/TB,aresample=async=1:first_pts=0,volume=%2")
                          .arg(inputIdx).arg(QString::number(vol, 'f', 3))
                   + label + ";";
            branchLabels << label;
        }
        if (branchLabels.size() == 1) {
            filter.chop(branchLabels.first().size() + 1);
            filter += QStringLiteral("[aout];");
        } else {
            filter += branchLabels.join("")
                   + QString("amix=inputs=%1:duration=longest:dropout_transition=0[aout];")
                         .arg(branchLabels.size());
        }
        if (filter.endsWith(';')) filter.chop(1);
        args << "-filter_complex" << filter;
        audioMap = QStringLiteral("[aout]");
    }

    args << "-map" << "0:v";
    if (!audioMap.isEmpty()) args << "-map" << audioMap;
    if (!videoFilter.isEmpty()) args << "-vf" << videoFilter;
    appendVideoEncoder(args);
    if (!audioMap.isEmpty()) {
        args << "-c:a" << "aac"
             << "-b:a" << QString("%1k").arg(cfg.audioBitrateKbps)
             << "-ar" << QString::number(cfg.audioSampleRateHz)
             << "-ac" << "2";
    }
    // Symmetric to the two-process path: keep DTS non-negative so FLV
    // is happy regardless of dshow's absolute uptime timestamps.
    args << "-avoid_negative_ts" << "make_zero";
    args << "-f" << "flv" << rtmpFull;
    return plan;
}

void StreamEngine::start(const StreamConfig& cfg,
                         const StreamTarget& target,
                         const SourceList& sources) {
    if (isRunning()) {
        emit errorOccurred(tr("Stream is already running."));
        return;
    }
    if (target.streamKey.trimmed().isEmpty()) {
        emit errorOccurred(tr("Stream Key is empty. Open Settings and paste the key from dashboard.twitch.tv."));
        return;
    }

    QString reason;
    const FfmpegPlan plan = buildFfmpegPlan(cfg, target, sources, &reason);
    if (plan.muxArgs.isEmpty()) {
        emit errorOccurred(reason.isEmpty()
            ? tr("Video source is not configured.") : reason);
        return;
    }

    auto logCmd = [&](const QString& prefix, QStringList a) {
        if (!a.isEmpty() && a.last().startsWith(QStringLiteral("rtmp"))) {
            a.last() = QStringLiteral("<rtmp-url-with-stream-key-hidden>");
        }
        emit logLine(prefix + a.join(' '));
    };

    const QString ffmpegExe =
        m_ffmpegPath.isEmpty() ? findFfmpeg() : m_ffmpegPath;
    m_swallowProcessErrors = true;

    if (!plan.captureArgs.isEmpty()) {
        // Two-process: capture | mux. Qt wires up the pipe for us via
        // setStandardOutputProcess — m_captureProc's stdout becomes
        // m_muxProc's stdin.
        logCmd(QStringLiteral("$ ffmpeg [capture] "), plan.captureArgs);
        logCmd(QStringLiteral("$ ffmpeg [mux] "), plan.muxArgs);
        m_captureProc->setProgram(ffmpegExe);
        m_captureProc->setArguments(plan.captureArgs);
        m_muxProc->setProgram(ffmpegExe);
        m_muxProc->setArguments(plan.muxArgs);
        m_captureProc->setStandardOutputProcess(m_muxProc);
        m_captureProc->start();
        m_muxProc->start();
        const bool okCap = m_captureProc->waitForStarted(3000);
        const bool okMux = m_muxProc->waitForStarted(3000);
        m_swallowProcessErrors = false;
        if (!okCap || !okMux) {
            if (m_captureProc->state() == QProcess::Running) m_captureProc->kill();
            if (m_muxProc->state() == QProcess::Running) m_muxProc->kill();
            emit errorOccurred(tr("Failed to launch ffmpeg. Make sure ffmpeg is installed and on PATH."));
            return;
        }
    } else {
        // Single-process path (no audio, or non-Windows, or non-DisplayCapture).
        logCmd(QStringLiteral("$ ffmpeg "), plan.muxArgs);
        m_muxProc->setProgram(ffmpegExe);
        m_muxProc->setArguments(plan.muxArgs);
        m_muxProc->start();
        const bool ok = m_muxProc->waitForStarted(3000);
        m_swallowProcessErrors = false;
        if (!ok) {
            emit errorOccurred(tr("Failed to launch ffmpeg. Make sure ffmpeg is installed and on PATH."));
            return;
        }
    }
    emit started();
}

void StreamEngine::stop() {
    if (!isRunning()) return;
    // Send 'q' to mux first — it stops gracefully, closes its stdin,
    // which makes the capture process see EOF and exit on its own.
    if (m_muxProc->state() == QProcess::Running) {
        m_muxProc->write("q\n");
    }
    if (m_captureProc->state() == QProcess::Running) {
        m_captureProc->write("q\n");
    }
    m_terminateTimer->start(3000);
}

void StreamEngine::onMuxReadyReadStandardError() {
    // ffmpeg prints its progress status as a single ever-updating line
    // separated by \r, not \n. We split on both so the UI can see live
    // updates without waiting for a newline that never arrives.
    const QByteArray chunk = m_muxProc->readAllStandardError();
    QByteArray buf = chunk;
    buf.replace('\r', '\n');
    for (const QByteArray& line : buf.split('\n')) {
        const QString s = QString::fromUtf8(line).trimmed();
        if (s.isEmpty()) continue;
        emit logLine(s);

        // Parse the running progress line, which looks like:
        //   frame= 1234 fps= 60 q=23.0 size=  12345kB time=00:00:20.55
        //   bitrate=4920.3kbits/s dup=0 drop=2 speed=1.00x
        if (!s.contains(QStringLiteral("frame=")) ||
            !s.contains(QStringLiteral("fps=")) ||
            !s.contains(QStringLiteral("bitrate="))) continue;

        auto extractDouble = [&](const QRegularExpression& re) -> double {
            const auto m = re.match(s);
            return m.hasMatch() ? m.captured(1).toDouble() : 0.0;
        };
        auto extractInt = [&](const QRegularExpression& re) -> int {
            const auto m = re.match(s);
            return m.hasMatch() ? m.captured(1).toInt() : 0;
        };

        static const QRegularExpression fpsRe(QStringLiteral(R"(fps=\s*([\d.]+))"));
        static const QRegularExpression brRe(QStringLiteral(R"(bitrate=\s*([\d.]+)kbits/s)"));
        static const QRegularExpression dropRe(QStringLiteral(R"(drop=\s*(\d+))"));
        static const QRegularExpression speedRe(QStringLiteral(R"(speed=\s*([\d.]+)x)"));
        static const QRegularExpression timeRe(
            QStringLiteral(R"(time=\s*(\d+):(\d+):([\d.]+))"));

        const double fps = extractDouble(fpsRe);
        const double bitrate = extractDouble(brRe);
        const int dropped = extractInt(dropRe);
        const double speed = extractDouble(speedRe);

        double timeSec = 0.0;
        const auto tm = timeRe.match(s);
        if (tm.hasMatch()) {
            timeSec = tm.captured(1).toInt() * 3600.0
                    + tm.captured(2).toInt() * 60.0
                    + tm.captured(3).toDouble();
        }

        emit statsUpdated(fps, bitrate, dropped, timeSec, speed);
    }
}

void StreamEngine::onCaptureReadyReadStandardError() {
    // Tag capture-side logs so the user can tell them apart from mux logs.
    const QByteArray chunk = m_captureProc->readAllStandardError();
    QByteArray buf = chunk;
    buf.replace('\r', '\n');
    for (const QByteArray& line : buf.split('\n')) {
        const QString s = QString::fromUtf8(line).trimmed();
        if (s.isEmpty()) continue;
        // Skip the noisy `frame=...` progress lines from the capture
        // side — the mux side's progress is what the user cares about
        // (it reflects what's actually being shipped to Twitch).
        if (s.startsWith(QStringLiteral("frame="))) continue;
        emit logLine(QStringLiteral("[capture] ") + s);
    }
}

void StreamEngine::onMuxFinished(int exitCode, QProcess::ExitStatus status) {
    // If capture is still running, kill it — mux exiting means we're done
    // streaming and we don't want a zombie producer.
    if (m_captureProc->state() == QProcess::Running) {
        m_captureProc->kill();
        m_captureProc->waitForFinished(2000);
    }
    m_terminateTimer->stop();
    m_killTimer->stop();
    emit stopped(exitCode, status);
}

void StreamEngine::onCaptureFinished(int /*exitCode*/, QProcess::ExitStatus /*status*/) {
    // Capture process ending alone is fine — mux will see EOF on stdin
    // and shut down on its own (which fires onMuxFinished, which emits
    // the public stopped signal). Nothing to do here.
}

void StreamEngine::onProcessErrorOccurred(QProcess::ProcessError err) {
    if (m_swallowProcessErrors) return;
    QString msg;
    switch (err) {
        case QProcess::FailedToStart:
            msg = tr("ffmpeg not found. Install ffmpeg and add it to PATH.");
            break;
        case QProcess::Crashed:
            msg = tr("ffmpeg crashed.");
            break;
        case QProcess::WriteError:
            msg = tr("Error writing to ffmpeg stdin.");
            break;
        case QProcess::ReadError:
            msg = tr("Error reading ffmpeg stderr.");
            break;
        default:
            msg = tr("ffmpeg process error.");
            break;
    }
    emit errorOccurred(msg);
}

} // namespace lumen
