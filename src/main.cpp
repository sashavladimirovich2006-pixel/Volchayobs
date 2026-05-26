#include "I18n.h"
#include "Logger.h"
#include "MainWindow.h"
#include "ThemeManager.h"

#include <QApplication>
#include <QLocale>
#include <QStringLiteral>

int main(int argc, char* argv[]) {
    QApplication::setOrganizationName(QStringLiteral("VolchayObs"));
    QApplication::setOrganizationDomain(QStringLiteral("volchay-obs.local"));
    QApplication::setApplicationName(QStringLiteral("Volchay Obs"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // QtWebEngine (Browser source) renders via a separate GPU process
    // and needs the OpenGL context to be shareable with the host app
    // before QApplication is constructed.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);

    // Install file logger right after QApplication so QStandardPaths
    // resolves to %APPDATA%/VolchayObs/logs/ correctly on every OS.
    // Also redirects qDebug/qWarning/qCritical into the same file.
    lumen::Logger::instance().install();
    LOG_I("startup", QStringLiteral("QApplication constructed, version %1")
              .arg(QCoreApplication::applicationVersion()));

    lumen::installTranslator(&app);

    lumen::ThemeManager theme(&app);
    lumen::MainWindow w(&theme);
    w.show();

    LOG_I("startup", QStringLiteral("MainWindow shown, entering event loop"));
    const int rc = app.exec();
    LOG_I("shutdown", QStringLiteral("Event loop exited with code %1").arg(rc));
    return rc;
}
