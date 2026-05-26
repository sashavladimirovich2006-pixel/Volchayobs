#include "Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QtGlobal>

namespace lumen {

namespace {

const char* levelTag(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

LogLevel fromQtMsgType(QtMsgType t) {
    switch (t) {
        case QtDebugMsg:    return LogLevel::Debug;
        case QtInfoMsg:     return LogLevel::Info;
        case QtWarningMsg:  return LogLevel::Warn;
        case QtCriticalMsg:
        case QtFatalMsg:    return LogLevel::Error;
    }
    return LogLevel::Info;
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    QString tag = QStringLiteral("Qt");
    if (ctx.category && *ctx.category) tag = QString::fromLatin1(ctx.category);
    Logger::instance().log(fromQtMsgType(type), tag, msg);
}

} // namespace

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::Logger() {
    m_logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + QStringLiteral("/logs");
    QDir().mkpath(m_logDir);
    rotateIfNeeded();
}

Logger::~Logger() {
    QMutexLocker lock(&m_mtx);
    if (m_file.isOpen()) {
        m_stream.flush();
        m_file.close();
    }
}

void Logger::rotateIfNeeded() {
    // Called under lock or from ctor. Opens a new daily file when date changes.
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    if (today == m_currentDate && m_file.isOpen()) return;

    if (m_file.isOpen()) {
        m_stream.flush();
        m_file.close();
    }

    m_currentDate = today;
    const QString path = m_logDir + QStringLiteral("/volchay-") + today + QStringLiteral(".log");
    m_file.setFileName(path);
    if (m_file.open(QIODevice::Append | QIODevice::Text)) {
        m_stream.setDevice(&m_file);
        m_stream << QStringLiteral("\n=== Session start %1 (pid %2) ===\n")
                        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                        .arg(QCoreApplication::applicationPid());
        m_stream.flush();
    }
}

void Logger::log(LogLevel lvl, const QString& tag, const QString& msg) {
    QMutexLocker lock(&m_mtx);
    rotateIfNeeded();
    if (!m_file.isOpen()) return;

    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    m_stream << QStringLiteral("[%1] [%2] [%3] %4\n")
                    .arg(ts, QString::fromLatin1(levelTag(lvl)), tag, msg);
    m_stream.flush();
}

QString Logger::logFilePath() const {
    QMutexLocker lock(&m_mtx);
    return m_file.fileName();
}

QString Logger::logDir() const {
    QMutexLocker lock(&m_mtx);
    return m_logDir;
}

void Logger::install() {
    qInstallMessageHandler(qtMessageHandler);
    log(LogLevel::Info, QStringLiteral("Logger"),
        QStringLiteral("File logger installed, writing to: %1").arg(logFilePath()));
}

} // namespace lumen
