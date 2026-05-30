#pragma once

#include <windows.h>
#include <string>
#include <fstream>
#include <mutex>
#include <iostream>
#include <sstream>
#include <cstdlib>

namespace PythonFar {

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
    static Logger& GetInstance() {
        static Logger instance;
        return instance;
    }

    void SetLogFile(const std::string& filepath) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_logFile.is_open()) {
            m_logFile.close();
        }
        m_logFile.open(filepath, std::ios::app);
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
        
        // Also write to std::cerr if not trace
        if (level >= LogLevel::Error) {
            std::cerr << logStr;
        }
    }

private:
    Logger() {
        // Default log file - use safe environment variable getter
        std::string tempDir = SafeGetEnv("TEMP", "C:\\temp");
        std::string logPath = tempDir + "\\pythonfar_loader.log";
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

    std::ofstream m_logFile;
    std::mutex m_mutex;
    
    LogLevel m_minLevel = LogLevel::Trace;
};

} // namespace PythonFar

// Utility function for converting wide strings to UTF-8 for logging
inline std::string WideToUTF8(const wchar_t* wide) {
    if (!wide) return "(null)";
    std::string result;
    while (*wide) {
        result += (char)*wide++;
    }
    return result;
}

#define LOG_TRACE(msg) do { std::ostringstream ss; ss << msg; PythonFar::Logger::GetInstance().Log(PythonFar::LogLevel::Trace, __FILE__, __LINE__, ss.str()); } while(0)
#define LOG_INFO(msg)  do { std::ostringstream ss; ss << msg; PythonFar::Logger::GetInstance().Log(PythonFar::LogLevel::Info, __FILE__, __LINE__, ss.str()); } while(0)
#define LOG_ERROR(msg) do { std::ostringstream ss; ss << msg; PythonFar::Logger::GetInstance().Log(PythonFar::LogLevel::Error, __FILE__, __LINE__, ss.str()); } while(0)
