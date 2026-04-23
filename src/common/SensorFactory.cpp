#include "pace/SensorFactory.hpp"
#include "pace/MqttService.hpp"
#include "pace/Pace.hpp"

#include "pace/sensors/CountSensor.hpp"
#include "pace/sensors/GameSensor.hpp"
#include "pace/sensors/ProcSensor.hpp"

#include <mqtt/topic_matcher.h>
#include <spdlog/spdlog.h>

#include <charconv>
#include <functional>

namespace
{

   template <typename T>
   std::optional<T> fromChars( const std::string& str )
   {
      T result{};
      if( std::from_chars( str.data(), str.data() + str.size(), result ).ec == std::errc() )
      {
         return result;
      }
      return std::nullopt;
   }

   /// @brief Try to build a typed config and construct a sensor of type T with it.
   /// @tparam T Type of the sensor to construct.
   /// @tparam TConfig Type of the config to build, must be constructible from JSON.
   /// @param mqtt MQTT service to pass to the sensor constructor.
   /// @param config JSON config to build the sensor config from.
   /// @return A unique pointer to the constructed sensor, or nullptr if required fields are missing or of wrong type.
   template <typename T, typename TConfig>
   std::unique_ptr<T> tryBuildSensor( pace::MqttService& mqtt, const nlohmann::json& config )
   {
      try
      {
         auto cfg = std::make_unique<TConfig>();
         config.get_to( *cfg );
         return std::make_unique<T>( mqtt, std::move( cfg ) );
      }
      catch( const nlohmann::json::exception& e )
      {
         // dont log - at startup the config will be invald so no point in logging
         // spdlog::debug( "Failed to build sensor config: {}", e.what() );
         return nullptr;
      }
   }

   /// @brief Registry of sensor factory functions keyed by sensor type name.
   /// Each entry builds a fully-constructed sensor ready for scheduling.
   /// TODO: custom functions this is kinda ugly!
   using SensorCreator = std::function<pace::sensors::SensorPtr( pace::MqttService&, const nlohmann::json& )>;
   const std::map<std::string, SensorCreator> sensorCreators{
      { "count", tryBuildSensor<pace::sensors::CountSensor, pace::sensors::config::BaseSensorConfig> },
      { "game",  tryBuildSensor<pace::sensors::GameSensor,  pace::sensors::config::GameSensorConfig> },
      { "proc",  tryBuildSensor<pace::sensors::ProcSensor,  pace::sensors::config::ProcSensorConfig> },
   };
}

namespace pace
{
   util::Task<bool> SensorFactory::subscribe()
   {
      co_await mqtt.subscribe( "sensor/+/config/+",
                               [ this ]( const std::string& topic, const std::string& data ) -> util::Task<bool>
                               {
                                  auto parts = mqtt::topic::split( topic );
                                  if( parts.size() < 6 )
                                  {
                                     spdlog::warn( "Invalid sensor config topic: {}", topic );
                                     co_return false;
                                  }

                                  /// TODO: guard the access/validate the content somehow?
                                  const auto& sensorName = parts[ 3 ];
                                  const auto& configNode = parts[ 5 ];
                                  onPartialConfig( sensorName, configNode, data );
                                  co_return true;
                               } );
      co_return true;
   }

   void SensorFactory::onPartialConfig( const std::string& name, const std::string& node, const std::string& data )
   {
      auto& partialConfig     = sensorConfigs[ name ];
      partialConfig[ "name" ] = name;

      if( auto floatResult = fromChars<float>( data ); floatResult )
      {
         partialConfig[ node ] = floatResult.value();
      }
      else if( auto intResult = fromChars<long>( data ); intResult )
      {
         partialConfig[ node ] = intResult.value();
      }
      else
      {
         // may be a solution for numbers as well?
         auto json = nlohmann::json::parse( data, nullptr, false );
         if( json.is_discarded() )
         {
            // just store it as string and hope for the best when building the config
            partialConfig[ node ] = data;
         }
         else
         {
            partialConfig[ node ] = json;
         }
      }

      if( ! partialConfig.contains( "type" ) )
      {
         return;
      }

      const auto& typeStr = partialConfig[ "type" ].get<std::string>();
      if( typeStr.empty() )
      {
         /// Topic got deleted -> so lets remove the sensor
         pace.removeSensor( name );
         return;
      }

      auto it = sensorCreators.find( typeStr );
      if( it == sensorCreators.end() )
      {
         spdlog::warn( "Unknown sensor type: {}", typeStr );
         return;
      }

      /// Config changes or is no longer valid in general -> remove the sensor
      /// and potentially re-create a new instance with the updated config.
      pace.removeSensor( name );
      auto sensor = it->second( mqtt, partialConfig );
      if( sensor )
      {
         /// easy and works ..
         /// I liked the queue back to pace which is polling the queue
         /// but that does not work with current co-routine setup :(
         /// GameSensor does have problems with reload ... why is still unclear.
         pace.addSensor( std::move( sensor ) );
      }
   }
} // namespace pace
