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

   /// @brief Finds process IDs by their name.
   /// @param processName The name of the process to search for.
   /// @return A vector of process IDs that match the given process name.
   std::vector<std::uint32_t> findPidsByName( const std::string& processName );

   /// @brief Returns a list of process names that have loaded GPU-related libraries, which can be an indicator of running games.
   /// @return A set of process names that have loaded GPU-related libraries.
   std::set<std::string> procsWithLoaded3DLibs();

} // namespace pace::sensors::impl
