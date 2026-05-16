#pragma once

#include "pace/commands/SystemExecutor.hpp"
#include "pace/sensors/SensorProvider.hpp"

#include "pace/switches/BaseSwitch.hpp"

#include <fmt/ranges.h>
#include <nlohmann/json.hpp>

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
               // TODO: On Linux systems this is a bit more tricky.
               // By design we will be the parent of the forked process.
               // In theory this also means that if we die or stop, we will take down the child as well.
               // Maybe systemd has some wraper?
               // TODO: Also it seems that we cannot SIGTERM the child??
               // TODO: Maybe firefox is just special as it surrived termination of pace
               logger->debug( "Starting process '{}' with image path '{}'", processName, config.imagePath );
               commands::impl::spawnNewProcess( config.imagePath );
               co_return true;
            }
            else
            {
               logger->debug( "Killing process '{}' by name", processName );
               co_await commands::impl::killProcessByName( processName );
               co_return false;
            }
         }

         util::Task<bool> fetch() const override
         {
            auto pids = sensors::impl::findPidsByName( processName );
            logger->trace( "Relevant PIDs for process '{}': [{}]", processName, fmt::join( pids, ", " ) );
            co_return pids.size() > 0;
         }

      private:

         std::string processName;
   };

} // namespace pace::switches
