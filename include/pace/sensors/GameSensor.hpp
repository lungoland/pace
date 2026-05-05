#pragma once

#include "pace/sensors/BaseSensor.hpp"
#include "pace/sensors/SensorProvider.hpp"

#include <fmt/ranges.h>

#include <set>
#include <vector>

namespace pace::sensors
{
   namespace config
   {
      struct GameSensorConfig : entities::config::EntityConfig
      {
            std::vector<std::string> ignoreProcesses;

            // GameSensorConfig adds 'ignoreProcesses' as an optional field
            // Inherits 'name' (required) and 'interval' (default) from EntityConfig
            NLOHMANN_DEFINE_DERIVED_TYPE_INTRUSIVE_WITH_DEFAULT( GameSensorConfig, entities::config::EntityConfig, ignoreProcesses )
      };
   }


   /// @brief A sensor that returns true if a game is running.
   /// Current detection mechanism relies on checking if a GPU-related DLL is loaded by a running process.
   class GameSensor : public BaseSensor<bool, config::GameSensorConfig>
   {
      public:

         using BaseSensor::BaseSensor;

         util::Task<bool> fetch() const override
         {
            auto candidates = impl::procsWithLoaded3DLibs();
            for( const auto& ignore : config.ignoreProcesses )
            {
               candidates.erase( ignore );
            }

            // Given the list of candidate processes .. this apraoch is not scaleable ...
            spdlog::trace( "GameSensor found candidate processes: [{}]", fmt::join( candidates, ", " ) );
            co_return ! candidates.empty();
         }
   };
} // namespace pace::sensors
