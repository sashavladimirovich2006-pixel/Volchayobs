#include "Source.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QObject>

namespace lumen {

QString sourceTypeKey(SourceType t) {
    switch (t) {
        case SourceType::DisplayCapture:     return QStringLiteral("display");
        case SourceType::WindowCapture:      return QStringLiteral("window");
        case SourceType::VideoCaptureDevice: return QStringLiteral("video_device");
        case SourceType::MediaFile:          return QStringLiteral("media_file");
        case SourceType::Image:              return QStringLiteral("image");
        case SourceType::ColorSource:        return QStringLiteral("color");
        case SourceType::TextSource:         return QStringLiteral("text");
        case SourceType::TestPattern:        return QStringLiteral("test_pattern");
        case SourceType::BrowserSource:      return QStringLiteral("browser");
        case SourceType::Microphone:         return QStringLiteral("microphone");
        case SourceType::DesktopAudio:       return QStringLiteral("desktop_audio");
    }
    return QStringLiteral("display");
}

SourceType sourceTypeFromKey(const QString& key, bool* ok) {
    auto setOk = [&](bool v) { if (ok) *ok = v; };
    static const struct { const char* k; SourceType t; } MAP[] = {
        { "display",       SourceType::DisplayCapture },
        { "window",        SourceType::WindowCapture },
        { "video_device",  SourceType::VideoCaptureDevice },
        { "media_file",    SourceType::MediaFile },
        { "image",         SourceType::Image },
        { "color",         SourceType::ColorSource },
        { "text",          SourceType::TextSource },
        { "test_pattern",  SourceType::TestPattern },
        { "browser",       SourceType::BrowserSource },
        { "microphone",    SourceType::Microphone },
        { "desktop_audio", SourceType::DesktopAudio },
    };
    for (const auto& m : MAP) {
        if (key == QLatin1String(m.k)) {
            setOk(true);
            return m.t;
        }
    }
    setOk(false);
    return SourceType::DisplayCapture;
}

QString sourceTypeDisplayName(SourceType t) {
    switch (t) {
        case SourceType::DisplayCapture:     return QObject::tr("Display capture");
        case SourceType::WindowCapture:      return QObject::tr("Window capture");
        case SourceType::VideoCaptureDevice: return QObject::tr("Webcam");
        case SourceType::MediaFile:          return QObject::tr("Media file");
        case SourceType::Image:              return QObject::tr("Image");
        case SourceType::ColorSource:        return QObject::tr("Solid color");
        case SourceType::TextSource:         return QObject::tr("Text");
        case SourceType::TestPattern:        return QObject::tr("Test pattern");
        case SourceType::BrowserSource:      return QObject::tr("Browser source");
        case SourceType::Microphone:         return QObject::tr("Microphone");
        case SourceType::DesktopAudio:       return QObject::tr("Desktop audio");
    }
    return QString();
}

bool sourceTypeIsVideo(SourceType t) {
    switch (t) {
        case SourceType::DisplayCapture:
        case SourceType::WindowCapture:
        case SourceType::VideoCaptureDevice:
        case SourceType::MediaFile:
        case SourceType::Image:
        case SourceType::ColorSource:
        case SourceType::TextSource:
        case SourceType::TestPattern:
        case SourceType::BrowserSource:
            return true;
        case SourceType::Microphone:
        case SourceType::DesktopAudio:
            return false;
    }
    return false;
}

bool sourceTypeIsAudio(SourceType t) {
    switch (t) {
        case SourceType::Microphone:
        case SourceType::DesktopAudio:
        case SourceType::MediaFile:        // media file may carry audio
            return true;
        default:
            return false;
    }
}

QJsonObject Source::toJson() const {
    QJsonObject o;
    o["id"]       = id;
    o["name"]     = name;
    o["type"]     = sourceTypeKey(type);
    o["enabled"]  = enabled;
    QJsonObject s = QJsonObject::fromVariantMap(settings);
    o["settings"] = s;
    QJsonObject tr;
    tr["x"] = transform.x();
    tr["y"] = transform.y();
    tr["w"] = transform.width();
    tr["h"] = transform.height();
    o["transform"] = tr;
    return o;
}

Source Source::fromJson(const QJsonObject& o) {
    Source s;
    s.id      = o.value("id").toString(s.id);
    s.name    = o.value("name").toString();
    s.type    = sourceTypeFromKey(o.value("type").toString());
    s.enabled = o.value("enabled").toBool(true);
    s.settings = o.value("settings").toObject().toVariantMap();
    if (s.name.isEmpty()) s.name = sourceTypeDisplayName(s.type);
    if (o.contains("transform")) {
        const QJsonObject tr = o.value("transform").toObject();
        s.transform = QRectF(
            tr.value("x").toDouble(0.2),
            tr.value("y").toDouble(0.2),
            tr.value("w").toDouble(0.6),
            tr.value("h").toDouble(0.6));
    }
    return s;
}

QString serializeSources(const SourceList& sources) {
    QJsonArray arr;
    for (const auto& s : sources) arr.append(s.toJson());
    return QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

SourceList deserializeSources(const QString& json) {
    SourceList out;
    if (json.isEmpty()) return out;
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) return out;
    for (const auto& v : doc.array()) {
        out.push_back(Source::fromJson(v.toObject()));
    }
    return out;
}

int firstEnabledVideoIndex(const SourceList& sources) {
    for (int i = 0; i < sources.size(); ++i) {
        if (sources[i].enabled && sourceTypeIsVideo(sources[i].type)) return i;
    }
    return -1;
}

} // namespace lumen
