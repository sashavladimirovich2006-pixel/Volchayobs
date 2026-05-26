#include "Devices.h"

#include <QAudioDevice>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QMediaDevices>
#include <QProcess>
#include <QRect>
#include <QRegularExpression>
#include <QScreen>

namespace lumen {

namespace {

// Qt-side enumeration of audio inputs. On every desktop platform Qt
// goes straight to the OS audio API (WASAPI on Windows, PulseAudio on
// Linux, Core Audio on macOS), so this list is always populated even
// when ffmpeg is missing from PATH.
QList<AudioDevice> qtAudioInputs() {
    QList<AudioDevice> out;
    const auto devices = QMediaDevices::audioInputs();
    for (const QAudioDevice& d : devices) {
        AudioDevice a;
        a.id = d.description();
        a.label = d.description();
        if (a.id.isEmpty()) {
            a.id = QString::fromUtf8(d.id());
            a.label = a.id;
        }
        out.push_back(a);
    }
    return out;
}

bool looksLikeLoopback(const QString& name) {
    const QString l = name.toLower();
    return l.contains("stereo mix")
        || l.contains("what u hear") || l.contains("what you hear")
        || l.contains("loopback") || l.contains("voicemeeter")
        || l.contains("cable output") || l.contains("vb-audio");
}

bool containsDeviceId(const QList<AudioDevice>& list, const QString& id) {
    for (const AudioDevice& d : list) {
        if (d.id.compare(id, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

// Run a short-lived helper, capture its merged stdout+stderr, and return
// the text. Bounded by a 3 second wait so a misbehaving binary can't hang
// the GUI when the user just opened the Settings dialog.
QString runAndCapture(const QString& program, const QStringList& args) {
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForStarted(1500)) return QString();
    p.waitForFinished(3000);
    return QString::fromUtf8(p.readAll());
}

#if defined(Q_OS_WIN)
#  include <windows.h>
#endif

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
// ffmpeg writes its device list to stderr in a free-form format. We parse
// the lines our backend cares about and ignore the rest. The exact regex
// targets the audio device blocks: lines that look like
//     [dshow @ ...]  "Microphone (Realtek)"
//     [AVFoundation indev @ ...] [0] Built-in Microphone
QStringList parseFfmpegDshowAudio(const QString& text) {
    QStringList out;
    bool audioBlock = false;
    const QStringList lines = text.split('\n');
    static const QRegularExpression nameRe(R"(\"([^\"]+)\")");
    for (const QString& raw : lines) {
        const QString l = raw.trimmed();
        if (l.contains("DirectShow audio devices", Qt::CaseInsensitive)) {
            audioBlock = true;
            continue;
        }
        if (l.contains("DirectShow video devices", Qt::CaseInsensitive)) {
            audioBlock = false;
            continue;
        }
        if (!audioBlock) continue;
        const auto m = nameRe.match(l);
        if (m.hasMatch() && !l.contains("Alternative name", Qt::CaseInsensitive)) {
            out << m.captured(1);
        }
    }
    return out;
}

// Same shape as parseFfmpegDshowAudio but pulls names from the "DirectShow
// video devices" block instead.
QStringList parseFfmpegDshowVideo(const QString& text) {
    QStringList out;
    bool videoBlock = false;
    const QStringList lines = text.split('\n');
    static const QRegularExpression nameRe(R"(\"([^\"]+)\")");
    for (const QString& raw : lines) {
        const QString l = raw.trimmed();
        if (l.contains("DirectShow video devices", Qt::CaseInsensitive)) {
            videoBlock = true;
            continue;
        }
        if (l.contains("DirectShow audio devices", Qt::CaseInsensitive)) {
            videoBlock = false;
            continue;
        }
        if (!videoBlock) continue;
        const auto m = nameRe.match(l);
        if (m.hasMatch() && !l.contains("Alternative name", Qt::CaseInsensitive)) {
            out << m.captured(1);
        }
    }
    return out;
}

QStringList parseFfmpegAvfoundationVideo(const QString& text) {
    QStringList out;
    bool videoBlock = false;
    const QStringList lines = text.split('\n');
    static const QRegularExpression entryRe(R"(\[\d+\]\s+(.+))");
    for (const QString& raw : lines) {
        const QString l = raw.trimmed();
        if (l.contains("AVFoundation video devices", Qt::CaseInsensitive)) {
            videoBlock = true;
            continue;
        }
        if (l.contains("AVFoundation audio devices", Qt::CaseInsensitive)) {
            videoBlock = false;
            continue;
        }
        if (!videoBlock) continue;
        const auto m = entryRe.match(l);
        if (m.hasMatch()) out << m.captured(1).trimmed();
    }
    return out;
}

QStringList parseFfmpegAvfoundationAudio(const QString& text) {
    QStringList out;
    bool audioBlock = false;
    const QStringList lines = text.split('\n');
    // Matches: "[AVFoundation indev @ 0x...] [0] Built-in Microphone"
    static const QRegularExpression entryRe(
        R"(\[\d+\]\s+(.+))");
    for (const QString& raw : lines) {
        const QString l = raw.trimmed();
        if (l.contains("AVFoundation audio devices", Qt::CaseInsensitive)) {
            audioBlock = true;
            continue;
        }
        if (l.contains("AVFoundation video devices", Qt::CaseInsensitive)) {
            audioBlock = false;
            continue;
        }
        if (!audioBlock) continue;
        const auto m = entryRe.match(l);
        if (m.hasMatch()) out << m.captured(1).trimmed();
    }
    return out;
}
#endif

} // namespace

QList<ScreenInfo> enumerateScreens() {
    QList<ScreenInfo> out;
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        QScreen* s = screens[i];
        const QRect g = s->geometry();
        ScreenInfo info;
        info.index = i;
        info.x = g.x();
        info.y = g.y();
        info.w = g.width();
        info.h = g.height();
        QString name = s->name();
        if (name.isEmpty()) name = QStringLiteral("Display %1").arg(i + 1);
        info.label = QStringLiteral("%1 — %2×%3").arg(name).arg(g.width()).arg(g.height());
        out.push_back(info);
    }
    return out;
}

#if defined(Q_OS_WIN)

QList<AudioDevice> enumerateMicrophones() {
    QList<AudioDevice> out;

    // Primary list comes from ffmpeg's dshow probe when ffmpeg is on
    // PATH — the names returned here are exactly what dshow wants when
    // the engine later starts streaming.
    const QString text = runAndCapture(
        QStringLiteral("ffmpeg"),
        {"-hide_banner", "-list_devices", "true", "-f", "dshow", "-i", "dummy"});
    for (const QString& name : parseFfmpegDshowAudio(text)) {
        // Heuristic: skip the loopback device used for desktop audio so the
        // mic list doesn't double-list it.
        if (name.compare(QStringLiteral("virtual-audio-capturer"),
                         Qt::CaseInsensitive) == 0) continue;
        AudioDevice d;
        d.id = name;
        d.label = name;
        out.push_back(d);
    }

    // Fallback: WASAPI device list via QMediaDevices. Windows' MMDevice
    // friendly names match the dshow names verbatim on every modern
    // driver we've tested, so the resulting id is still usable as a
    // dshow input string. We keep loopback-looking entries here too —
    // VB-Cable Output, Stereo Mix, Voicemeeter Out, etc. are genuine
    // capture endpoints, and OBS lets users pick them as either a mic
    // or a desktop-audio source; we mirror that.
    for (const AudioDevice& q : qtAudioInputs()) {
        if (q.label.compare(QStringLiteral("virtual-audio-capturer"),
                            Qt::CaseInsensitive) == 0) continue;
        if (!containsDeviceId(out, q.id)) out.push_back(q);
    }

    return out;
}

QList<AudioDevice> enumerateDesktopAudio() {
    QList<AudioDevice> out;
    // The OBS-bundled "virtual-audio-capturer" or the screen-capture
    // recorder's loopback device is the canonical Windows option. We list
    // it whether or not the user has it installed; if they don't, ffmpeg
    // will fail at start time with a clear error.
    AudioDevice d;
    d.id = QStringLiteral("virtual-audio-capturer");
    d.label = QStringLiteral("virtual-audio-capturer (Screen Capture Recorder)");
    out.push_back(d);

    // Also surface any other dshow audio devices the user might want to
    // designate as desktop audio (e.g. a Voicemeeter virtual cable).
    const QString text = runAndCapture(
        QStringLiteral("ffmpeg"),
        {"-hide_banner", "-list_devices", "true", "-f", "dshow", "-i", "dummy"});
    for (const QString& name : parseFfmpegDshowAudio(text)) {
        if (name.compare(QStringLiteral("virtual-audio-capturer"),
                         Qt::CaseInsensitive) == 0) continue;
        AudioDevice extra;
        extra.id = name;
        extra.label = name;
        out.push_back(extra);
    }

    // Fallback: WASAPI device list via QMediaDevices. Prefer loopback-
    // looking inputs (Stereo Mix, Voicemeeter, Cable Output) so the user
    // can pick a real desktop-audio source even when ffmpeg isn't on
    // PATH yet.
    QList<AudioDevice> qtLoopbacks;
    QList<AudioDevice> qtOthers;
    for (const AudioDevice& q : qtAudioInputs()) {
        if (containsDeviceId(out, q.id)) continue;
        if (looksLikeLoopback(q.label)) qtLoopbacks.push_back(q);
        else qtOthers.push_back(q);
    }
    for (const AudioDevice& q : qtLoopbacks) out.push_back(q);
    for (const AudioDevice& q : qtOthers) out.push_back(q);

    return out;
}

QList<VideoDevice> enumerateVideoCaptureDevices() {
    QList<VideoDevice> out;
    const QString text = runAndCapture(
        QStringLiteral("ffmpeg"),
        {"-hide_banner", "-list_devices", "true", "-f", "dshow", "-i", "dummy"});
    for (const QString& name : parseFfmpegDshowVideo(text)) {
        VideoDevice d;
        d.id = name;
        d.label = name;
        out.push_back(d);
    }
    return out;
}

#elif defined(Q_OS_MACOS)

QList<AudioDevice> enumerateMicrophones() {
    QList<AudioDevice> out;
    const QString text = runAndCapture(
        QStringLiteral("ffmpeg"),
        {"-hide_banner", "-f", "avfoundation", "-list_devices", "true", "-i", ""});
    int idx = 0;
    for (const QString& name : parseFfmpegAvfoundationAudio(text)) {
        AudioDevice d;
        d.id = QString::number(idx++);
        d.label = name;
        out.push_back(d);
    }
    return out;
}

QList<AudioDevice> enumerateDesktopAudio() {
    // macOS has no first-class loopback API; users typically install
    // BlackHole or Soundflower, which then appear as regular audio
    // devices. List the same set as microphones with a note.
    QList<AudioDevice> out = enumerateMicrophones();
    for (auto& d : out) {
        d.label = QStringLiteral("(loopback) %1").arg(d.label);
    }
    return out;
}

QList<VideoDevice> enumerateVideoCaptureDevices() {
    QList<VideoDevice> out;
    const QString text = runAndCapture(
        QStringLiteral("ffmpeg"),
        {"-hide_banner", "-f", "avfoundation", "-list_devices", "true", "-i", ""});
    int idx = 0;
    for (const QString& name : parseFfmpegAvfoundationVideo(text)) {
        VideoDevice d;
        d.id = QString::number(idx++);
        d.label = name;
        out.push_back(d);
    }
    return out;
}

#else // Linux / X11 + PulseAudio

QList<AudioDevice> enumerateMicrophones() {
    QList<AudioDevice> out;
    const QString text = runAndCapture(QStringLiteral("pactl"),
                                       {"list", "sources", "short"});
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList cols = line.split('\t');
        if (cols.size() < 2) continue;
        const QString name = cols[1];
        // Monitor sources are desktop-audio loopbacks, not real mics.
        if (name.endsWith(QStringLiteral(".monitor"))) continue;
        AudioDevice d;
        d.id = name;
        d.label = name;
        out.push_back(d);
    }
    if (out.isEmpty()) {
        // pactl missing or PulseAudio not running — fall back to the
        // ALSA/Pipewire devices Qt sees directly. Last resort is the
        // implicit "default" so the dialog is still usable.
        for (const AudioDevice& q : qtAudioInputs()) {
            if (!containsDeviceId(out, q.id)) out.push_back(q);
        }
        if (out.isEmpty()) {
            AudioDevice d;
            d.id = QStringLiteral("default");
            d.label = QStringLiteral("default (PulseAudio default source)");
            out.push_back(d);
        }
    }
    return out;
}

QList<AudioDevice> enumerateDesktopAudio() {
    QList<AudioDevice> out;
    const QString text = runAndCapture(QStringLiteral("pactl"),
                                       {"list", "sources", "short"});
    const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList cols = line.split('\t');
        if (cols.size() < 2) continue;
        const QString name = cols[1];
        if (!name.endsWith(QStringLiteral(".monitor"))) continue;
        AudioDevice d;
        d.id = name;
        d.label = name;
        out.push_back(d);
    }
    return out;
}

QList<VideoDevice> enumerateVideoCaptureDevices() {
    QList<VideoDevice> out;
    // Probe /dev/video0..9 — v4l2 numbers them sequentially. We don't
    // shell out to `v4l2-ctl --list-devices` because it isn't installed
    // by default on most distros.
    for (int i = 0; i < 10; ++i) {
        const QString path = QString("/dev/video%1").arg(i);
        if (QFileInfo::exists(path)) {
            VideoDevice d;
            d.id = path;
            d.label = path;
            out.push_back(d);
        }
    }
    return out;
}

#endif

#if defined(Q_OS_WIN)

namespace {

// EnumWindows callback writes (title -> count) into the userdata map.
// We track duplicates so the picker can disambiguate identical titles
// by appending " (2)", " (3)", etc.
BOOL CALLBACK collectWindowProc(HWND h, LPARAM lparam) {
    auto* out = reinterpret_cast<QList<WindowInfo>*>(lparam);
    if (!IsWindowVisible(h)) return TRUE;
    if (GetWindow(h, GW_OWNER) != nullptr) return TRUE;
    if (GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;

    wchar_t buf[512];
    const int len = GetWindowTextW(h, buf, 512);
    if (len <= 0) return TRUE;
    QString title = QString::fromWCharArray(buf, len).trimmed();
    if (title.isEmpty()) return TRUE;

    WindowInfo w;
    w.id = title;
    w.label = title;
    out->push_back(w);
    return TRUE;
}

} // namespace

QList<WindowInfo> enumerateCaptureWindows() {
    QList<WindowInfo> out;
    EnumWindows(&collectWindowProc, reinterpret_cast<LPARAM>(&out));
    // Disambiguate duplicate titles by appending an instance number to
    // the second-onwards occurrence. ffmpeg's gdigrab matches title=...
    // by exact match, so we keep the id stable and only mutate label.
    QHash<QString, int> seen;
    for (WindowInfo& w : out) {
        const int n = ++seen[w.id];
        if (n > 1) w.label = QStringLiteral("%1 (%2)").arg(w.id).arg(n);
    }
    return out;
}

#else

QList<WindowInfo> enumerateCaptureWindows() {
    // gdigrab title= is Windows-only, so the picker stays empty on
    // other platforms and the SourceConfigDialog falls back to its
    // free-form QLineEdit.
    return {};
}

#endif

} // namespace lumen

