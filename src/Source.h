#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <QVariantMap>

namespace lumen {

// Every kind of input the user can add to the live mix. Listed in the
// order they appear in the [+ Add] menu.
enum class SourceType {
    DisplayCapture,        // monitor full-screen capture
    WindowCapture,         // gdigrab title=... (Windows-only effective)
    VideoCaptureDevice,    // webcam / capture card
    MediaFile,             // local video/audio file
    Image,                 // static PNG/JPG
    ColorSource,           // solid fill
    TextSource,            // rendered text caption
    TestPattern,           // ffmpeg testsrc2 + sine
    BrowserSource,         // QtWebEngine page used as a video layer
    Microphone,            // dshow / pulse / avfoundation audio capture
    DesktopAudio,          // virtual-audio-capturer / .monitor / loopback
};

QString sourceTypeKey(SourceType t);
SourceType sourceTypeFromKey(const QString& key, bool* ok = nullptr);
QString    sourceTypeDisplayName(SourceType t);
bool       sourceTypeIsVideo(SourceType t);
bool       sourceTypeIsAudio(SourceType t);

// One scene-graph entry. We deliberately store type-specific knobs in a
// QVariantMap rather than a discriminated union: this keeps JSON
// serialization trivial, lets the per-type configure dialogs evolve
// independently, and avoids a giant struct that touches every header.
struct Source {
    QString     id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString     name;            // user-visible label
    SourceType  type = SourceType::DisplayCapture;
    bool        enabled = true;
    QVariantMap settings;         // type-specific knobs; see SourceFields

    // Placement of the source on the preview canvas, normalised to [0..1]
    // (so window resizes don't break the layout). Default is centered 60%
    // of the canvas — that way the user can immediately see and drag the
    // resize handles instead of the source covering everything.
    QRectF      transform = QRectF(0.2, 0.2, 0.6, 0.6);

    QJsonObject toJson() const;
    static Source fromJson(const QJsonObject& o);
};

// String keys used inside Source::settings. Centralised so per-type code
// (config dialog, ffmpeg builder, preview renderer) all agree.
namespace SourceFields {
    // DisplayCapture
    constexpr auto SCREEN_INDEX = "screenIndex";

    // WindowCapture
    constexpr auto WINDOW_TITLE = "windowTitle";

    // VideoCaptureDevice
    constexpr auto VIDEO_DEVICE_ID    = "videoDeviceId";
    constexpr auto VIDEO_DEVICE_LABEL = "videoDeviceLabel";

    // MediaFile
    constexpr auto FILE_PATH = "filePath";
    constexpr auto LOOP      = "loop";

    // Image
    // (also uses FILE_PATH)

    // ColorSource
    constexpr auto COLOR_RGB    = "colorRgb";
    constexpr auto COLOR_WIDTH  = "colorWidth";
    constexpr auto COLOR_HEIGHT = "colorHeight";

    // TextSource
    constexpr auto TEXT_BODY     = "text";
    constexpr auto TEXT_FONT     = "fontFamily";
    constexpr auto TEXT_SIZE     = "fontSize";
    constexpr auto TEXT_COLOR    = "fontColor";
    constexpr auto TEXT_BG_COLOR = "bgColor";

    // TestPattern (no fields — uses encoder w/h/fps)

    // BrowserSource
    constexpr auto BROWSER_URL          = "browserUrl";
    constexpr auto BROWSER_WIDTH        = "browserWidth";    // CSS pixels
    constexpr auto BROWSER_HEIGHT       = "browserHeight";
    constexpr auto BROWSER_REFRESH_SEC  = "browserRefreshSec"; // 0 = no reload
    constexpr auto BROWSER_CUSTOM_CSS   = "browserCustomCss";

    // Microphone & DesktopAudio
    constexpr auto AUDIO_DEVICE_ID    = "audioDeviceId";
    constexpr auto AUDIO_DEVICE_LABEL = "audioDeviceLabel";
    constexpr auto AUDIO_VOLUME       = "volume";  // 0.0..1.0
}

// Whole-list helpers. Persisted as a JSON array under a single
// QSettings key so the on-disk format stays human-readable and
// schema-free.
using SourceList = QList<Source>;

QString    serializeSources(const SourceList& sources);
SourceList deserializeSources(const QString& json);

// Pick the source whose frame is shown in the live preview / sent over
// RTMP. We use the first enabled video source; returns -1 if none.
int firstEnabledVideoIndex(const SourceList& sources);

} // namespace lumen
