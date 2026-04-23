#pragma once

#include "util/expected.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>


namespace pace::sensors::impl
{
   util::expected<double, std::string> readCpuTemperatureC();

   util::expected<double, std::string> readMemoryUsagePercent();

   std::vector<std::uint32_t> findPidsByName( const std::string& processName );

   /// Returns true if any running process has a 3D graphics library loaded,
   /// indicating a game or GPU-accelerated application is active on the system.
   std::set<std::string> procsWithLoaded3DLibs();

} // namespace pace::sensors::impl
