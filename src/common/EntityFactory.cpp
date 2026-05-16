#include "pace/EntityFactory.hpp"

#include "pace/MqttService.hpp"
#include "pace/Pace.hpp"

#include "pace/commands/KillCommand.hpp"
#include "pace/commands/NotifyCommand.hpp"
#include "pace/commands/StopCommand.hpp"
#include "pace/commands/SystemActionCommand.hpp"

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
         return nullptr;
      }
   }

   using EntityCreator = std::function<pace::entities::EntityPtr( pace::Pace&, pace::MqttService&, const nlohmann::json& )>;

   template <typename T>
   concept HasConfig = requires { typename T::Config; };

   /// Returns the correct tryBuildEntity instantiation for T,
   /// dispatching on whether T exposes a Config type alias.
   template <typename T>
   EntityCreator makeCreator()
   {
      if constexpr( HasConfig<T> )
      {
         return tryBuildEntity<T, typename T::Config>;
      }
      else
      {
         return tryBuildEntity<T>;
      }
   }

   /// Builds the registry from a parameter pack of entity types.
   /// Each type must expose a static constexpr std::string_view kType.
   template <typename... Ts>
   std::map<std::string, EntityCreator> makeCreatorMap()
   {
      std::map<std::string, EntityCreator> ret;
      ( ret.emplace( std::string{ Ts::kType }, makeCreator<Ts>() ), ... );
      return ret;
   }

   /// Registry keyed by subtype.
   const std::map<std::string, EntityCreator> entityCreators = makeCreatorMap<
      pace::sensors::GameSensor, pace::switches::ProcessSwitch, pace::commands::NotifyCommand, pace::commands::StopCommand,
      pace::commands::LockCommand, pace::commands::SleepCommand, pace::commands::RebootCommand, pace::commands::ShutdownCommand>();
}

namespace pace
{
   EntityFactory::EntityFactory( Pace& paceService, MqttService& mqttService )
      : pace( paceService )
      , mqtt( mqttService )
      , entityConfigs()
   {}

   util::Task<bool> EntityFactory::subscribe()
   {
      co_await mqtt.subscribe( "entity/+/config",
                               [ this ]( const std::string& topic, const std::string& data ) -> util::Task<bool>
                               {
                                  auto parts = mqtt::topic::split( topic );
                                  // Is this even possible?
                                  if( parts.size() != 5 )
                                  {
                                     co_return false;
                                  }

                                  const auto& entityName = parts[ 3 ];
                                  auto        json       = nlohmann::json::parse( data, nullptr, false );
                                  if( json.is_discarded() )
                                  {
                                     logger->warn( "Invalid JSON in full config for entity {}", entityName );
                                     co_return false;
                                  }
                                  co_return co_await onFullConfig( entityName, std::move( json ) );
                               } );

      co_await mqtt.subscribe( "entity/+/config/+",
                               [ this ]( const std::string& topic, const std::string& data ) -> util::Task<bool>
                               {
                                  auto parts = mqtt::topic::split( topic );
                                  // is this even possible?
                                  if( parts.size() != 6 )
                                  {
                                     co_return false;
                                  }

                                  const auto& entityName = parts[ 3 ];
                                  const auto& configNode = parts.back();

                                  auto& partialConfig         = entityConfigs[ entityName ];
                                  partialConfig[ configNode ] = parseConfigValue( data );
                                  if( ! partialConfig.contains( "type" ) )
                                  {
                                     co_return false;
                                  }
                                  co_return co_await onFullConfig( entityName, partialConfig );
                               } );
      co_return true;
   }

   util::Task<bool> EntityFactory::onFullConfig( const std::string& name, nlohmann::json config )
   {
      if( ! config.contains( "type" ) || ! config[ "type" ].is_string() )
      {
         logger->info( "Config for {} missing or invalid 'type' field", name );
         co_await pace.removeEntity( name );
         co_return false;
      }
      const auto& type = config[ "type" ].get<std::string>();
      auto        it   = entityCreators.find( type );
      if( it == entityCreators.end() )
      {
         logger->warn( "Unknown entity type: {}", type );
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
      else
      {
         logger->info( "Config for {} of 'type' {} is invalid", name, type );
      }
      co_return false;
   }
} // namespace pace
