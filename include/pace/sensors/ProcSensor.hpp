#pragma once

#include "pace/sensors/BaseSensor.hpp"
#include "pace/sensors/SensorProvider.hpp"

#include <nlohmann/json.hpp>

namespace pace::sensors
{
   namespace config
   {
      struct ProcSensorConfig : BaseSensorConfig
      {
            std::string processName;
      };

      // ProcSensorConfig adds 'processName' as required field
      // Inherits 'name' (required) and 'interval' (default) from BaseSensorConfig
      NLOHMANN_DEFINE_DERIVED_TYPE_NON_INTRUSIVE( ProcSensorConfig, BaseSensorConfig, processName )
   }

   /// @brief A sensor that returns true if a process is running.
   class ProcSensor : public BaseSensor<bool, config::ProcSensorConfig>
   {
      public:

         using BaseSensor::BaseSensor;

         util::Task<bool> fetch() const override
         {
            auto pids = impl::findPidsByName( config.processName );
            co_return ! pids.empty();
         }
   };
} // namespace pace::sensors
