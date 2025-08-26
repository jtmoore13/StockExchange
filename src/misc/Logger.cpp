#include "Logger.h"
#include "Colors.h"
#include "fmt/core.h"
#include "Colors.h"
#include <chrono>
#include <mutex>

using namespace Colors;

namespace {
   std::string GetTimeStr(std::chrono::time_point<std::chrono::system_clock> now)
   {
      // libstdc++ lacks full support for fmt::format("{:%H:%M:%S.%f}", now) :(
      std::time_t time = std::chrono::system_clock::to_time_t(now);
      std::tm localTime;
      localtime_r(&time, &localTime);
      auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) % 1000000;
      return fmt::format("{:02}:{:02}:{:02}.{:06}",
                        localTime.tm_hour,
                        localTime.tm_min,
                        localTime.tm_sec,
                        us.count());
   }
}


/////////////////////////////////////////////////////////////////////////////////////////////////
// LoggerBase

void LoggerBase::LogError_(const std::string& msg) const 
{
   Log_(ColorUtils::Wrap("ERROR: " + msg, Colors::Red));
}

void LoggerBase::LogWarning_(const std::string& msg) const
{
   Log_(ColorUtils::Wrap("WARNING: " + msg, Colors::Orange));
}


/////////////////////////////////////////////////////////////////////////////////////////////////
// Logger

/*static*/ void Logger::LogMsg(const std::string& msg, const std::string& prefix, std::chrono::time_point<std::chrono::system_clock> logTime)
{
   static std::mutex outputMutex;
   std::lock_guard lock(outputMutex);
   fmt::print("{}[{}]{}{}: {}{}\n", LoggingPrefix::timeColor, GetTimeStr(logTime), Reset, prefix, msg, Reset);
}



