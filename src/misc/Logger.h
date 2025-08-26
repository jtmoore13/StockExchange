#pragma once
#include <chrono>
#include <string>

namespace Logging {
    inline bool logEnabled = true;
    inline bool logVerbose = false;  // enables LOG_VERBOSE
}

#define LOG(msg)         do { if (Logging::logEnabled) this->Log_(msg); } while (false)
#define LOG_ERROR(msg)   do { if (Logging::logEnabled) this->LogError_(msg); } while (false)
#define LOG_WARNING(msg) do { if (Logging::logEnabled) this->LogWarning_(msg); } while (false)
#define LOG_VERBOSE(msg) do { if (Logging::logVerbose) this->Log_(msg); } while (false)


/*
    Any class that wants to support logging should inherit from LoggerBase.
    Derived classes must implement Log_(), which defines how the log message
    is handled. They should typically delegate to Logger::LogMsg(), a static,
    thread-safe function that handles output formatting.

    Use LOG / LOG_ERROR / LOG_WARNING macros in your code to conditionally log.
    Disable logging at runtime by setting Logging::logEnabled = false.
*/


/////////////////////////////////////////////////////////////////////////////////////////////////
// LoggerBase

class LoggerBase
{
protected:
    LoggerBase() = default;
    virtual ~LoggerBase() = default;
    virtual void Log_(const std::string& msg) const = 0;

    void LogError_(const std::string& msg) const;
    void LogWarning_(const std::string& msg) const;
};


/////////////////////////////////////////////////////////////////////////////////////////////////
// Logger


class Logger
{
public:
    static void LogMsg(const std::string& msg, const std::string& prefix,
        std::chrono::time_point<std::chrono::system_clock> logTime = std::chrono::system_clock::now());
};
