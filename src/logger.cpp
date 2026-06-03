#include "common_log.hpp"

namespace PythonFar {

// Shared logger instances - defined here, linked by both DLLs
Logger& GetLoaderLogger() {
    static Logger instance("pythonfar_loader.log");
    return instance;
}

Logger& GetAdapterLogger() {
    static Logger instance("pythonfar_adapter.log");
    return instance;
}

} // namespace PythonFar
