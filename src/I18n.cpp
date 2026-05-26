#include "I18n.h"

#include <QApplication>
#include <QCoreApplication>
#include <QSettings>

namespace lumen {

namespace {

constexpr auto K_LANGUAGE = "ui/language";

// Singleton lives for the lifetime of QApplication so callers can grab
// it via installTranslator() once and keep using applyLanguage() to
// flip the language at runtime.
HashTranslator* g_translator = nullptr;

} // namespace

QString languageDisplayName(Language l) {
    switch (l) {
        case Language::English:   return QStringLiteral("English");
        case Language::Ukrainian: return QStringLiteral("Українська");
    }
    return QStringLiteral("English");
}

QString languageCode(Language l) {
    switch (l) {
        case Language::English:   return QStringLiteral("en");
        case Language::Ukrainian: return QStringLiteral("uk");
    }
    return QStringLiteral("en");
}

Language languageFromCode(const QString& code) {
    if (code.compare(QStringLiteral("uk"), Qt::CaseInsensitive) == 0)
        return Language::Ukrainian;
    return Language::English;
}

HashTranslator::HashTranslator(QObject* parent)
    : QTranslator(parent) {}

void HashTranslator::setLanguage(Language l) {
    if (m_lang == l && !m_dict.isEmpty() == (l == Language::Ukrainian)) {
        return;
    }
    m_lang = l;
    rebuildDict();
}

QString HashTranslator::translate(const char* /*context*/,
                                  const char* sourceText,
                                  const char* /*disambiguation*/,
                                  int /*n*/) const {
    if (m_lang == Language::English || !sourceText) return QString();
    auto it = m_dict.constFind(QByteArray(sourceText));
    if (it == m_dict.constEnd()) return QString();
    return it.value();
}

void HashTranslator::rebuildDict() {
    m_dict.clear();
    if (m_lang != Language::Ukrainian) return;

    // English source -> Ukrainian translation. Keep entries grouped by
    // the file that declares the source string for easier maintenance.
    auto add = [&](const char* en, const QString& uk) {
        m_dict.insert(QByteArray(en), uk);
    };

    // --- MainWindow / shell ---
    add("Stream",                     QStringLiteral("Стрім"));
    add("Settings",                   QStringLiteral("Налаштування"));
    add("About",                      QStringLiteral("Про програму"));
    add("CURRENT PRESET",             QStringLiteral("ПОТОЧНИЙ ПРЕСЕТ"));
    add("BITRATE",                    QStringLiteral("БІТРЕЙТ"));
    add("OFFLINE",                    QStringLiteral("OFFLINE"));
    add("LIVE",                       QStringLiteral("В ЕФІРІ"));
    add("Go live",                    QStringLiteral("Почати стрім"));
    add("Stop",                       QStringLiteral("Зупинити"));
    add("ENCODER LOG",                QStringLiteral("ЛОГ КОДУВАЛЬНИКА"));
    add("Custom",                     QStringLiteral("Власні"));
    add("Modern Twitch streaming client — C++/Qt6",
        QStringLiteral("Сучасний клієнт стрімінгу на Twitch — C++/Qt6"));
    add("<p>Minimalist streamer with presets following the official Twitch guidelines.<br>"
        "Themes: Light, Blackout (OLED), RGB (animated accent).</p>"
        "<p>The stream is sent to Twitch RTMP through a local <b>ffmpeg</b> — "
        "install it and make sure it is on PATH.</p>"
        "<p>Twitch Broadcasting Guidelines: "
        "<a href=\"https://help.twitch.tv/s/article/broadcasting-guidelines\">help.twitch.tv</a></p>",
        QStringLiteral(
            "<p>Мінімалістичний стрімер з пресетами за офіційною документацією Twitch.<br>"
            "Теми: Світла, Blackout (OLED), RGB (анімований акцент).</p>"
            "<p>Потік йде на Twitch RTMP через локальний <b>ffmpeg</b> — "
            "встанови його та переконайся, що він у PATH.</p>"
            "<p>Гайдлайни Twitch: "
            "<a href=\"https://help.twitch.tv/s/article/broadcasting-guidelines\">help.twitch.tv</a></p>"));

    // --- SourcesPanel ---
    add("SOURCES",                    QStringLiteral("ДЖЕРЕЛА"));
    add("+ Add",                      QStringLiteral("+ Додати"));
    add("Configure",                  QStringLiteral("Налаштувати"));
    add("Remove",                     QStringLiteral("Видалити"));

    // --- AudioMixerPanel ---
    add("AUDIO MIXER",                QStringLiteral("АУДІО-МІКШЕР"));
    add("+ Microphone",               QStringLiteral("+ Мікрофон"));
    add("+ Desktop audio",            QStringLiteral("+ Звук робочого столу"));
    add("MUTE",                       QStringLiteral("ВИМКНУТИ"));
    add("MUTED",                      QStringLiteral("ВИМКНЕНО"));
    add("No device selected",         QStringLiteral("Пристрій не вибрано"));
    add("Click Configure to pick an input device",
        QStringLiteral("Натисни «Налаштувати», щоб обрати пристрій"));
    add("Click Configure to pick a desktop-audio source",
        QStringLiteral("Натисни «Налаштувати», щоб обрати джерело звуку"));
    add("No audio sources yet — add a microphone or desktop audio above to start mixing.",
        QStringLiteral("Аудіоджерел поки немає — додай мікрофон або звук робочого столу зверху."));

    // --- SourceConfigDialog ---
    add("New source",                 QStringLiteral("Нове джерело"));
    add("Configure source",           QStringLiteral("Налаштування джерела"));
    add("Source name",                QStringLiteral("Назва джерела"));
    add("Name",                       QStringLiteral("Назва"));
    add("Type",                       QStringLiteral("Тип"));
    add("Refresh devices",            QStringLiteral("Оновити пристрої"));
    add("Monitor",                    QStringLiteral("Монітор"));
    add("(no monitors found)",        QStringLiteral("(моніторів не знайдено)"));
    add("Window",                     QStringLiteral("Вікно"));
    add("Exact window title",         QStringLiteral("Точний заголовок вікна"));
    add("Window capture by title only works on Windows (gdigrab title=...).",
        QStringLiteral("Захоплення вікна за заголовком працює лише на Windows (gdigrab title=...)."));
    add("Camera",                     QStringLiteral("Камера"));
    add("(no devices found)",         QStringLiteral("(пристроїв не знайдено)"));
    add("Choose…",                    QStringLiteral("Вибрати…"));
    add("File",                       QStringLiteral("Файл"));
    add("Loop",                       QStringLiteral("Зациклити"));
    add("Color",                      QStringLiteral("Колір"));
    add("Width",                      QStringLiteral("Ширина"));
    add("Height",                     QStringLiteral("Висота"));
    add("Text to display",            QStringLiteral("Текст для відображення"));
    add("Text",                       QStringLiteral("Текст"));
    add("Size",                       QStringLiteral("Розмір"));
    add("Text color",                 QStringLiteral("Колір тексту"));
    add("Background color",           QStringLiteral("Колір фону"));
    add("Generated by ffmpeg testsrc2 at the encoder's current resolution and fps. "
        "If there are no other audio sources, a 440 Hz sine tone will be added.",
        QStringLiteral(
            "Генерується ffmpeg testsrc2 у поточній роздільній здатності/fps кодувальника. "
            "Якщо немає інших аудіоджерел — буде додано sine-тон 440 Hz."));
    add("Device",                     QStringLiteral("Пристрій"));
    add("Volume",                     QStringLiteral("Гучність"));
    add(" (not found)",               QStringLiteral(" (не знайдено)"));
    add("Choose file",                QStringLiteral("Вибрати файл"));
    add("Source color",               QStringLiteral("Колір джерела"));
    add("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All files (*)",
        QStringLiteral("Зображення (*.png *.jpg *.jpeg *.bmp *.webp);;Усі файли (*)"));
    add("Media files (*.mp4 *.mov *.mkv *.webm *.mp3 *.wav *.aac *.flac);;All files (*)",
        QStringLiteral("Медіа-файли (*.mp4 *.mov *.mkv *.webm *.mp3 *.wav *.aac *.flac);;Усі файли (*)"));
    add("All files (*)",              QStringLiteral("Усі файли (*)"));

    // --- SettingsDialog ---
    add("Settings — Volchay Obs",     QStringLiteral("Налаштування — Volchay Obs"));
    add("Stream",                     QStringLiteral("Стрім"));
    add("Encoder",                    QStringLiteral("Кодувальник"));
    add("Appearance",                 QStringLiteral("Зовнішній вигляд"));
    add("Custom (manual settings)",   QStringLiteral("Власні (ручні налаштування)"));
    add("Apply preset",               QStringLiteral("Застосувати пресет"));
    add("Stream key from dashboard.twitch.tv → Settings → Stream",
        QStringLiteral("Ключ із dashboard.twitch.tv → Settings → Stream"));
    add("Remember stream key (stored in QSettings)",
        QStringLiteral("Запам'ятати ключ (зберігається в QSettings)"));
    add("Preset",                     QStringLiteral("Пресет"));
    add("RTMP server",                QStringLiteral("RTMP-сервер"));
    add("Stream Key",                 QStringLiteral("Stream Key"));
    add("Frames per second",          QStringLiteral("Кадрів на секунду"));
    add("Video bitrate",              QStringLiteral("Відео-бітрейт"));
    add("Audio bitrate",              QStringLiteral("Аудіо-бітрейт"));
    add("Keyframe interval",          QStringLiteral("Keyframe interval"));
    add("Rate control",               QStringLiteral("Rate control"));
    add("x264 preset",                QStringLiteral("Пресет x264"));
    add("CBR (Twitch recommended)",   QStringLiteral("CBR (рекомендується Twitch)"));
    add("Theme",                      QStringLiteral("Тема"));
    add("Accent",                     QStringLiteral("Акцент"));
    add("Pick accent…",               QStringLiteral("Вибрати акцент…"));
    add("Pick accent color",          QStringLiteral("Вибрати акцентний колір"));
    add("Accent: %1",                 QStringLiteral("Акцент: %1"));
    add("The RGB theme animates the accent automatically and ignores the picked color.",
        QStringLiteral("RGB-тема анімує акцент автоматично та ігнорує вибраний колір."));
    add("Language",                   QStringLiteral("Мова"));
    add("Language change applies after restart.",
        QStringLiteral("Зміна мови застосується після перезапуску."));

    // --- Source types ---
    add("Display capture",            QStringLiteral("Захоплення екрана"));
    add("Window capture",             QStringLiteral("Захоплення вікна"));
    add("Webcam",                     QStringLiteral("Веб-камера"));
    add("Media file",                 QStringLiteral("Медіа-файл"));
    add("Image",                      QStringLiteral("Зображення"));
    add("Solid color",                QStringLiteral("Суцільний колір"));
    add("Test pattern",               QStringLiteral("Тестова картинка"));
    add("Browser source",             QStringLiteral("Веб-сторінка"));
    add("Microphone",                 QStringLiteral("Мікрофон"));
    add("Desktop audio",              QStringLiteral("Звук робочого стола"));

    // --- Themes ---
    add("Light",                      QStringLiteral("Світла"));
    add("Blackout",                   QStringLiteral("Blackout"));
    add("RGB",                        QStringLiteral("RGB"));

    // --- PresetManager ---
    add("Twitch 720p30",              QStringLiteral("Twitch 720p30"));
    add("Twitch 720p60",              QStringLiteral("Twitch 720p60"));
    add("Twitch 1080p30",             QStringLiteral("Twitch 1080p30"));
    add("Twitch 1080p60",             QStringLiteral("Twitch 1080p60"));
    add("1280×720, 30 fps, 3000 kbps — lightweight stream",
        QStringLiteral("1280×720, 30 fps, 3000 kbps — полегшений потік"));
    add("1280×720, 60 fps, 4500 kbps — official 720p60",
        QStringLiteral("1280×720, 60 fps, 4500 kbps — офіційний 720p60"));
    add("1920×1080, 30 fps, 4500 kbps — economical 1080p",
        QStringLiteral("1920×1080, 30 fps, 4500 kbps — економний 1080p"));
    add("1920×1080, 60 fps, 6000 kbps — Twitch recommended",
        QStringLiteral("1920×1080, 60 fps, 6000 kbps — рекомендований Twitch"));

    // --- StreamEngine errors ---
    add("«%1»: window title is not set.",
        QStringLiteral("«%1»: не вказано заголовок вікна."));
    add("«%1»: window capture by title is only available on Windows.",
        QStringLiteral("«%1»: захоплення вікна за заголовком доступне лише на Windows."));
    add("«%1»: device not selected.",
        QStringLiteral("«%1»: не обрано пристрій."));
    add("«%1»: file not found (%2).",
        QStringLiteral("«%1»: файл не знайдено (%2)."));
    add("«%1»: image not found (%2).",
        QStringLiteral("«%1»: зображення не знайдено (%2)."));
    add("No enabled video source. Add at least one on the main screen.",
        QStringLiteral("Немає увімкненого відеоджерела. Додай хоча б одне на головному екрані."));
    add("Stream is already running.",
        QStringLiteral("Потік уже запущено."));
    add("Stream Key is empty. Open Settings and paste the key from dashboard.twitch.tv.",
        QStringLiteral("Stream Key порожній. Відкрий налаштування та встав ключ із dashboard.twitch.tv."));
    add("Video source is not configured.",
        QStringLiteral("Відеоджерело не налаштоване."));
    add("Failed to launch ffmpeg. Make sure ffmpeg is installed and on PATH.",
        QStringLiteral("Не вдалося запустити ffmpeg. Переконайся, що ffmpeg встановлено й він у PATH."));
    add("ffmpeg not found. Install ffmpeg and add it to PATH.",
        QStringLiteral("ffmpeg не знайдено. Встанови ffmpeg і додай у PATH."));
    add("ffmpeg crashed.",
        QStringLiteral("ffmpeg аварійно завершився."));
    add("Error writing to ffmpeg stdin.",
        QStringLiteral("Помилка запису в stdin ffmpeg."));
    add("Error reading ffmpeg stderr.",
        QStringLiteral("Помилка читання stderr ffmpeg."));
    add("ffmpeg process error.",
        QStringLiteral("Помилка процесу ffmpeg."));

    // --- PreviewWidget captions ---
    add("ON AIR",                     QStringLiteral("В ЕФІРІ"));
    add("(empty text)",               QStringLiteral("(порожній текст)"));
    add("Selected: %1  ·  drag corners to resize, body to move",
        QStringLiteral("Обрано: %1  ·  тягни кути для масштабу, тіло — щоб рухати"));
    add("Click a source on the canvas or in the sources list to edit",
        QStringLiteral("Клацни джерело на сцені або в списку, щоб редагувати"));
    add("Add a source from the «Sources» panel",
        QStringLiteral("Додай джерело на панелі «Джерела»"));
    add("Monitor not found",
        QStringLiteral("Монітор не знайдено"));
    add("Could not capture frame (X11/Wayland needs xcb).",
        QStringLiteral("Не вдалося отримати кадр (для X11/Wayland потрібен xcb)."));
    add("Window preview only available on Windows during streaming.",
        QStringLiteral("Превʼю вікна доступне лише на Windows під час стріму."));
    add("Camera preview will appear during streaming.",
        QStringLiteral("Превʼю камери з'явиться під час стріму."));
    add("No file selected.",
        QStringLiteral("Файл не обрано."));
    add("Media file preview will appear during streaming:\n%1",
        QStringLiteral("Превʼю медіа-файлу з'явиться під час стріму:\n%1"));
    add("No image file selected.",
        QStringLiteral("Файл зображення не обрано."));
    add("Failed to load image:\n%1",
        QStringLiteral("Не вдалося завантажити зображення:\n%1"));
    add("Audio source — no video preview.",
        QStringLiteral("Аудіоджерело — без відео-превʼю."));
}

HashTranslator& installTranslator(QApplication* app) {
    if (!g_translator) {
        g_translator = new HashTranslator(app);
    }
    QSettings q;
    const QString code = q.value(K_LANGUAGE, QStringLiteral("en")).toString();
    g_translator->setLanguage(languageFromCode(code));
    if (app) {
        QCoreApplication::installTranslator(g_translator);
    }
    return *g_translator;
}

void applyLanguage(Language l) {
    QSettings q;
    q.setValue(K_LANGUAGE, languageCode(l));
    if (g_translator) {
        g_translator->setLanguage(l);
    }
}

} // namespace lumen
