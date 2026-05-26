#pragma once

#include <QFile>
#include <QMutex>
#include <QString>
#include <QTextStream>

namespace lumen {

enum class LogLevel { Debug, Info, Warn, Error };

class Logger {
public:
    static Logger& instance();

    void log(LogLevel lvl, const QString& tag, const QString& msg);
    QString logFilePath() const;
    QString logDir() const;
    void install();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();

    void rotateIfNeeded();

    mutable QMutex m_mtx;
    QFile m_file;
    QTextStream m_stream;
    QString m_currentDate;
    QString m_logDir;
};

#define LOG_D(tag, msg) ::lumen::Logger::instance().log(::lumen::LogLevel::Debug, tag, msg)
#define LOG_I(tag, msg) ::lumen::Logger::instance().log(::lumen::LogLevel::Info,  tag, msg)
#define LOG_W(tag, msg) ::lumen::Logger::instance().log(::lumen::LogLevel::Warn,  tag, msg)
#define LOG_E(tag, msg) ::lumen::Logger::instance().log(::lumen::LogLevel::Error, tag, msg)

} // namespace lumen
