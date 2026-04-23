#pragma once

#include "util/Task.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <type_traits>


/// TODO: move into own class
namespace nlohmann
{
   template <>
   struct adl_serializer<std::chrono::milliseconds>
   {
         static void to_json( nlohmann::json& j, const std::chrono::milliseconds& ms )
         {
            j = ms.count();
         }

         static void from_json( const nlohmann::json& j, std::chrono::milliseconds& ms )
         {
            ms = std::chrono::milliseconds( j.get<long long>() );
         }
   };
}

namespace pace
{
   class MqttService;
}

namespace pace::sensors
{
   namespace config
   {
      struct BaseSensorConfig
      {
            std::string               name;
            std::chrono::milliseconds interval = std::chrono::milliseconds{ 5000 };
      };

      // Custom serialization for BaseSensorConfig: 'name' is required, 'interval' has a default
      inline void to_json( nlohmann::json& j, const BaseSensorConfig& cfg )
      {
         j[ "name" ]     = cfg.name;
         j[ "interval" ] = cfg.interval;
      }

      inline void from_json( const nlohmann::json& j, BaseSensorConfig& cfg )
      {
         cfg.name = j.at( "name" ).get<std::string>(); // required: throws if missing
         if( j.contains( "interval" ) )
         {
            cfg.interval = j[ "interval" ].get<std::chrono::milliseconds>();
         }
         // else: use default from struct definition (5000ms)
      }
   }

   /// @brief Base class for all sensors. Defines the interface and common logic for publishing sensor data to MQTT.
   class SensorInterface
   {
      public:

         static constexpr int32_t MAX_DEBOUNCE = 5;

         explicit SensorInterface( MqttService& mqttService, std::unique_ptr<config::BaseSensorConfig> config );
         virtual ~SensorInterface() = default;

         /// @brief Main interface for calling code to fetch sensor data and publish to MQTT.
         /// @return  true if new data was published, false if no data could be published (also including debounce)
         util::Task<bool> fetchAndPublish();

         /// @brief Fetch the sensor data. This is the main function that derived sensors need to implement to
         /// provide their specific data fetching logic.
         /// @return  The fetched sensor data as a string.
         /// TODO: Find a way to remove the "_" while working with BaseSensor
         virtual util::Task<std::string> fetch_() const = 0;

         /// @brief Name of the sensor and subsequent the MQTT topic:
         ///        pace/{node}/sensor/{name}/state
         std::string               name() const;
         std::chrono::milliseconds interval() const;

      private:

         MqttService&                              mqtt;
         std::unique_ptr<config::BaseSensorConfig> config;

         /// @brief Cache the last published data to implement debounce logic
         std::string lastData;
         /// @brief Counter to track how many times the same data has been returned by fetch_ to implement debounce logic
         /// TODO: Add Reset Command to reset debounce?
         int32_t debounce = 0;
   };
   using SensorPtr = std::unique_ptr<SensorInterface>;

   /// @brief Template base class for typed sensors. Provides a default implementation of fetch_ that converts the typed data to string.
   /// @tparam T The type of the sensor data. The type must have a std::to_string overload.
   /// @tparam TConfig The config type for this sensor. Must derive from BaseSensorConfig.
   template <typename T, typename TConfig = config::BaseSensorConfig>
   class BaseSensor : public SensorInterface
   {
         static_assert( std::is_base_of_v<config::BaseSensorConfig, TConfig>, "TConfig must derive from BaseSensorConfig" );

      public:

         BaseSensor( MqttService& mqttService, std::unique_ptr<TConfig> cfg = std::make_unique<TConfig>() )
            : BaseSensor( mqttService, std::move( cfg ), cfg.get() )
         {}

         /// @brief Fetch the sensor data and convert it to a string.
         /// @return The fetched sensor data as a string.
         util::Task<std::string> fetch_() const override
         {
            if constexpr( std::is_same_v<T, std::string> )
            {
               return fetch();
            }
            co_return std::to_string( co_await fetch() );
         }

         /// @brief Fetch the sensor data. This is the main function that derived sensors
         /// need to implement to provide their specific data fetching logic.
         /// @return The fetched sensor data as the specific type T.
         virtual util::Task<T> fetch() const = 0;

      protected:

         const TConfig& config;

      private:

         /// Delegating constructor: captures raw pointer before unique_ptr ownership transfer,
         /// avoiding the UB of dereferencing cfg after std::move(cfg) in the MIL.
         BaseSensor( MqttService& mqttService, std::unique_ptr<TConfig> cfg, TConfig* raw )
            : SensorInterface( mqttService, std::move( cfg ) )
            , config( *raw )
         {}
   };
} // namespace pace::sensors
