#pragma once

#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <iostream>
#include <sstream>
#include <cstdlib>

namespace PythonFar {

// Logging Configuration
// Default temp directory fallback (when TEMP env var is not set)
inline constexpr const char* DEFAULT_TEMP_DIR = "C:\\temp";

// Safe environment variable getter (replaces unsafe getenv)
inline std::string SafeGetEnv(const char* name, const std::string& defaultValue = "") {
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) == 0 && value != nullptr) {
        std::string result(value);
        free(value);
        return result;
    }
    return defaultValue;
}

enum class LogLevel {
    Trace = 0,
    Info = 1,
    Error = 2
};

class Logger {
public:
    Logger(const std::string& logFileName) : m_logFileName(logFileName) {
        std::string tempDir = SafeGetEnv("TEMP", DEFAULT_TEMP_DIR);
        std::string logPath = tempDir + "\\" + logFileName;
        
        // Clear log on initialization (truncate mode)
        m_logFile.open(logPath, std::ios::trunc);
        m_logFile.close();
        
        // Reopen in append mode
        m_logFile.open(logPath, std::ios::app);
        
        // Check environment variable for log level
        std::string levelStr = SafeGetEnv("PYTHONFAR_LOG_LEVEL");
        if (!levelStr.empty()) {
            if (levelStr == "TRACE" || levelStr == "0") m_minLevel = LogLevel::Trace;
            else if (levelStr == "INFO" || levelStr == "1") m_minLevel = LogLevel::Info;
            else if (levelStr == "ERROR" || levelStr == "2") m_minLevel = LogLevel::Error;
        }
    }
    
    ~Logger() {
        if (m_logFile.is_open()) {
            m_logFile.close();
        }
    }

    void Log(LogLevel level, const char* file, int line, const std::string& message) {
        if (level < m_minLevel) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        std::ostringstream ss;
        
        switch (level) {
            case LogLevel::Trace: ss << "[TRACE]"; break;
            case LogLevel::Info:  ss << "[INFO] "; break;
            case LogLevel::Error: ss << "[ERROR]"; break;
        }
        
        // Strip path from file
        std::string filename = file;
        size_t pos = filename.find_last_of("/\\");
        if (pos != std::string::npos) {
            filename = filename.substr(pos + 1);
        }

        ss << " [" << filename << ":" << line << "] " << message << "\n";
        
        std::string logStr = ss.str();

        if (m_logFile.is_open()) {
            m_logFile << logStr;
            m_logFile.flush();
        }
        
        // Also write to std::cerr if error
        if (level >= LogLevel::Error) {
            std::cerr << logStr;
        }
    }

private:
    std::ofstream m_logFile;
    std::mutex m_mutex;
    std::string m_logFileName;
    LogLevel m_minLevel = LogLevel::Trace;
};

// Forward declarations - defined in logger.cpp
Logger& GetLoaderLogger();
Logger& GetAdapterLogger();

// Convert a UTF-16 wide string to a proper UTF-8 byte string.
// NOTE: this performs a real WideCharToMultiByte(CP_UTF8) conversion; it must
// NOT truncate each wchar_t to a byte, which would corrupt any non-ASCII text
// (e.g. Cyrillic/CJK paths and messages) and break string comparisons.
inline std::string WideToUTF8(const wchar_t* wide) {
    if (!wide) return "(null)";
    if (*wide == L'\0') return std::string();

    const int sizeNeeded = WideCharToMultiByte(
        CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) return std::string();

    // sizeNeeded includes the terminating null; allocate without it.
    std::string result(static_cast<size_t>(sizeNeeded - 1), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide, -1, result.data(), sizeNeeded, nullptr, nullptr);
    return result;
}

// Convert a UTF-8 byte string to a UTF-16 wide string.
// NOTE: this performs a real MultiByteToWideChar(CP_UTF8) conversion; it must
// NOT widen each byte to a wchar_t, which would corrupt any non-ASCII text
// (e.g. Cyrillic/CJK plugin names and paths) and can break plugin loading on
// localized systems.
inline std::wstring UTF8ToWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();

    const int sizeNeeded = MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (sizeNeeded <= 0) return std::wstring();

    std::wstring result(static_cast<size_t>(sizeNeeded), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
        result.data(), sizeNeeded);
    return result;
}

} // namespace PythonFar

// Keep WideToUTF8 available unqualified for existing call sites
using PythonFar::WideToUTF8;

// Core logging macro - logs to the given logger instance.
// Each translation unit defines LOG_TRACE/LOG_INFO/LOG_ERROR in terms of this
// by selecting which logger to use (see PYTHONFAR_LOGGER below).
#define PYTHONFAR_LOG(logger, lvl, msg) \
    do { std::ostringstream _ss; _ss << msg; \
         (logger).Log(PythonFar::LogLevel::lvl, __FILE__, __LINE__, _ss.str()); } while(0)

// A translation unit may define PYTHONFAR_LOGGER before including this header
// (or any header that includes it) to select its logger. Defaults to the loader.
#ifndef PYTHONFAR_LOGGER
#define PYTHONFAR_LOGGER PythonFar::GetLoaderLogger()
#endif

#define LOG_TRACE(msg) PYTHONFAR_LOG(PYTHONFAR_LOGGER, Trace, msg)
#define LOG_INFO(msg)  PYTHONFAR_LOG(PYTHONFAR_LOGGER, Info,  msg)
#define LOG_ERROR(msg) PYTHONFAR_LOG(PYTHONFAR_LOGGER, Error, msg)
