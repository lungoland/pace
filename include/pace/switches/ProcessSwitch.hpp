#pragma once

#include "pace/commands/SystemExecutor.hpp"
#include "pace/sensors/SensorProvider.hpp"

#include "pace/switches/BaseSwitch.hpp"

#include <fmt/ranges.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace pace::switches
{
   namespace config
   {
      struct ProcessSwitchConfig : entities::config::EntityConfig
      {
            std::string imagePath{};
      };

      // ProcessSwitchConfig adds 'imagePath' as required field
      // Inherits 'name' (required) and 'interval' (default) from EntityConfig
      NLOHMANN_DEFINE_DERIVED_TYPE_NON_INTRUSIVE( ProcessSwitchConfig, entities::config::EntityConfig, imagePath )
   }

   /// @brief Switch to start/stop a managed process
   class ProcessSwitch : public BaseSwitch<ProcessSwitch, bool, config::ProcessSwitchConfig>
   {
      public:

         static constexpr std::string_view kType = "proc";
         using Config                            = config::ProcessSwitchConfig;

         explicit ProcessSwitch( MqttService& mqttService, const config::ProcessSwitchConfig& cfg )
            : BaseSwitch( mqttService, cfg )
         {
            if( auto pos = cfg.imagePath.find_last_of( "/\\" ); pos != std::string::npos )
            {
               processName = cfg.imagePath.substr( pos + 1 );
            }
            else
            {
               processName = cfg.imagePath;
            }
         }

         util::Task<ResponseType> execute( bool request ) override
         {
            auto running = co_await fetch();
            logger->info( "Process '{}' is currently {}; wanted {}", processName, running ? "running" : "stopped",
                          request ? "running" : "stopped" );
            if( request == running )
            {
               co_return running;
            }

            if( request )
            {
               logger->debug( "Starting process '{}' with image path '{}'", processName, config.imagePath );
               auto spawnResult = commands::impl::spawnNewProcessGroup( config.imagePath );
               if( ! spawnResult )
               {
                  logger->warn( "Failed to spawn process '{}' in dedicated process group: {}. Falling back to regular spawn.", processName,
                                spawnResult.error() );

                  auto fallbackSpawnResult = commands::impl::spawnNewProcess( config.imagePath );
                  if( ! fallbackSpawnResult )
                  {
                     co_return util::unexpected{ fallbackSpawnResult.error() };
                  }

                  managedProcessGroupId.reset();
                  co_return true;
               }

               managedProcessGroupId = *spawnResult;
               logger->debug( "Process '{}' started with PGID '{}'", processName, *managedProcessGroupId );
               co_return true;
            }
            else
            {
               util::VoidResult killResult = util::unexpected{ "no managed process group present" };

               if( managedProcessGroupId )
               {
                  logger->debug( "Killing process '{}' by managed PGID '{}'", processName, *managedProcessGroupId );
                  killResult = co_await commands::impl::killProcessGroup( *managedProcessGroupId );
               }
               else
               {
                  logger->warn( "No managed PGID for process '{}'; falling back to name-based kill", processName );
                  killResult = co_await commands::impl::killProcessByName( processName );
               }

               if( ! killResult )
               {
                  co_return util::unexpected{ killResult.error() };
               }

               managedProcessGroupId.reset();
               co_return false;
            }
         }

         util::Task<bool> fetch() const override
         {
            if( managedProcessGroupId )
            {
               auto pids = sensors::impl::findPidsByProcessGroup( *managedProcessGroupId );
               logger->trace( "Relevant PIDs for process '{}' in PGID '{}': [{}]", processName, *managedProcessGroupId,
                              fmt::join( pids, ", " ) );
               co_return ! pids.empty();
            }

            auto pids = sensors::impl::findPidsByName( processName );
            logger->trace( "Relevant PIDs for process '{}': [{}]", processName, fmt::join( pids, ", " ) );
            co_return pids.size() > 0;
         }

      private:

         std::string                 processName;
         std::optional<std::int64_t> managedProcessGroupId;
   };

} // namespace pace::switches
