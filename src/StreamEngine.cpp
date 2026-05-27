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
            // ddagrab uses DXGI Desktop Duplication — much lower CPU/latency than gdigrab.
            // Falls back to gdigrab if ddagrab is unavailable (older Windows/drivers).
            const int screenIdx = (idx >= 0 && idx < screens.size()) ? idx : 0;
            args << "-thread_queue_size" << "512"
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
    args << "-thread_queue_size" << "512"
         << "-f" << "dshow" << "-i" << QString("audio=%1").arg(id);
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
      m_proc(new QProcess(this)),
      m_terminateTimer(new QTimer(this)),
      m_killTimer(new QTimer(this)) {
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_proc, &QProcess::readyReadStandardError,
            this, &StreamEngine::onReadyReadStandardError);
    connect(m_proc, &QProcess::finished,
            this, &StreamEngine::onFinished);
    connect(m_proc, &QProcess::errorOccurred,
            this, &StreamEngine::onErrorOccurred);

    m_terminateTimer->setSingleShot(true);
    m_killTimer->setSingleShot(true);
    connect(m_terminateTimer, &QTimer::timeout, this, [this] {
        if (m_proc->state() == QProcess::Running) {
            m_proc->terminate();
            m_killTimer->start(2000);
        }
    });
    connect(m_killTimer, &QTimer::timeout, this, [this] {
        if (m_proc->state() == QProcess::Running) {
            m_proc->kill();
        }
    });
}

StreamEngine::~StreamEngine() {
    m_terminateTimer->stop();
    m_killTimer->stop();
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

void StreamEngine::setFfmpegPath(const QString& path) {
    m_ffmpegPath = path;
}

bool StreamEngine::isRunning() const {
    return m_proc->state() == QProcess::Running;
}

QStringList StreamEngine::buildFfmpegArgs(const StreamConfig& cfg,
                                          const StreamTarget& target,
                                          const SourceList& sources,
                                          QString* failureReason) {
    auto fail = [&](const QString& msg) -> QStringList {
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

    QStringList args;
    args << "-hide_banner" << "-loglevel" << "info";
    args << videoArgs;

    // Collect every enabled audio source. MediaFile carries audio inline,
    // so we enable that pass-through when it's the primary video source.
    QList<int> audioInputs; // indices in `sources`
    for (int i = 0; i < sources.size(); ++i) {
        const Source& s = sources[i];
        if (!s.enabled) continue;
        if (s.type == SourceType::Microphone || s.type == SourceType::DesktopAudio) {
            audioInputs.push_back(i);
        }
    }
    int audioInputCount = 0;
    constexpr int firstAudioInputIndex = 1; // video is always input 0

    // Append each native audio device as its own ffmpeg input.
    for (int i : audioInputs) {
        const QStringList a = audioInputArgsFor(sources[i]);
        if (!a.isEmpty()) {
            args << a;
            ++audioInputCount;
        }
    }

    // If the primary source is a media file or test pattern with audio,
    // and the user didn't explicitly add audio sources, lift the audio
    // straight from input 0 so the output isn't silent.
    bool primaryHasAudio = false;
    if (sources[videoIdx].type == SourceType::MediaFile) {
        primaryHasAudio = true;
    }
    if (audioInputCount == 0 && sources[videoIdx].type == SourceType::TestPattern) {
        // testsrc2 doesn't carry audio — synthesise a sine tone so the FLV
        // muxer doesn't choke and Twitch's player still has a track.
        args << "-f" << "lavfi"
             << "-i" << QString("sine=frequency=440:sample_rate=%1")
                            .arg(cfg.audioSampleRateHz);
        ++audioInputCount;
    }

    // Build the audio routing. We always go through filter_complex when
    // there's at least one audio input so per-source volume sliders
    // (Source.settings[AUDIO_VOLUME]) take effect — both for a single
    // input and for the multi-input amix case.
    auto volumeFor = [](const Source& s) {
        const QVariant v = s.settings.value(SourceFields::AUDIO_VOLUME, 1.0);
        bool ok = false;
        double d = v.toDouble(&ok);
        if (!ok) d = 1.0;
        return qBound(0.0, d, 4.0);
    };
    QString audioMap;
    if (audioInputCount == 0 && !primaryHasAudio) {
        // no audio at all
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
            // Match `inputIdx` back to its Source so we can pick up the
            // user's volume slider; fall back to 1.0 if we can't find it
            // (e.g. the synthesised sine for TestPattern with no mic).
            double vol = 1.0;
            int counted = 0;
            for (int i : audioInputs) {
                if (audioInputArgsFor(sources[i]).isEmpty()) continue;
                if (counted == j) { vol = volumeFor(sources[i]); break; }
                ++counted;
            }
            const QString label = QString("[a%1]").arg(branchIdx++);
            filter += QString("[%1:a]volume=%2").arg(inputIdx)
                          .arg(QString::number(vol, 'f', 3)) + label + ";";
            branchLabels << label;
        }
        if (branchLabels.size() == 1) {
            // Single branch — just rewrite the label to [aout] so we don't
            // pay the cost of a 1-input amix.
            filter.chop(branchLabels.first().size() + 1); // drop "[aN];"
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

    args << "-map" << QStringLiteral("0:v");
    if (!audioMap.isEmpty()) args << "-map" << audioMap;

    // ---- Video encoder ----
    args << "-c:v" << encoderName(cfg.encoder);
    if (cfg.encoder == Encoder::X264) {
        args << "-preset" << cfg.x264Preset
             << "-profile:v" << cfg.profile
             << "-tune" << "zerolatency";
    }
    args << "-pix_fmt" << "yuv420p"
         << "-b:v" << QString("%1k").arg(cfg.videoBitrateKbps)
         << "-g" << QString::number(cfg.fps * cfg.keyframeIntervalSec)
         << "-keyint_min" << QString::number(cfg.fps * cfg.keyframeIntervalSec)
         << "-r" << QString::number(cfg.fps);

    const QString rcMode = rateControlMode(cfg.rateControl, cfg.encoder);
    if (cfg.encoder == Encoder::X264) {
        if (cfg.rateControl == RateControl::CBR) {
            args << "-maxrate" << QString("%1k").arg(cfg.videoBitrateKbps)
                 << "-bufsize" << QString("%1k").arg(cfg.videoBitrateKbps * 2)
                 << "-x264-params"
                 << QString("nal-hrd=cbr:keyint=%1:min-keyint=%1")
                        .arg(cfg.fps * cfg.keyframeIntervalSec);
        } else if (cfg.rateControl == RateControl::VBR) {
            args << "-maxrate" << QString("%1k").arg(cfg.videoBitrateKbps * 3 / 2)
                 << "-bufsize" << QString("%1k").arg(cfg.videoBitrateKbps * 2);
        } else {
            args << "-crf" << QStringLiteral("23");
        }
    } else {
        args << "-rc" << rcMode;
        if (cfg.rateControl == RateControl::CBR) {
            args << "-maxrate" << QString("%1k").arg(cfg.videoBitrateKbps)
                 << "-bufsize" << QString("%1k").arg(cfg.videoBitrateKbps * 2);
        } else if (cfg.rateControl == RateControl::VBR) {
            args << "-maxrate" << QString("%1k").arg(cfg.videoBitrateKbps * 3 / 2)
                 << "-bufsize" << QString("%1k").arg(cfg.videoBitrateKbps * 2);
        }
    }

    // ---- Audio encoder ----
    if (!audioMap.isEmpty()) {
        args << "-c:a" << "aac"
             << "-b:a" << QString("%1k").arg(cfg.audioBitrateKbps)
             << "-ar" << QString::number(cfg.audioSampleRateHz)
             << "-ac" << "2";
    }

    // ---- Output (RTMP) ----
    QString rtmpFull = target.rtmpUrl;
    if (!rtmpFull.endsWith('/')) rtmpFull += '/';
    rtmpFull += target.streamKey;
    args << "-f" << "flv" << rtmpFull;

    return args;
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
    const QStringList args = buildFfmpegArgs(cfg, target, sources, &reason);
    if (args.isEmpty()) {
        emit errorOccurred(reason.isEmpty()
            ? tr("Video source is not configured.") : reason);
        return;
    }

    QStringList loggable = args;
    if (!loggable.isEmpty()) {
        loggable.last() = QStringLiteral("<rtmp-url-with-stream-key-hidden>");
    }
    emit logLine(QStringLiteral("$ ffmpeg ") + loggable.join(' '));
    m_proc->setProgram(m_ffmpegPath.isEmpty() ? findFfmpeg() : m_ffmpegPath);
    m_proc->setArguments(args);
    m_swallowProcessErrors = true;
    m_proc->start();
    const bool ok = m_proc->waitForStarted(3000);
    m_swallowProcessErrors = false;
    if (!ok) {
        emit errorOccurred(tr("Failed to launch ffmpeg. Make sure ffmpeg is installed and on PATH."));
        return;
    }
    emit started();
}

void StreamEngine::stop() {
    if (!isRunning()) return;
    m_proc->write("q\n");
    m_terminateTimer->start(3000);
}

void StreamEngine::onReadyReadStandardError() {
    // ffmpeg prints its progress status as a single ever-updating line
    // separated by \r, not \n. We split on both so the UI can see live
    // updates without waiting for a newline that never arrives.
    const QByteArray chunk = m_proc->readAllStandardError();
    QByteArray buf = chunk;
    buf.replace('\r', '\n');
    for (const QByteArray& line : buf.split('\n')) {
        const QString s = QString::fromUtf8(line).trimmed();
        if (s.isEmpty()) continue;
        emit logLine(s);

        // Parse the running progress line, which looks like:
        //   frame= 1234 fps= 60 q=23.0 size=  12345kB time=00:00:20.55
        //   bitrate=4920.3kbits/s dup=0 drop=2 speed=1.00x
        // Only fire statsUpdated when we see the canonical "frame=" line
        // so other log noise doesn't reset the UI.
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

void StreamEngine::onFinished(int exitCode, QProcess::ExitStatus status) {
    m_terminateTimer->stop();
    m_killTimer->stop();
    emit stopped(exitCode, status);
}

void StreamEngine::onErrorOccurred(QProcess::ProcessError err) {
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
