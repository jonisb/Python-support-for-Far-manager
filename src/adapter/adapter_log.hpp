#pragma once

// All code in src/adapter/ logs to the adapter log file.
// Defining PYTHONFAR_LOGGER before including common_log.hpp selects the logger.
#define PYTHONFAR_LOGGER PythonFar::GetAdapterLogger()
#include "../common_log.hpp"
