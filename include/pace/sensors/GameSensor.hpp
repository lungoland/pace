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
            std::vector<std::string> exclude;

            // GameSensorConfig adds 'exclude' as an optional field
            // Inherits 'name' (required) and 'interval' (default) from EntityConfig
            NLOHMANN_DEFINE_DERIVED_TYPE_INTRUSIVE_WITH_DEFAULT( GameSensorConfig, entities::config::EntityConfig, exclude )
      };
   }


   /// @brief A sensor that returns true if a game is running.
   /// Current detection mechanism relies on checking if a GPU-related DLL is loaded by a running process.
   class GameSensor : public BaseSensor<bool, config::GameSensorConfig>
   {
      public:

         static constexpr std::string_view kType = "game";
         using Config                            = config::GameSensorConfig;

         using BaseSensor::BaseSensor;

         util::Task<bool> fetch() const override
         {
            auto candidates = impl::procsWithLoaded3DLibs();
            for( const auto& ignore : config.exclude )
            {
               candidates.erase( ignore );
            }

            // Given the list of candidate processes .. this approach is not scalable ...
            // TODO: Add button to auto populate exclude list
            // But code in here, or implement a home-assistant automation
            // TODO: once this works, we could also code a home-assistant automation to auto-generate
            // a proc switch for detected games
            spdlog::trace( "GameSensor found candidate processes: [{}]", fmt::join( candidates, ", " ) );
            lastCandidates = candidates;
            co_return ! candidates.empty();
         }

         std::optional<nlohmann::json> getAttributes() const override
         {
            return std::make_optional<nlohmann::json>( {
               { "processes", lastCandidates }
            } );
         }

      private:

         mutable std::set<std::string> lastCandidates;
   };
} // namespace pace::sensors
