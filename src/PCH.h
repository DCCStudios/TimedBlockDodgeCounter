#pragma once

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <SimpleIni.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <atomic>
#include <chrono>
#include <thread>

// Undefine Windows API macros that conflict with CommonLibSSE functions
#ifdef PlaySound
#undef PlaySound
#endif

using namespace std::literals;
namespace logger = SKSE::log;
