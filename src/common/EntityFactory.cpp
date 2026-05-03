#include "pace/EntityFactory.hpp"

#include "pace/MqttService.hpp"
#include "pace/Pace.hpp"

#include "pace/commands/ExecCommand.hpp"
#include "pace/commands/KillCommand.hpp"
#include "pace/commands/NotifyCommand.hpp"
#include "pace/commands/NullCommand.hpp"
#include "pace/commands/PingCommand.hpp"
#include "pace/commands/StopCommand.hpp"
#include "pace/commands/SystemActionCommand.hpp"

#include "pace/sensors/CountSensor.hpp"
#include "pace/sensors/GameSensor.hpp"
#include "pace/sensors/ProcSensor.hpp"

#include "pace/switches/ProcessSwitch.hpp"

#include <mqtt/topic_matcher.h>
#include <spdlog/spdlog.h>

#include <functional>
#include <type_traits>

namespace
{
   nlohmann::json parseConfigValue( const std::string& data )
   {
      auto json = nlohmann::json::parse( data, nullptr, false );
      if( json.is_discarded() )
      {
         // Keep invalid JSON payloads as raw strings and let config deserialization decide.
         return data;
      }
      return json;
   }

   template <typename TEntity>
   pace::entities::EntityPtr tryBuildEntity( pace::Pace& pace, pace::MqttService& mqtt, const nlohmann::json& )
   {
      if constexpr( std::is_constructible_v<TEntity, pace::MqttService&, pace::Pace&> )
      {
         return std::make_unique<TEntity>( mqtt, pace );
      }
      else
      {
         return std::make_unique<TEntity>( mqtt );
      }
   }

   template <typename TEntity, typename TConfig>
   pace::entities::EntityPtr tryBuildEntity( pace::Pace&, pace::MqttService& mqtt, const nlohmann::json& config )
   {
      try
      {
         auto cfg = config.get<TConfig>();
         return std::make_unique<TEntity>( mqtt, cfg );
      }
      catch( const nlohmann::json::exception& e )
      {
         spdlog::debug( "Failed to build entity config: {}", e.what() );
         return nullptr;
      }
   }

   /// Registry keyed by subtype.
   using EntityCreator = std::function<pace::entities::EntityPtr( pace::Pace&, pace::MqttService&, const nlohmann::json& )>;
   const std::map<std::string, EntityCreator> entityCreators{
      { "count", tryBuildEntity<pace::sensors::CountSensor, pace::entities::config::EntityConfig> },
      { "game", tryBuildEntity<pace::sensors::GameSensor, pace::sensors::config::GameSensorConfig> },

      { "proc", tryBuildEntity<pace::switches::ProcessSwitch, pace::switches::config::ProcessSwitchConfig> },

      { "null", tryBuildEntity<pace::commands::NullCommand> },
      { "ping", tryBuildEntity<pace::commands::PingCommand> },
      { "notify", tryBuildEntity<pace::commands::NotifyCommand> },
      { "stop", tryBuildEntity<pace::commands::StopCommand> },
      { "lock", tryBuildEntity<pace::commands::LockCommand> },
      { "sleep", tryBuildEntity<pace::commands::SleepCommand> },
      { "reboot", tryBuildEntity<pace::commands::RebootCommand> },
      { "shutdown", tryBuildEntity<pace::commands::ShutdownCommand> },
   };
}

namespace pace
{
   EntityFactory::EntityFactory( Pace& p, MqttService& m )
      : pace( p )
      , mqtt( m )
   {}

   util::Task<bool> EntityFactory::subscribe()
   {
      co_await mqtt.subscribe( "entity/+/config",
                               [ this ]( const std::string& topic, const std::string& data ) -> util::Task<bool>
                               {
                                  auto parts = mqtt::topic::split( topic );
                                  if( parts.size() != 5 )
                                  {
                                     spdlog::warn( "Invalid entity full config topic: {}", topic );
                                     co_return false;
                                  }

                                  const auto& entityName = parts[ 3 ];
                                  co_return co_await onFullConfig( entityName, data );
                               } );

      co_await mqtt.subscribe( "entity/+/config/+",
                               [ this ]( const std::string& topic, const std::string& data ) -> util::Task<bool>
                               {
                                  auto parts = mqtt::topic::split( topic );
                                  if( parts.size() < 6 )
                                  {
                                     spdlog::warn( "Invalid entity partial config topic: {}", topic );
                                     co_return false;
                                  }

                                  const auto& entityName = parts[ 3 ];
                                  const auto& configNode = parts.back();
                                  co_return co_await onPartialConfig( entityName, configNode, data );
                               } );
      co_return true;
   }

   util::Task<bool> EntityFactory::onFullConfig( const std::string& name, const std::string& data )
   {
      auto json = nlohmann::json::parse( data, nullptr, false );
      if( json.is_discarded() )
      {
         spdlog::warn( "Invalid JSON in full config for entity {}", name );
         co_return false;
      }

      if( ! json.is_object() )
      {
         spdlog::warn( "Full config for {} must be a JSON object", name );
         co_return false;
      }

      co_return co_await onFullConfig( name, std::move( json ) );
   }

   util::Task<bool> EntityFactory::onFullConfig( const std::string& name, nlohmann::json config )
   {
      if( ! config.contains( "type" ) || ! config[ "type" ].is_string() )
      {
         spdlog::warn( "Config for {} missing or invalid 'type' field", name );
         co_await pace.removeEntity( name );
         co_return false;
      }
      const auto& type = config[ "type" ].get<std::string>();
      auto        it   = entityCreators.find( type );
      if( it == entityCreators.end() )
      {
         spdlog::warn( "Unknown entity type: {}", type );
         co_await pace.removeEntity( name );
         co_return false;
      }

      // before updating, remove the existing entity
      co_await pace.removeEntity( name );

      config[ "name" ] = name;
      auto entity      = it->second( pace, mqtt, config );
      if( entity )
      {
         co_return co_await pace.addEntity( std::move( entity ) );
      }
      co_return false;
   }

   util::Task<bool> EntityFactory::onPartialConfig( const std::string& name, const std::string& node, const std::string& data )
   {
      auto& partialConfig   = entityConfigs[ name ];
      partialConfig[ node ] = parseConfigValue( data );

      if( ! partialConfig.contains( "type" ) )
      {
         co_return true;
      }

      co_return co_await onFullConfig( name, partialConfig );
   }
} // namespace pace
