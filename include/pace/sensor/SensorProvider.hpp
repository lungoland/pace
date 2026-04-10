#pragma once

#include "util/expected.hpp"

#include <memory>
#include <string>

namespace pace::sensor
{
   class SensorProvider
   {
      public:

         virtual ~SensorProvider() = default;

         virtual util::expected<double, std::string> readCpuTemperatureC() const    = 0;
         virtual util::expected<double, std::string> readMemoryUsagePercent() const = 0;
   };

   std::unique_ptr<SensorProvider> createSensorProvider();

} // namespace pace::sensor
