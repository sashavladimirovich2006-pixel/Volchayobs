#include "SourceConfigDialog.h"

#include "Devices.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lumen {

namespace {

QString fileFilterFor(SourceType t) {
    switch (t) {
        case SourceType::Image:
            return QObject::tr("Images (*.png *.jpg *.jpeg *.bmp *.webp);;All files (*)");
        case SourceType::MediaFile:
            return QObject::tr("Media files (*.mp4 *.mov *.mkv *.webm *.mp3 *.wav *.aac *.flac);;All files (*)");
        default:
            return QObject::tr("All files (*)");
    }
}

void styleColorButton(QPushButton* btn, const QColor& c) {
    if (!btn) return;
    btn->setText(c.name(QColor::HexRgb).toUpper());
    QColor textColor = (c.lightnessF() > 0.55) ? Qt::black : Qt::white;
    btn->setStyleSheet(QString("background-color: %1; color: %2; padding: 6px 12px;")
                           .arg(c.name(QColor::HexRgb), textColor.name()));
}

} // namespace

SourceConfigDialog::SourceConfigDialog(const Source& initial, Mode mode, QWidget* parent)
    : QDialog(parent), m_source(initial), m_mode(mode) {
    setWindowTitle(mode == Mode::AddNew ? tr("New source")
                                        : tr("Configure source"));
    setModal(true);
    resize(520, 480);

    auto* root = new QVBoxLayout(this);

    auto* headerForm = new QFormLayout;
    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText(tr("Source name"));
    headerForm->addRow(tr("Name"), m_nameEdit);

    m_typeCombo = new QComboBox;
    const SourceType allTypes[] = {
        SourceType::DisplayCapture, SourceType::WindowCapture,
        SourceType::VideoCaptureDevice, SourceType::MediaFile,
        SourceType::Image, SourceType::ColorSource, SourceType::TextSource,
        SourceType::TestPattern, SourceType::BrowserSource,
        SourceType::Microphone, SourceType::DesktopAudio,
    };
    for (auto t : allTypes) {
        m_typeCombo->addItem(sourceTypeDisplayName(t), int(t));
    }
    m_typeCombo->setCurrentIndex(m_typeCombo->findData(int(m_source.type)));
    if (m_mode == Mode::EditExisting) m_typeCombo->setEnabled(false);
    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this] {
                m_source.type = static_cast<SourceType>(m_typeCombo->currentData().toInt());
                if (m_source.name.isEmpty()) {
                    m_source.name = sourceTypeDisplayName(m_source.type);
                    m_nameEdit->setText(m_source.name);
                }
                rebuildBody();
            });
    headerForm->addRow(tr("Type"), m_typeCombo);
    root->addLayout(headerForm);

    m_bodyHost = new QWidget;
    auto* bodyLay = new QVBoxLayout(m_bodyHost);
    bodyLay->setContentsMargins(0, 8, 0, 8);
    root->addWidget(m_bodyHost, 1);

    m_refreshBtn = new QPushButton(tr("Refresh devices"));
    m_refreshBtn->setProperty("role", "secondary");
    // Don't let this button become the dialog's "default" action after a
    // click — otherwise it keeps the orange :default highlight even
    // after the cursor has left, which reads as a stuck hover state.
    m_refreshBtn->setAutoDefault(false);
    m_refreshBtn->setDefault(false);
    m_refreshBtn->setFocusPolicy(Qt::TabFocus);
    connect(m_refreshBtn, &QPushButton::clicked,
            this, &SourceConfigDialog::onRefreshDevices);
    auto* refreshRow = new QHBoxLayout;
    refreshRow->addStretch(1);
    refreshRow->addWidget(m_refreshBtn);
    root->addLayout(refreshRow);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setProperty("role", "primary");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    if (m_source.name.isEmpty()) m_source.name = sourceTypeDisplayName(m_source.type);
    m_nameEdit->setText(m_source.name);
    rebuildBody();
    readToUi();
}

void SourceConfigDialog::rebuildBody() {
    // Drop the previous controls — Qt will delete the widgets that lived
    // inside the host layout, and we null out our pointers so per-type
    // accessors below can't reach into freed memory.
    if (auto* old = m_bodyHost->layout()) {
        QLayoutItem* it;
        while ((it = old->takeAt(0)) != nullptr) {
            if (auto* w = it->widget()) w->deleteLater();
            delete it;
        }
        delete old;
    }
    m_screenCombo = nullptr;
    m_windowTitleEdit = nullptr;
    m_windowCombo = nullptr;
    m_videoDeviceCombo = nullptr;
    m_audioDeviceCombo = nullptr;
    m_filePathEdit = nullptr;
    m_filePickBtn = nullptr;
    m_loopCheck = nullptr;
    m_colorBtn = nullptr;
    m_colorWidthSpin = nullptr;
    m_colorHeightSpin = nullptr;
    m_textBodyEdit = nullptr;
    m_textSizeSpin = nullptr;
    m_textColorBtn = nullptr;
    m_textBgBtn = nullptr;
    m_volumeSpin = nullptr;

    auto* form = new QFormLayout(m_bodyHost);
    form->setContentsMargins(0, 0, 0, 0);

    m_refreshBtn->setVisible(
        m_source.type == SourceType::DisplayCapture ||
        m_source.type == SourceType::WindowCapture ||
        m_source.type == SourceType::VideoCaptureDevice ||
        m_source.type == SourceType::Microphone ||
        m_source.type == SourceType::DesktopAudio);

    switch (m_source.type) {
        case SourceType::DisplayCapture: {
            m_screenCombo = new QComboBox;
            for (const auto& s : enumerateScreens()) {
                m_screenCombo->addItem(
                    QString("%1 — %2×%3 @ (%4, %5)")
                        .arg(s.label).arg(s.w).arg(s.h).arg(s.x).arg(s.y),
                    s.index);
            }
            if (m_screenCombo->count() == 0)
                m_screenCombo->addItem(tr("(no monitors found)"), 0);
            form->addRow(tr("Monitor"), m_screenCombo);
            break;
        }
        case SourceType::WindowCapture: {
            // On Windows we enumerate visible top-level windows so the
            // user can pick one from a dropdown instead of typing the
            // exact title by hand. On other platforms the picker is
            // empty, so we fall back to the free-form edit.
            const auto windows = enumerateCaptureWindows();
            if (!windows.isEmpty()) {
                m_windowCombo = new QComboBox;
                m_windowCombo->setEditable(true);
                m_windowCombo->setInsertPolicy(QComboBox::NoInsert);
                for (const auto& w : windows)
                    m_windowCombo->addItem(w.label, w.id);
                form->addRow(tr("Window"), m_windowCombo);
            } else {
                m_windowTitleEdit = new QLineEdit;
                m_windowTitleEdit->setPlaceholderText(tr("Exact window title"));
                form->addRow(tr("Window"), m_windowTitleEdit);
            }
            auto* hint = new QLabel(tr(
                "Window capture by title only works on Windows (gdigrab title=...)."));
            hint->setWordWrap(true);
            hint->setProperty("role", "sectionHeader");
            form->addRow(hint);
            break;
        }
        case SourceType::VideoCaptureDevice: {
            m_videoDeviceCombo = new QComboBox;
            for (const auto& d : enumerateVideoCaptureDevices()) {
                m_videoDeviceCombo->addItem(d.label, d.id);
            }
            if (m_videoDeviceCombo->count() == 0)
                m_videoDeviceCombo->addItem(tr("(no devices found)"), QString());
            form->addRow(tr("Camera"), m_videoDeviceCombo);
            break;
        }
        case SourceType::MediaFile:
        case SourceType::Image: {
            m_filePathEdit = new QLineEdit;
            m_filePickBtn = new QPushButton(tr("Choose…"));
            connect(m_filePickBtn, &QPushButton::clicked,
                    this, &SourceConfigDialog::onPickFile);
            auto* row = new QHBoxLayout;
            row->addWidget(m_filePathEdit, 1);
            row->addWidget(m_filePickBtn);
            form->addRow(tr("File"), row);
            if (m_source.type == SourceType::MediaFile) {
                m_loopCheck = new QCheckBox(tr("Loop"));
                form->addRow(QString(), m_loopCheck);
            }
            break;
        }
        case SourceType::ColorSource: {
            m_colorBtn = new QPushButton;
            connect(m_colorBtn, &QPushButton::clicked,
                    this, &SourceConfigDialog::onPickColor);
            m_colorWidthSpin = new QSpinBox;
            m_colorWidthSpin->setRange(64, 7680);
            m_colorWidthSpin->setSuffix(QStringLiteral(" px"));
            m_colorHeightSpin = new QSpinBox;
            m_colorHeightSpin->setRange(64, 4320);
            m_colorHeightSpin->setSuffix(QStringLiteral(" px"));
            form->addRow(tr("Color"), m_colorBtn);
            form->addRow(tr("Width"), m_colorWidthSpin);
            form->addRow(tr("Height"), m_colorHeightSpin);
            break;
        }
        case SourceType::TextSource: {
            m_textBodyEdit = new QPlainTextEdit;
            m_textBodyEdit->setPlaceholderText(tr("Text to display"));
            m_textSizeSpin = new QSpinBox;
            m_textSizeSpin->setRange(8, 256);
            m_textSizeSpin->setSuffix(QStringLiteral(" pt"));
            m_textColorBtn = new QPushButton;
            connect(m_textColorBtn, &QPushButton::clicked,
                    this, &SourceConfigDialog::onPickTextColor);
            m_textBgBtn = new QPushButton;
            connect(m_textBgBtn, &QPushButton::clicked,
                    this, &SourceConfigDialog::onPickTextBgColor);
            form->addRow(tr("Text"), m_textBodyEdit);
            form->addRow(tr("Size"), m_textSizeSpin);
            form->addRow(tr("Text color"), m_textColorBtn);
            form->addRow(tr("Background color"), m_textBgBtn);
            break;
        }
        case SourceType::TestPattern: {
            auto* hint = new QLabel(tr(
                "Generated by ffmpeg testsrc2 at the encoder's current resolution and fps. "
                "If there are no other audio sources, a 440 Hz sine tone will be added."));
            hint->setWordWrap(true);
            form->addRow(hint);
            break;
        }
        case SourceType::BrowserSource: {
            m_browserUrlEdit = new QLineEdit;
            m_browserUrlEdit->setPlaceholderText(
                QStringLiteral("https://streamelements.com/overlay/…"));
            form->addRow(tr("URL"), m_browserUrlEdit);

            m_browserWidthSpin = new QSpinBox;
            m_browserWidthSpin->setRange(64, 7680);
            m_browserWidthSpin->setSuffix(QStringLiteral(" px"));
            m_browserHeightSpin = new QSpinBox;
            m_browserHeightSpin->setRange(64, 4320);
            m_browserHeightSpin->setSuffix(QStringLiteral(" px"));
            form->addRow(tr("Width"), m_browserWidthSpin);
            form->addRow(tr("Height"), m_browserHeightSpin);

            m_browserRefreshSpin = new QSpinBox;
            m_browserRefreshSpin->setRange(0, 86400);
            m_browserRefreshSpin->setSuffix(QStringLiteral(" s"));
            m_browserRefreshSpin->setSpecialValueText(tr("Never"));
            form->addRow(tr("Auto-refresh"), m_browserRefreshSpin);

            m_browserCssEdit = new QPlainTextEdit;
            m_browserCssEdit->setPlaceholderText(
                QStringLiteral("/* optional CSS injected after page load */\n"
                               "body { background: transparent !important; }"));
            m_browserCssEdit->setMaximumHeight(80);
            form->addRow(tr("Custom CSS"), m_browserCssEdit);

            auto* hint = new QLabel(tr(
                "Renders a web page via QtWebEngine as a video layer. The page "
                "background is transparent by default so widgets (donation "
                "alerts, chat overlays, browser-source widgets) composite "
                "cleanly over the scene."));
            hint->setWordWrap(true);
            hint->setProperty("role", "sectionHeader");
            form->addRow(hint);
            break;
        }
        case SourceType::Microphone:
        case SourceType::DesktopAudio: {
            m_audioDeviceCombo = new QComboBox;
            const auto list = (m_source.type == SourceType::Microphone)
                                  ? enumerateMicrophones()
                                  : enumerateDesktopAudio();
            for (const auto& d : list) m_audioDeviceCombo->addItem(d.label, d.id);
            if (m_audioDeviceCombo->count() == 0)
                m_audioDeviceCombo->addItem(tr("(no devices found)"), QString());
            m_volumeSpin = new QSpinBox;
            m_volumeSpin->setRange(0, 200);
            m_volumeSpin->setSuffix(QStringLiteral(" %"));
            form->addRow(tr("Device"), m_audioDeviceCombo);
            form->addRow(tr("Volume"), m_volumeSpin);
            break;
        }
    }
}

void SourceConfigDialog::readToUi() {
    if (m_screenCombo) {
        const int idx = m_source.settings.value(SourceFields::SCREEN_INDEX, 0).toInt();
        int row = m_screenCombo->findData(idx);
        if (row >= 0) m_screenCombo->setCurrentIndex(row);
    }
    if (m_windowTitleEdit) {
        m_windowTitleEdit->setText(
            m_source.settings.value(SourceFields::WINDOW_TITLE).toString());
    }
    if (m_windowCombo) {
        const QString title = m_source.settings.value(
            SourceFields::WINDOW_TITLE).toString();
        const int row = m_windowCombo->findData(title);
        if (row >= 0) m_windowCombo->setCurrentIndex(row);
        else if (!title.isEmpty()) m_windowCombo->setEditText(title);
    }
    if (m_videoDeviceCombo) {
        const QString id = m_source.settings.value(SourceFields::VIDEO_DEVICE_ID).toString();
        int row = m_videoDeviceCombo->findData(id);
        if (row >= 0) m_videoDeviceCombo->setCurrentIndex(row);
        else if (!id.isEmpty())
            m_videoDeviceCombo->addItem(id + tr(" (not found)"), id);
    }
    if (m_audioDeviceCombo) {
        const QString id = m_source.settings.value(SourceFields::AUDIO_DEVICE_ID).toString();
        int row = m_audioDeviceCombo->findData(id);
        if (row >= 0) m_audioDeviceCombo->setCurrentIndex(row);
        else if (!id.isEmpty())
            m_audioDeviceCombo->addItem(id + tr(" (not found)"), id);
    }
    if (m_filePathEdit) {
        m_filePathEdit->setText(m_source.settings.value(SourceFields::FILE_PATH).toString());
    }
    if (m_loopCheck) {
        m_loopCheck->setChecked(m_source.settings.value(SourceFields::LOOP, true).toBool());
    }
    if (m_colorBtn) {
        QColor c(m_source.settings.value(SourceFields::COLOR_RGB,
                    QStringLiteral("#202020")).toString());
        if (!c.isValid()) c = QColor("#202020");
        styleColorButton(m_colorBtn, c);
    }
    if (m_colorWidthSpin) {
        m_colorWidthSpin->setValue(
            m_source.settings.value(SourceFields::COLOR_WIDTH, 1920).toInt());
    }
    if (m_colorHeightSpin) {
        m_colorHeightSpin->setValue(
            m_source.settings.value(SourceFields::COLOR_HEIGHT, 1080).toInt());
    }
    if (m_textBodyEdit) {
        m_textBodyEdit->setPlainText(
            m_source.settings.value(SourceFields::TEXT_BODY).toString());
    }
    if (m_textSizeSpin) {
        m_textSizeSpin->setValue(
            m_source.settings.value(SourceFields::TEXT_SIZE, 48).toInt());
    }
    if (m_textColorBtn) {
        QColor c(m_source.settings.value(SourceFields::TEXT_COLOR,
                    QStringLiteral("#ffffff")).toString());
        if (!c.isValid()) c = QColor(Qt::white);
        styleColorButton(m_textColorBtn, c);
    }
    if (m_textBgBtn) {
        QColor c(m_source.settings.value(SourceFields::TEXT_BG_COLOR,
                    QStringLiteral("#000000")).toString());
        if (!c.isValid()) c = QColor(Qt::black);
        styleColorButton(m_textBgBtn, c);
    }
    if (m_volumeSpin) {
        const double v = m_source.settings.value(SourceFields::AUDIO_VOLUME, 1.0).toDouble();
        m_volumeSpin->setValue(qRound(v * 100.0));
    }
    if (m_browserUrlEdit) {
        m_browserUrlEdit->setText(
            m_source.settings.value(SourceFields::BROWSER_URL).toString());
    }
    if (m_browserWidthSpin) {
        m_browserWidthSpin->setValue(
            m_source.settings.value(SourceFields::BROWSER_WIDTH, 1280).toInt());
    }
    if (m_browserHeightSpin) {
        m_browserHeightSpin->setValue(
            m_source.settings.value(SourceFields::BROWSER_HEIGHT, 720).toInt());
    }
    if (m_browserRefreshSpin) {
        m_browserRefreshSpin->setValue(
            m_source.settings.value(SourceFields::BROWSER_REFRESH_SEC, 0).toInt());
    }
    if (m_browserCssEdit) {
        m_browserCssEdit->setPlainText(
            m_source.settings.value(SourceFields::BROWSER_CUSTOM_CSS).toString());
    }
}

void SourceConfigDialog::writeBackFromUi() {
    m_source.name = m_nameEdit->text().trimmed();
    if (m_source.name.isEmpty())
        m_source.name = sourceTypeDisplayName(m_source.type);

    if (m_screenCombo) {
        m_source.settings[SourceFields::SCREEN_INDEX] = m_screenCombo->currentData().toInt();
    }
    if (m_windowTitleEdit) {
        m_source.settings[SourceFields::WINDOW_TITLE] = m_windowTitleEdit->text().trimmed();
    }
    if (m_windowCombo) {
        // The user can either pick from the list (combobox data carries
        // the canonical title) or type in their own; either way the
        // currentText is what gdigrab matches against.
        const QString id = m_windowCombo->currentData().toString();
        const QString typed = m_windowCombo->currentText().trimmed();
        m_source.settings[SourceFields::WINDOW_TITLE] = id.isEmpty() ? typed : id;
    }
    if (m_videoDeviceCombo) {
        m_source.settings[SourceFields::VIDEO_DEVICE_ID] = m_videoDeviceCombo->currentData().toString();
        m_source.settings[SourceFields::VIDEO_DEVICE_LABEL] = m_videoDeviceCombo->currentText();
    }
    if (m_audioDeviceCombo) {
        m_source.settings[SourceFields::AUDIO_DEVICE_ID] = m_audioDeviceCombo->currentData().toString();
        m_source.settings[SourceFields::AUDIO_DEVICE_LABEL] = m_audioDeviceCombo->currentText();
    }
    if (m_filePathEdit) {
        m_source.settings[SourceFields::FILE_PATH] = m_filePathEdit->text();
    }
    if (m_loopCheck) {
        m_source.settings[SourceFields::LOOP] = m_loopCheck->isChecked();
    }
    if (m_colorWidthSpin) {
        m_source.settings[SourceFields::COLOR_WIDTH] = m_colorWidthSpin->value();
    }
    if (m_colorHeightSpin) {
        m_source.settings[SourceFields::COLOR_HEIGHT] = m_colorHeightSpin->value();
    }
    if (m_textBodyEdit) {
        m_source.settings[SourceFields::TEXT_BODY] = m_textBodyEdit->toPlainText();
    }
    if (m_textSizeSpin) {
        m_source.settings[SourceFields::TEXT_SIZE] = m_textSizeSpin->value();
    }
    if (m_volumeSpin) {
        m_source.settings[SourceFields::AUDIO_VOLUME] = m_volumeSpin->value() / 100.0;
    }
    if (m_browserUrlEdit) {
        m_source.settings[SourceFields::BROWSER_URL] = m_browserUrlEdit->text().trimmed();
    }
    if (m_browserWidthSpin) {
        m_source.settings[SourceFields::BROWSER_WIDTH] = m_browserWidthSpin->value();
    }
    if (m_browserHeightSpin) {
        m_source.settings[SourceFields::BROWSER_HEIGHT] = m_browserHeightSpin->value();
    }
    if (m_browserRefreshSpin) {
        m_source.settings[SourceFields::BROWSER_REFRESH_SEC] = m_browserRefreshSpin->value();
    }
    if (m_browserCssEdit) {
        m_source.settings[SourceFields::BROWSER_CUSTOM_CSS] = m_browserCssEdit->toPlainText();
    }
}

void SourceConfigDialog::onPickFile() {
    const QString filter = fileFilterFor(m_source.type);
    const QString chosen = QFileDialog::getOpenFileName(this, tr("Choose file"),
                                                        m_filePathEdit->text(), filter);
    if (chosen.isEmpty()) return;
    m_filePathEdit->setText(chosen);
}

void SourceConfigDialog::onPickColor() {
    QColor current(m_source.settings.value(SourceFields::COLOR_RGB,
                                            QStringLiteral("#202020")).toString());
    QColor c = QColorDialog::getColor(current, this, tr("Source color"));
    if (!c.isValid()) return;
    m_source.settings[SourceFields::COLOR_RGB] = c.name(QColor::HexRgb);
    styleColorButton(m_colorBtn, c);
}

void SourceConfigDialog::onPickTextColor() {
    QColor current(m_source.settings.value(SourceFields::TEXT_COLOR,
                                            QStringLiteral("#ffffff")).toString());
    QColor c = QColorDialog::getColor(current, this, tr("Text color"));
    if (!c.isValid()) return;
    m_source.settings[SourceFields::TEXT_COLOR] = c.name(QColor::HexRgb);
    styleColorButton(m_textColorBtn, c);
}

void SourceConfigDialog::onPickTextBgColor() {
    QColor current(m_source.settings.value(SourceFields::TEXT_BG_COLOR,
                                            QStringLiteral("#000000")).toString());
    QColor c = QColorDialog::getColor(current, this, tr("Background color"));
    if (!c.isValid()) return;
    m_source.settings[SourceFields::TEXT_BG_COLOR] = c.name(QColor::HexRgb);
    styleColorButton(m_textBgBtn, c);
}

void SourceConfigDialog::onRefreshDevices() {
    writeBackFromUi();
    rebuildBody();
    readToUi();
}

void SourceConfigDialog::accept() {
    writeBackFromUi();
    QDialog::accept();
}

} // namespace lumen
